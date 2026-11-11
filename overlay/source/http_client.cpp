#include "http_client.hpp"
#include <curl/curl.h>
#include <cstring>
#include <switch.h>
#include <sys/stat.h>

static constexpr const char* kCaPath = "sdmc:/config/translate/cacert_live.pem";

// ─── libcurl response callback ─────────────────────────────────────────────
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    const size_t bytes = size * nmemb;
    str->append(ptr, bytes);
    return bytes;
}

static void configureCurl(CURL* curl, const std::string& url,
                          const std::vector<std::string>& headers,
                          std::string& responseBody,
                          curl_slist*& headerList,
                          char* errorBuffer) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TranslateNX");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    // Use the same optional CA bundle as the Live HUD. When present, verify the
    // certificate chain and hostname; otherwise retain the existing compatibility
    // behavior for installations that have not added a CA bundle yet.
    struct stat st;
    if (stat(kCaPath, &st) == 0) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, kCaPath);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    for (const auto& header : headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }
    if (headerList) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
}

static void performWithRetry(CURL* curl, HttpResponse& response, char* errorBuffer) {
    CURLcode result = CURLE_FAILED_INIT;
    for (int attempt = 0; attempt < 3; ++attempt) {
        response.body.clear();
        errorBuffer[0] = '\0';
        result = curl_easy_perform(curl);
        if (result == CURLE_OK) break;
        if (attempt < 2) svcSleepThread(500000000ull);
    }

    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    } else {
        response.errorStr = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);
    }
}

namespace HttpClient {

void init() {
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void cleanup() {
    curl_global_cleanup();
    socketExit();
}

HttpResponse post(const std::string& url, const std::string& body,
                  const std::vector<std::string>& headers) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.errorStr = "libcurl initialization failed"; return response; }

    curl_slist* headerList = nullptr;
    char errorBuffer[CURL_ERROR_SIZE] = {0};
    configureCurl(curl, url, headers, response.body, headerList, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    performWithRetry(curl, response, errorBuffer);

    if (headerList) curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse get(const std::string& url, const std::vector<std::string>& headers) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.errorStr = "libcurl initialization failed"; return response; }

    curl_slist* headerList = nullptr;
    char errorBuffer[CURL_ERROR_SIZE] = {0};
    configureCurl(curl, url, headers, response.body, headerList, errorBuffer);
    performWithRetry(curl, response, errorBuffer);

    if (headerList) curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse postMultipart(const std::string& url, const std::vector<uint8_t>& fileData,
                           const std::string& fieldName, const std::string& filename,
                           const std::vector<std::pair<std::string,std::string>>& extraFields,
                           const std::vector<std::string>& headers) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.errorStr = "libcurl initialization failed"; return response; }

    curl_mime* form = curl_mime_init(curl);
    if (!form) {
        curl_easy_cleanup(curl);
        response.errorStr = "libcurl MIME initialization failed";
        return response;
    }

    curl_mimepart* part = curl_mime_addpart(form);
    curl_mime_name(part, fieldName.c_str());
    curl_mime_filename(part, filename.c_str());
    curl_mime_data(part, reinterpret_cast<const char*>(fileData.data()), fileData.size());
    curl_mime_type(part, "image/jpeg");

    for (const auto& field : extraFields) {
        curl_mimepart* extra = curl_mime_addpart(form);
        curl_mime_name(extra, field.first.c_str());
        curl_mime_data(extra, field.second.c_str(), CURL_ZERO_TERMINATED);
    }

    curl_slist* headerList = nullptr;
    char errorBuffer[CURL_ERROR_SIZE] = {0};
    configureCurl(curl, url, headers, response.body, headerList, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    performWithRetry(curl, response, errorBuffer);

    if (headerList) curl_slist_free_all(headerList);
    curl_mime_free(form);
    curl_easy_cleanup(curl);
    return response;
}

} // namespace HttpClient
