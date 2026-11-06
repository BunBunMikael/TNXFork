// TranslateNX Live (modification): capture -> change detection -> OCR boxes -> ja->en -> HUD patches.
//
// Memory rules (the overlay heap is 4 MB by default):
//   * every large buffer is allocated once in start() and freed (after being wiped) in stop()
//   * the screenshot never touches the SD card; it lives in one 512 KB RAM buffer and is wiped
//     as soon as the translation of that frame is done
//   * uploads are streamed (base64 on the fly), no second copy of the image is built
//   * downloads are capped (kRespCap); an oversized answer is dropped, never grown into
#ifdef TNX_HUD

#include "live.hpp"
#include "live_logic.hpp"

#include <switch.h>
#include <curl/curl.h>
#include <cJSON.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdio.h>
#include <jpeglib.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace LiveLogic;

namespace Live {
namespace {

// ─── budgets ──────────────────────────────────────────────────────────────
constexpr size_t kJpegCap   = 512 * 1024;
constexpr size_t kRespCap   = 192 * 1024;
constexpr size_t kStackSize = 256 * 1024;
constexpr u64    kPollNs    = 2500ULL * 1000000ULL;     // how often the screen is checked
constexpr u64    kRetryNs   = 1200ULL * 1000000ULL;     // re-check while the picture is still moving
constexpr int    kMaxRetries = 2;
constexpr u64    kRetryDelayNs[2] = {5000000000ULL, 15000000000ULL};
constexpr int    kLayerW = 640;      // capture px -> layer px is /2
constexpr int    kCacheSize = 24;
constexpr const char* kCaPath = "sdmc:/config/translate/cacert_live.pem";

// ─── shared state ─────────────────────────────────────────────────────────
Mutex             g_mtx;
bool              g_mtxInit = false;
Snapshot          g_snap;
std::atomic<bool> g_abort{false};
bool              g_running = false;
Thread            g_thread;
Settings          g_set;
uint8_t*          g_jpeg = nullptr;
size_t            g_jpegLen = 0;
char*             g_resp = nullptr;
size_t            g_respLen = 0;
bool              g_verifyTls = false;
CURLcode          g_lastCurlResult = CURLE_OK;
char              g_curlError[CURL_ERROR_SIZE] = {0};

struct CacheEntry { uint64_t h = 0; char text[kTextMax] = {0}; };
CacheEntry g_cache[kCacheSize];
int        g_cacheNext = 0;

// ─── snapshot helpers ─────────────────────────────────────────────────────
void setStatus(const char* fmt, const char* a = "") {
    mutexLock(&g_mtx);
    snprintf(g_snap.status, sizeof(g_snap.status), fmt, a);
    if (g_set.debug) {
        const size_t n = strlen(g_snap.status);
        snprintf(g_snap.status + n, sizeof(g_snap.status) - n, " | heap %dK",
                 (int)(mallinfo().uordblks / 1024));
    }
    g_snap.version++;
    mutexUnlock(&g_mtx);
}

void setPatches(const std::vector<Patch>& p) {
    mutexLock(&g_mtx);
    g_snap.count = (int)p.size();
    for (size_t i = 0; i < p.size(); ++i) g_snap.patches[i] = p[i];
    g_snap.version++;
    mutexUnlock(&g_mtx);
}

bool sleepFor(u64 ns) {   // interruptible; false if we were told to stop
    while (ns > 0 && !g_abort.load(std::memory_order_acquire)) {
        const u64 step = ns > 50000000ULL ? 50000000ULL : ns;
        svcSleepThread(step);
        ns -= step;
    }
    return !g_abort.load(std::memory_order_acquire);
}

// ─── cache ────────────────────────────────────────────────────────────────
const CacheEntry* cacheFind(uint64_t h) {
    for (auto& e : g_cache) if (e.h == h && e.text[0]) return &e;
    return nullptr;
}
void cacheAdd(uint64_t h, const std::string& t) {
    CacheEntry& e = g_cache[g_cacheNext];
    g_cacheNext = (g_cacheNext + 1) % kCacheSize;
    e.h = h;
    snprintf(e.text, sizeof(e.text), "%s", truncateUtf8(t, kTextMax - 1).c_str());
}

// ─── HTTP (libcurl multi, so a close request can interrupt a transfer) ────
CURL*  g_easy  = nullptr;
CURLM* g_multi = nullptr;
bool   g_socketsUp = false;

size_t writeCb(char* p, size_t s, size_t n, void*) {
    const size_t k = s * n;
    if (g_abort.load(std::memory_order_relaxed)) return 0;
    if (g_respLen + k + 1 > kRespCap) return 0;     // too big: fail instead of growing
    memcpy(g_resp + g_respLen, p, k);
    g_respLen += k;
    g_resp[g_respLen] = 0;
    return k;
}
size_t readCb(char* dst, size_t s, size_t n, void* user) {
    if (g_abort.load(std::memory_order_relaxed)) return CURL_READFUNC_ABORT;
    return static_cast<B64Stream*>(user)->read(dst, s * n);
}

struct MimeSrc { const uint8_t* d; size_t len; size_t pos; };
size_t mimeRead(char* dst, size_t s, size_t n, void* user) {
    if (g_abort.load(std::memory_order_relaxed)) return CURL_READFUNC_ABORT;
    MimeSrc* m = static_cast<MimeSrc*>(user);
    size_t k = s * n;
    if (k > m->len - m->pos) k = m->len - m->pos;
    memcpy(dst, m->d + m->pos, k);
    m->pos += k;
    return k;
}
int mimeSeek(void* user, curl_off_t off, int origin) {
    MimeSrc* m = static_cast<MimeSrc*>(user);
    if (origin != SEEK_SET || off < 0 || (size_t)off > m->len) return CURL_SEEKFUNC_CANTSEEK;
    m->pos = (size_t)off;
    return CURL_SEEKFUNC_OK;
}

bool netUp() {
    // Small socket buffers: this overlay only ever has one connection at a time.
    static const SocketInitConfig cfg = {
        .tcp_tx_buf_size = 0x8000, .tcp_rx_buf_size = 0x8000,
        .tcp_tx_buf_max_size = 0, .tcp_rx_buf_max_size = 0,
        .udp_tx_buf_size = 0x1000, .udp_rx_buf_size = 0x1000,
        .sb_efficiency = 1, .num_bsd_sessions = 2, .bsd_service_type = BsdServiceType_Auto,
    };
    if (R_FAILED(socketInitialize(&cfg))) {
        if (R_FAILED(socketInitializeDefault())) return false;
    }
    g_socketsUp = true;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_easy = curl_easy_init();
    g_multi = curl_multi_init();
    if (g_multi) curl_multi_setopt(g_multi, CURLMOPT_MAXCONNECTS, 2L);
    struct stat st;
    g_verifyTls = (stat(kCaPath, &st) == 0);
    return g_easy && g_multi;
}

void netDown() {
    if (g_easy)  { curl_easy_cleanup(g_easy);   g_easy = nullptr; }
    if (g_multi) { curl_multi_cleanup(g_multi); g_multi = nullptr; }
    curl_global_cleanup();
    if (g_socketsUp) { socketExit(); g_socketsUp = false; }
}

// status: HTTP code, or 0 on transport failure, -1 when cancelled
int perform(CURL* h, curl_slist* headers, curl_mime* mime) {
    int status = 0;
    g_respLen = 0; g_resp[0] = 0;
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    if (mime) curl_easy_setopt(h, CURLOPT_MIMEPOST, mime);
    curl_multi_add_handle(g_multi, h);
    int running = 1;
    CURLcode result = CURLE_OK;
    bool cancelled = false;
    while (running) {
        if (curl_multi_perform(g_multi, &running) != CURLM_OK) { result = CURLE_FAILED_INIT; break; }
        if (!running) break;
        if (g_abort.load(std::memory_order_acquire)) { cancelled = true; break; }
        curl_multi_poll(g_multi, nullptr, 0, 100, nullptr);
    }
    if (!cancelled && result == CURLE_OK) {
        int q;
        while (CURLMsg* m = curl_multi_info_read(g_multi, &q))
            if (m->msg == CURLMSG_DONE) result = m->data.result;
    }
    curl_multi_remove_handle(g_multi, h);
    g_lastCurlResult = result;
    if (cancelled) status = -1;
    else if (result != CURLE_OK) status = 0;
    else { long code = 0; curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code); status = (int)code; }
    return status;
}

void prepare(CURL* h, const char* url) {
    curl_easy_reset(h);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(h, CURLOPT_UPLOAD_BUFFERSIZE, 16384L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "TranslateNX-Live");
    memset(g_curlError, 0, sizeof(g_curlError));
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, g_curlError);
    if (g_verifyTls) {
        curl_easy_setopt(h, CURLOPT_CAINFO, kCaPath);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    }
}


std::string urlEncode(const std::string& input) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3);
    for (unsigned char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

int getMyMemory(const std::string& text, std::string& translated) {
    std::string lang = g_set.targetLang.empty() ? "en" : g_set.targetLang;
    std::string url = "https://api.mymemory.translated.net/get?q=" + urlEncode(text) +
                      "&langpair=ja%7C" + urlEncode(lang);
    prepare(g_easy, url.c_str());
    const int status = perform(g_easy, nullptr, nullptr);
    if (status < 200 || status >= 300) return status;

    cJSON* root = cJSON_Parse(g_resp);
    if (!root) return status;
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "responseData");
    const cJSON* value = data ? cJSON_GetObjectItemCaseSensitive(data, "translatedText") : nullptr;
    if (cJSON_IsString(value) && value->valuestring) translated = value->valuestring;
    cJSON_Delete(root);
    return status;
}

std::string curlFailureText() {
    if (g_curlError[0]) return g_curlError;
    return curl_easy_strerror(g_lastCurlResult);
}

int postJson(const char* url, const std::string& keyHeader, const std::string& body) {
    prepare(g_easy, url);
    curl_easy_setopt(g_easy, CURLOPT_POST, 1L);
    curl_easy_setopt(g_easy, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(g_easy, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!keyHeader.empty()) h = curl_slist_append(h, keyHeader.c_str());
    const int st = perform(g_easy, h, nullptr);
    curl_slist_free_all(h);
    return st;
}

int postVision() {
    static const char* kPre  = "{\"requests\":[{\"image\":{\"content\":\"";
    static const char* kPost = "\"},\"features\":[{\"type\":\"TEXT_DETECTION\"}],"
                               "\"imageContext\":{\"languageHints\":[\"ja\"]}}]}";
    B64Stream stream(kPre, g_jpeg, g_jpegLen, kPost);   // streamed: no copy of the image is built
    prepare(g_easy, "https://vision.googleapis.com/v1/images:annotate"
                    "?fields=responses(error,textAnnotations(description,boundingPoly))");
    curl_easy_setopt(g_easy, CURLOPT_POST, 1L);
    curl_easy_setopt(g_easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)stream.totalSize());
    curl_easy_setopt(g_easy, CURLOPT_READFUNCTION, readCb);
    curl_easy_setopt(g_easy, CURLOPT_READDATA, &stream);
    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
    h = curl_slist_append(h, "Expect:");
    h = curl_slist_append(h, (std::string("X-Goog-Api-Key: ") + g_set.visionKey).c_str());
    const int st = perform(g_easy, h, nullptr);
    curl_slist_free_all(h);
    return st;
}

int postOcrSpace() {
    prepare(g_easy, "https://api.ocr.space/parse/image");
    curl_easy_setopt(g_easy, CURLOPT_TIMEOUT, 30L);
    MimeSrc src{g_jpeg, g_jpegLen, 0};
    curl_mime* mime = curl_mime_init(g_easy);
    curl_mimepart* p = curl_mime_addpart(mime);
    curl_mime_name(p, "file");
    curl_mime_filename(p, "s.jpg");
    curl_mime_type(p, "image/jpeg");
    curl_mime_data_cb(p, (curl_off_t)g_jpegLen, mimeRead, mimeSeek, nullptr, &src);
    const char* f[][2] = {{"language", "jpn"}, {"OCREngine", "1"}, {"scale", "true"},
                          {"isTable", "false"}, {"isOverlayRequired", "true"}};
    for (auto& kv : f) {
        curl_mimepart* e = curl_mime_addpart(mime);
        curl_mime_name(e, kv[0]);
        curl_mime_data(e, kv[1], CURL_ZERO_TERMINATED);
    }
    curl_slist* h = curl_slist_append(nullptr, (std::string("apikey: ") +
                                                (g_set.ocrSpaceKey.empty() ? "helloworld" : g_set.ocrSpaceKey)).c_str());
    const int st = perform(g_easy, h, mime);
    curl_slist_free_all(h);
    curl_mime_free(mime);
    return st;
}

// ─── capture + thumbnail (no SD card involved) ────────────────────────────
struct JErr { jpeg_error_mgr pub; jmp_buf jb; };
void jErrExit(j_common_ptr c) { longjmp(reinterpret_cast<JErr*>(c->err)->jb, 1); }

bool captureFrame() {
    u64 sz = 0;
    // Only the game's own layer stack. The HUD layer is removed from this stack in tesla.hpp
    // (hudSkipDebugStack) so our own English text is not captured. No fallback to the default
    // stack on purpose: that one would contain the HUD.
    const Result rc = capsscCaptureJpegScreenShot(&sz, g_jpeg, kJpegCap,
                                                  ViLayerStack_ApplicationForDebug, 2000000000ULL);
    if (R_FAILED(rc) || sz == 0 || sz > kJpegCap) { g_jpegLen = 0; return false; }
    g_jpegLen = (size_t)sz;
    return true;
}

bool makeThumb(uint8_t* out /* kThumbW*kThumbH */) {
    jpeg_decompress_struct cinfo;
    JErr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jErrExit;
    if (setjmp(jerr.jb)) { jpeg_destroy_decompress(&cinfo); return false; }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, g_jpeg, (unsigned long)g_jpegLen);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) { jpeg_destroy_decompress(&cinfo); return false; }
    cinfo.out_color_space = JCS_GRAYSCALE;
    cinfo.scale_num = 1; cinfo.scale_denom = 8;      // 1280x720 -> 160x90, decoded at 1/8 size
    cinfo.dct_method = JDCT_FASTEST;
    jpeg_start_decompress(&cinfo);
    if (cinfo.output_width != (JDIMENSION)kThumbW || cinfo.output_height != (JDIMENSION)kThumbH ||
        cinfo.output_components != 1) {
        jpeg_abort_decompress(&cinfo); jpeg_destroy_decompress(&cinfo); return false;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = out + cinfo.output_scanline * kThumbW;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

void wipeFrame() { if (g_jpeg) memset(g_jpeg, 0, kJpegCap); g_jpegLen = 0; }

// ─── pipeline steps ───────────────────────────────────────────────────────
// 0 ok, 1 api error (backoff), 2 cancelled
bool retryableHttp(int status) {
    return status == 0 || status == 408 || status == 425 || (status >= 500 && status < 600);
}

// 0 = success, 1 = API error (retryable out-param), 2 = cancelled.
int runOcr(std::vector<Word>& words, std::string& err, bool& retryable) {
    words.clear();
    err.clear();
    retryable = true;
    if (g_set.useVision && g_set.visionKey.empty()) {
        err = "OCR: Google Vision API key is missing";
        retryable = false;
        return 1;
    }
    const int st = g_set.useVision ? postVision() : postOcrSpace();
    if (st == -1) return 2;
    if (st < 200 || st >= 300) {
        retryable = retryableHttp(st);
        if (st == 0) err = "OCR: network error - " + curlFailureText();
        else if (st == 401 || st == 403) err = "OCR: API key rejected or provider access denied (HTTP " + std::to_string(st) + ")";
        else err = "OCR: HTTP " + std::to_string(st);
        return 1;
    }
    const bool ok = g_set.useVision ? parseVisionWords(g_resp, words, err)
                                    : parseOcrSpaceWords(g_resp, words, err);
    if (!ok) { err = "OCR: " + err; retryable = true; return 1; }
    return 0;
}

int runTranslate(const std::vector<std::string>& texts, std::vector<std::string>& out, std::string& err, bool& retryable) {
    retryable = true;
    if (g_set.translateProvider == TranslateProvider::DeepL && g_set.deeplKey.empty()) {
        err = "Translate: DeepL API key is missing"; retryable = false; return 1;
    }
    if (g_set.translateProvider == TranslateProvider::GoogleCloud && g_set.googleKey.empty()) {
        err = "Translate: Google Cloud API key is missing"; retryable = false; return 1;
    }
    if (g_set.translateProvider == TranslateProvider::MyMemory) {
        // MyMemory accepts one short query per request; keeping each OCR block separate
        // preserves the HUD's one-translation-per-patch layout.
        // Preserve completed lines across retries so a transient failure does not
        // resend successful MyMemory requests and consume quota unnecessarily.
        for (size_t i = out.size(); i < texts.size(); ++i) {
            if (g_abort.load(std::memory_order_acquire)) return 2;
            std::string translated;
            const int st = getMyMemory(texts[i], translated);
            if (st == -1) return 2;
            if (st < 200 || st >= 300) {
                retryable = retryableHttp(st);
                err = st == 0 ? "Translate: network error - " + curlFailureText()
                              : "Translate: MyMemory HTTP " + std::to_string(st);
                return 1;
            }
            if (translated.empty()) { err = "Translate: MyMemory returned empty text"; return 1; }
            out.push_back(std::move(translated));
        }
        return 0;
    }

    int st;
    if (g_set.translateProvider == TranslateProvider::DeepL) {
        const bool freePlan = g_set.deeplKey.size() >= 3 &&
            g_set.deeplKey.compare(g_set.deeplKey.size() - 3, 3, ":fx") == 0;
        st = postJson(freePlan ? "https://api-free.deepl.com/v2/translate" : "https://api.deepl.com/v2/translate",
                      "Authorization: DeepL-Auth-Key " + g_set.deeplKey,
                      buildDeepLBody(texts, g_set.targetLang));
    } else {
        st = postJson("https://translation.googleapis.com/language/translate/v2",
                      "X-Goog-Api-Key: " + g_set.googleKey,
                      buildGoogleTranslateBody(texts, g_set.targetLang));
    }
    if (st == -1) return 2;
    if (st < 200 || st >= 300) {
        retryable = retryableHttp(st);
        if (st == 0) err = "Translate: network error - " + curlFailureText();
        else if (st == 401 || st == 403) err = "Translate: API key rejected or provider access denied (HTTP " + std::to_string(st) + ")";
        else err = "Translate: HTTP " + std::to_string(st);
        return 1;
    }
    out.clear();
    const bool ok = g_set.translateProvider == TranslateProvider::DeepL
        ? parseDeepL(g_resp, out) : parseGoogleTranslate(g_resp, out);
    if (!ok || out.size() != texts.size()) { err = "Translate: bad answer"; retryable = true; return 1; }
    for (auto& t : out) if (t.empty()) { err = "Translate: empty text"; return 1; }
    return 0;
}

// ─── worker ───────────────────────────────────────────────────────────────
void worker(void*) {
    static uint8_t thumbNow[kThumbW * kThumbH], thumbPrev[kThumbW * kThumbH], thumbOcr[kThumbW * kThumbH];
    bool havePrev = false, haveOcr = false;
    bool ocrKeyAccepted = false, translationKeyAccepted = false;
    int unstable = 0;

    if (R_FAILED(capsscInitialize())) { setStatus("Capture service unavailable"); sleepFor(~0ULL >> 1); }
    else if (!netUp())               { setStatus("Network init failed");           sleepFor(~0ULL >> 1); }
    else {
        setStatus(g_verifyTls ? "Watching" : "Watching (TLS not verified)");
        u64 wait = 500000000ULL;
        while (sleepFor(wait)) {
            wait = kPollNs;

            if (!captureFrame() || !makeThumb(thumbNow)) { wipeFrame(); setStatus("Waiting for game picture"); continue; }

            const float sinceOcr  = haveOcr  ? thumbDiffPercent(thumbNow, thumbOcr)  : 100.0f;
            const float sincePrev = havePrev ? thumbDiffPercent(thumbNow, thumbPrev) : 100.0f;
            memcpy(thumbPrev, thumbNow, sizeof(thumbNow)); havePrev = true;

            if (haveOcr && sinceOcr < 1.5f) { wipeFrame(); continue; }          // nothing new on screen
            if (haveOcr && sinceOcr > 15.0f) setPatches({});                    // new scene: drop stale text
            if (sincePrev > 3.0f && unstable < 3) { ++unstable; wipeFrame(); wait = kRetryNs; continue; }  // still moving
            unstable = 0;

            setStatus("Reading screen...");
            std::vector<Word> words; std::string err;
            int rc = 1;
            for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
                bool retryable = true;
                rc = runOcr(words, err, retryable);
                if (rc != 1 || !retryable || attempt == kMaxRetries) break;
                char retryStatus[64];
                snprintf(retryStatus, sizeof(retryStatus), "OCR failed; retry %d/%d", attempt + 1, kMaxRetries);
                setStatus("%s", retryStatus);
                if (!sleepFor(kRetryDelayNs[attempt])) { rc = 2; break; }
            }
            if (rc == 2) break;
            if (rc == 1) {
                wipeFrame();
                setStatus("%s", err.c_str());
                memcpy(thumbOcr, thumbNow, sizeof(thumbNow)); haveOcr = true;
                continue;
            }
            ocrKeyAccepted = g_set.useVision ? !g_set.visionKey.empty() : !g_set.ocrSpaceKey.empty();
            wipeFrame();    // the picture is no longer needed once the OCR answer is in

            std::vector<Block> blocks = groupBlocks(std::move(words), kMaxPatches, kTextMax - 1);
            if (blocks.empty()) {
                setPatches({});
                setStatus(ocrKeyAccepted ? "No Japanese text | OCR key OK" : "No Japanese text");
                memcpy(thumbOcr, thumbNow, sizeof(thumbNow)); haveOcr = true;
                continue;
            }

            std::vector<std::string> missing; std::vector<uint64_t> missingHash;
            for (auto& b : blocks) {
                const uint64_t h = hashText(b.text);
                if (!cacheFind(h)) {
                    bool dup = false; for (auto m : missingHash) if (m == h) dup = true;
                    if (!dup) { missing.push_back(b.text); missingHash.push_back(h); }
                }
            }
            if (!missing.empty()) {
                setStatus("Translating...");
                std::vector<std::string> tr;
                rc = 1;
                for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
                    bool retryable = true;
                    rc = runTranslate(missing, tr, err, retryable);
                    if (rc != 1 || !retryable || attempt == kMaxRetries) break;
                    char retryStatus[64];
                    snprintf(retryStatus, sizeof(retryStatus), "Translation failed; retry %d/%d", attempt + 1, kMaxRetries);
                    setStatus("%s", retryStatus);
                    if (!sleepFor(kRetryDelayNs[attempt])) { rc = 2; break; }
                }
                if (rc == 2) break;
                if (rc == 1) {
                    setStatus("%s", err.c_str());
                    memcpy(thumbOcr, thumbNow, sizeof(thumbNow)); haveOcr = true;
                    continue;
                }
                translationKeyAccepted = g_set.translateProvider == TranslateProvider::DeepL
                    ? !g_set.deeplKey.empty()
                    : (g_set.translateProvider == TranslateProvider::GoogleCloud && !g_set.googleKey.empty());
                for (size_t i = 0; i < tr.size(); ++i) cacheAdd(missingHash[i], tr[i]);
            }

            std::vector<Patch> patches;
            for (auto& b : blocks) {
                const CacheEntry* c = cacheFind(hashText(b.text));
                if (!c) continue;
                Patch p;
                const int pad = 3;
                int x0 = b.x0 / 2 - pad, y0 = b.y0 / 2 - pad, x1 = b.x1 / 2 + pad, y1 = b.y1 / 2 + pad;
                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 > kLayerW) x1 = kLayerW;
                if (y1 > 360) y1 = 360;
                p.x = (int16_t)x0; p.y = (int16_t)y0; p.w = (int16_t)(x1 - x0); p.h = (int16_t)(y1 - y0);
                p.lum = (uint8_t)ringBrightness(thumbNow, b.x0, b.y0, b.x1, b.y1);
                snprintf(p.text, sizeof(p.text), "%s", c->text);
                patches.push_back(p);
            }
            if (g_abort.load(std::memory_order_acquire)) break;   // never publish after a close request
            setPatches(patches);
            std::string liveStatus = "Live";
            if (ocrKeyAccepted) liveStatus += " | OCR OK";
            if (translationKeyAccepted) liveStatus += " | translation key OK";
            setStatus("%s", liveStatus.c_str());
            memcpy(thumbOcr, thumbNow, sizeof(thumbNow)); haveOcr = true;
        }
    }

    wipeFrame();
    netDown();
    capsscExit();
}

} // namespace

// ─── public API ───────────────────────────────────────────────────────────
bool start(const Settings& s) {
    if (g_running) return true;
    if (!g_mtxInit) { mutexInit(&g_mtx); g_mtxInit = true; }
    g_set = s;
    g_abort.store(false, std::memory_order_release);
    g_jpeg = static_cast<uint8_t*>(calloc(1, kJpegCap));
    g_resp = static_cast<char*>(calloc(1, kRespCap));
    for (auto& e : g_cache) e = CacheEntry();
    g_snap = Snapshot();
    if (!g_jpeg || !g_resp) { free(g_jpeg); free(g_resp); g_jpeg = nullptr; g_resp = nullptr; return false; }
    if (R_FAILED(threadCreate(&g_thread, worker, nullptr, nullptr, kStackSize, 0x2C, -2)) ||
        R_FAILED(threadStart(&g_thread))) {
        free(g_jpeg); free(g_resp); g_jpeg = nullptr; g_resp = nullptr;
        return false;
    }
    g_running = true;
    return true;
}

void stop() {
    if (!g_running) return;
    g_abort.store(true, std::memory_order_release);   // cancels capture waits, curl transfers and sleeps
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    if (g_jpeg) { memset(g_jpeg, 0, kJpegCap); free(g_jpeg); g_jpeg = nullptr; }
    if (g_resp) { memset(g_resp, 0, kRespCap); free(g_resp); g_resp = nullptr; }
    g_jpegLen = g_respLen = 0;
    for (auto& e : g_cache) e = CacheEntry();
    g_snap = Snapshot();
    g_running = false;
}

bool running() { return g_running; }

bool copySnapshot(Snapshot& out, uint32_t& lastVersion) {
    if (!g_mtxInit) return false;
    mutexLock(&g_mtx);
    const bool changed = (g_snap.version != lastVersion);
    if (changed) { out = g_snap; lastVersion = g_snap.version; }
    mutexUnlock(&g_mtx);
    return changed;
}

} // namespace Live

#endif // TNX_HUD
