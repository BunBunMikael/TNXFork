#include "live_logic.hpp"
#include <algorithm>
#include <cJSON.h>
#include <cstdlib>
#include <cstring>

namespace LiveLogic {

// ─── UTF-8 ────────────────────────────────────────────────────────────────
static bool nextCp(const std::string& s, size_t& i, uint32_t& cp) {
    const unsigned char c = (unsigned char)s[i];
    size_t n = 1;
    if (c < 0x80) { cp = c; }
    else if ((c >> 5) == 6)  { cp = c & 0x1F; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
    else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
    else { i++; return false; }
    if (i + n > s.size()) { i = s.size(); return false; }
    for (size_t k = 1; k < n; ++k) {
        const unsigned char cc = (unsigned char)s[i + k];
        if ((cc >> 6) != 2) { i++; return false; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += n;
    return true;
}

bool hasJapanese(const std::string& s) {
    size_t i = 0; uint32_t cp;
    while (i < s.size()) {
        if (!nextCp(s, i, cp)) continue;
        if ((cp >= 0x3000 && cp <= 0x30FF) ||   // CJK punctuation, hiragana, katakana
            (cp >= 0x3400 && cp <= 0x4DBF) ||   // kanji ext A
            (cp >= 0x4E00 && cp <= 0x9FFF) ||   // kanji
            (cp >= 0xFF01 && cp <= 0xFF9F))     // full-width forms, half-width kana
            return true;
    }
    return false;
}

std::string truncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    while (n > 0 && ((unsigned char)s[n] >> 6) == 2) --n;   // do not cut inside a character
    return s.substr(0, n);
}

// ─── Grouping ─────────────────────────────────────────────────────────────
namespace {
struct Line { std::string text; int x0, y0, x1, y1; std::vector<Word> words; };

int hOf(const Line& l) { return l.y1 - l.y0; }

void finishLine(Line& l) {
    std::sort(l.words.begin(), l.words.end(), [](const Word& a, const Word& b) { return a.x0 < b.x0; });
    l.text.clear();
    for (auto& w : l.words) l.text += w.text;   // Japanese is written without spaces
}
}

std::vector<Block> groupBlocks(std::vector<Word> words, size_t maxBlocks, size_t maxBytes) {
    // 1. Japanese only (English / digits-only words are dropped on purpose)
    words.erase(std::remove_if(words.begin(), words.end(), [](const Word& w) {
        return w.x1 <= w.x0 || w.y1 <= w.y0 || !hasJapanese(w.text);
    }), words.end());

    // 2. words -> lines
    std::vector<Line> lines;
    for (const Word& w : words) {
        const int wh = w.y1 - w.y0;
        bool merged = false;
        for (Line& l : lines) {
            const int overlap = std::min(l.y1, w.y1) - std::max(l.y0, w.y0);
            const int minH = std::min(hOf(l), wh);
            const int maxH = std::max(hOf(l), wh);
            if (overlap * 2 > minH && w.x0 - l.x1 <= maxH * 3 / 2 && l.x0 - w.x1 <= maxH * 3 / 2) {
                l.words.push_back(w);
                l.x0 = std::min(l.x0, w.x0); l.x1 = std::max(l.x1, w.x1);
                l.y0 = std::min(l.y0, w.y0); l.y1 = std::max(l.y1, w.y1);
                merged = true; break;
            }
        }
        if (!merged) lines.push_back(Line{"", w.x0, w.y0, w.x1, w.y1, {w}});
    }
    for (Line& l : lines) finishLine(l);
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) {
        return a.y0 != b.y0 ? a.y0 < b.y0 : a.x0 < b.x0;
    });

    // 3. lines -> paragraphs
    std::vector<Block> blocks;
    std::vector<int> lastH;     // height of the last line merged into each block
    std::vector<int> lastBot;   // bottom of the last line merged into each block
    for (const Line& l : lines) {
        bool merged = false;
        for (size_t i = 0; i < blocks.size(); ++i) {
            Block& b = blocks[i];
            const int gap   = l.y0 - lastBot[i];
            const int h     = std::max(lastH[i], hOf(l));
            const int ratio = hOf(l) * 10 / std::max(1, lastH[i]);
            const int ox    = std::min(b.x1, l.x1) - std::max(b.x0, l.x0);
            const int minW  = std::min(b.x1 - b.x0, l.x1 - l.x0);
            if (gap >= -h / 3 && gap <= h * 3 / 5 && ratio >= 6 && ratio <= 16 && ox * 10 > minW * 3) {
                b.text += l.text;
                b.x0 = std::min(b.x0, l.x0); b.x1 = std::max(b.x1, l.x1);
                b.y0 = std::min(b.y0, l.y0); b.y1 = std::max(b.y1, l.y1);
                lastH[i] = hOf(l); lastBot[i] = l.y1;
                merged = true; break;
            }
        }
        if (!merged) {
            blocks.push_back(Block{l.text, l.x0, l.y0, l.x1, l.y1});
            lastH.push_back(hOf(l)); lastBot.push_back(l.y1);
        }
    }

    std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
        return a.y0 != b.y0 ? a.y0 < b.y0 : a.x0 < b.x0;
    });
    if (blocks.size() > maxBlocks) blocks.resize(maxBlocks);
    for (Block& b : blocks) b.text = truncateUtf8(b.text, maxBytes);
    return blocks;
}

// ─── JSON helpers ─────────────────────────────────────────────────────────
static int numOf(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : 0;   // Vision omits 0 coordinates
}

bool parseVisionWords(const char* json, std::vector<Word>& out, std::string& err) {
    cJSON* root = cJSON_Parse(json);
    if (!root) { err = "bad JSON"; return false; }
    bool ok = false;
    const cJSON* responses = cJSON_GetObjectItemCaseSensitive(root, "responses");
    const cJSON* r0 = cJSON_IsArray(responses) ? cJSON_GetArrayItem(responses, 0) : nullptr;
    if (r0) {
        const cJSON* e = cJSON_GetObjectItemCaseSensitive(r0, "error");
        if (e) {
            const cJSON* m = cJSON_GetObjectItemCaseSensitive(e, "message");
            err = cJSON_IsString(m) ? m->valuestring : "Vision error";
        } else {
            ok = true;
            const cJSON* ann = cJSON_GetObjectItemCaseSensitive(r0, "textAnnotations");
            const int n = cJSON_IsArray(ann) ? cJSON_GetArraySize(ann) : 0;
            for (int i = 1; i < n; ++i) {   // [0] is the whole text, [1..] are the pieces
                const cJSON* a = cJSON_GetArrayItem(ann, i);
                const cJSON* d = cJSON_GetObjectItemCaseSensitive(a, "description");
                const cJSON* poly = cJSON_GetObjectItemCaseSensitive(a, "boundingPoly");
                const cJSON* vs = poly ? cJSON_GetObjectItemCaseSensitive(poly, "vertices") : nullptr;
                if (!cJSON_IsString(d) || !cJSON_IsArray(vs) || cJSON_GetArraySize(vs) < 3) continue;
                Word w; w.text = d->valuestring;
                w.x0 = w.y0 = 1 << 30; w.x1 = w.y1 = -1;
                const int vn = cJSON_GetArraySize(vs);
                for (int k = 0; k < vn; ++k) {
                    const cJSON* v = cJSON_GetArrayItem(vs, k);
                    const int x = numOf(v, "x"), y = numOf(v, "y");
                    w.x0 = std::min(w.x0, x); w.x1 = std::max(w.x1, x);
                    w.y0 = std::min(w.y0, y); w.y1 = std::max(w.y1, y);
                }
                out.push_back(std::move(w));
            }
        }
    } else err = "no response";
    cJSON_Delete(root);
    return ok;
}

bool parseOcrSpaceWords(const char* json, std::vector<Word>& out, std::string& err) {
    cJSON* root = cJSON_Parse(json);
    if (!root) { err = "bad JSON"; return false; }
    bool ok = false;
    const cJSON* isErr = cJSON_GetObjectItemCaseSensitive(root, "IsErroredOnProcessing");
    if (cJSON_IsTrue(isErr)) {
        const cJSON* m = cJSON_GetObjectItemCaseSensitive(root, "ErrorMessage");
        const cJSON* m0 = cJSON_IsArray(m) ? cJSON_GetArrayItem(m, 0) : m;
        err = cJSON_IsString(m0) ? m0->valuestring : "OCR.space error";
    } else {
        ok = true;
        const cJSON* pr = cJSON_GetObjectItemCaseSensitive(root, "ParsedResults");
        const cJSON* p0 = cJSON_IsArray(pr) ? cJSON_GetArrayItem(pr, 0) : nullptr;
        const cJSON* ov = p0 ? cJSON_GetObjectItemCaseSensitive(p0, "TextOverlay") : nullptr;
        const cJSON* lines = ov ? cJSON_GetObjectItemCaseSensitive(ov, "Lines") : nullptr;
        const int ln = cJSON_IsArray(lines) ? cJSON_GetArraySize(lines) : 0;
        for (int i = 0; i < ln; ++i) {
            const cJSON* words = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(lines, i), "Words");
            const int wn = cJSON_IsArray(words) ? cJSON_GetArraySize(words) : 0;
            for (int j = 0; j < wn; ++j) {
                const cJSON* w = cJSON_GetArrayItem(words, j);
                const cJSON* t = cJSON_GetObjectItemCaseSensitive(w, "WordText");
                if (!cJSON_IsString(t)) continue;
                Word o; o.text = t->valuestring;
                o.x0 = numOf(w, "Left");  o.y0 = numOf(w, "Top");
                o.x1 = o.x0 + numOf(w, "Width"); o.y1 = o.y0 + numOf(w, "Height");
                out.push_back(std::move(o));
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

// ─── Translation bodies / responses ───────────────────────────────────────
static std::string printAndFree(cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    std::string r = s ? s : "";
    if (s) free(s);
    cJSON_Delete(root);
    return r;
}

std::string buildDeepLBody(const std::vector<std::string>& texts, const std::string& targetLang) {
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_CreateArray();
    for (auto& t : texts) cJSON_AddItemToArray(arr, cJSON_CreateString(t.c_str()));
    cJSON_AddItemToObject(root, "text", arr);
    cJSON_AddStringToObject(root, "source_lang", "JA");
    std::string target = targetLang.empty() ? "EN" : targetLang;
    std::transform(target.begin(), target.end(), target.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    if (target == "EN") target = "EN-US";
    cJSON_AddStringToObject(root, "target_lang", target.c_str());
    return printAndFree(root);
}

std::string buildGoogleTranslateBody(const std::vector<std::string>& texts, const std::string& targetLang) {
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_CreateArray();
    for (auto& t : texts) cJSON_AddItemToArray(arr, cJSON_CreateString(t.c_str()));
    cJSON_AddItemToObject(root, "q", arr);
    cJSON_AddStringToObject(root, "source", "ja");
    std::string target = targetLang.empty() ? "en" : targetLang;
    std::transform(target.begin(), target.end(), target.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    cJSON_AddStringToObject(root, "target", target.c_str());
    cJSON_AddStringToObject(root, "format", "text");
    return printAndFree(root);
}

static bool collect(const cJSON* arr, const char* key, std::vector<std::string>& out) {
    if (!cJSON_IsArray(arr)) return false;
    const int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; ++i) {
        const cJSON* t = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(arr, i), key);
        out.push_back(cJSON_IsString(t) ? t->valuestring : "");
    }
    return n > 0;
}

bool parseDeepL(const char* json, std::vector<std::string>& out) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    const bool ok = collect(cJSON_GetObjectItemCaseSensitive(root, "translations"), "text", out);
    cJSON_Delete(root);
    return ok;
}

bool parseGoogleTranslate(const char* json, std::vector<std::string>& out) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const bool ok = data && collect(cJSON_GetObjectItemCaseSensitive(data, "translations"), "translatedText", out);
    cJSON_Delete(root);
    return ok;
}

// ─── Base64 stream ────────────────────────────────────────────────────────
B64Stream::B64Stream(const char* prefix, const uint8_t* data, size_t len, const char* suffix)
    : m_prefix(prefix), m_prefixLen(strlen(prefix)), m_data(data), m_len(len),
      m_suffix(suffix), m_suffixLen(strlen(suffix)) {
    m_total = m_prefixLen + ((len + 2) / 3) * 4 + m_suffixLen;
}

size_t B64Stream::read(char* out, size_t cap) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = 0;
    while (n < cap) {
        if (m_prefixPos < m_prefixLen) { out[n++] = m_prefix[m_prefixPos++]; continue; }
        if (m_pendPos < m_pendLen)     { out[n++] = m_pend[m_pendPos++];     continue; }
        if (m_dataPos < m_len) {
            const size_t left = m_len - m_dataPos;
            uint32_t v = (uint32_t)m_data[m_dataPos] << 16;
            if (left > 1) v |= (uint32_t)m_data[m_dataPos + 1] << 8;
            if (left > 2) v |= m_data[m_dataPos + 2];
            m_pend[0] = T[(v >> 18) & 63];
            m_pend[1] = T[(v >> 12) & 63];
            m_pend[2] = left > 1 ? T[(v >> 6) & 63] : '=';
            m_pend[3] = left > 2 ? T[v & 63] : '=';
            m_pendLen = 4; m_pendPos = 0;
            m_dataPos += left > 3 ? 3 : left;
            continue;
        }
        if (m_suffixPos < m_suffixLen) { out[n++] = m_suffix[m_suffixPos++]; continue; }
        break;
    }
    return n;
}

// ─── Thumbnails ───────────────────────────────────────────────────────────
float thumbDiffPercent(const uint8_t* a, const uint8_t* b, int threshold) {
    int diff = 0;
    for (int i = 0; i < kThumbW * kThumbH; ++i)
        if (std::abs((int)a[i] - (int)b[i]) > threshold) ++diff;
    return diff * 100.0f / (kThumbW * kThumbH);
}

int ringBrightness(const uint8_t* t, int x0, int y0, int x1, int y1) {
    // capture px (1280x720) -> thumb px (/8), ring one thumb pixel outside the box
    const int tx0 = std::max(0, x0 / 8 - 1), ty0 = std::max(0, y0 / 8 - 1);
    const int tx1 = std::min(kThumbW - 1, x1 / 8 + 1), ty1 = std::min(kThumbH - 1, y1 / 8 + 1);
    long sum = 0; int cnt = 0;
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x) {
            const bool edge = (x == tx0 || x == tx1 || y == ty0 || y == ty1);
            if (edge) { sum += t[y * kThumbW + x]; ++cnt; }
        }
    return cnt ? (int)(sum / cnt) : 0;
}

uint64_t hashText(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

} // namespace LiveLogic
