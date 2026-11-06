#include "http_client.hpp"
#include <curl/curl.h>
#include <cstring>
#include <switch.h>

#include <cJSON.h>
#include <map>
#include <mutex>

static std::map<std::string, std::string> g_dnsCache;
static std::mutex g_dnsMutex;

// ─── Yardımcı: libcurl veri callback ──────────────────────────────────────
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

// ─── Dinamik DNS Çözümleyici (Google DNS JSON API üzerinden) ──────────────
static std::string resolveDomainToIp(const std::string& domain) {
    {
        std::lock_guard<std::mutex> lock(g_dnsMutex);
        if (g_dnsCache.count(domain)) {
            return g_dnsCache[domain];
        }
    }

    std::string url = "https://8.8.8.8/resolve?name=" + domain + "&type=A";
    std::string responseBody;
    
    CURLcode res = CURLE_FAILED_INIT;
    for (int retry = 0; retry < 3; retry++) {
        responseBody.clear();
        CURL* curl = curl_easy_init();
        if (!curl) continue;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && !responseBody.empty()) {
            break; // Başarılı
        }
        
        // 1 saniye bekle ve tekrar dene
        svcSleepThread(1000000000ull);
    }

    if (res != CURLE_OK || responseBody.empty()) {
        return "";
    }

    std::string foundIp = "";
    cJSON* root = cJSON_Parse(responseBody.c_str());
    if (root) {
        cJSON* answer = cJSON_GetObjectItem(root, "Answer");
        if (cJSON_IsArray(answer)) {
            int count = cJSON_GetArraySize(answer);
            for (int i = 0; i < count; i++) {
                cJSON* item = cJSON_GetArrayItem(answer, i);
                cJSON* type = cJSON_GetObjectItem(item, "type");
                cJSON* data = cJSON_GetObjectItem(item, "data");
                if (type && type->valueint == 1 && data && cJSON_IsString(data)) {
                    foundIp = data->valuestring;
                    break;
                }
            }
        }
        cJSON_Delete(root);
    }

    if (!foundIp.empty()) {
        std::lock_guard<std::mutex> lock(g_dnsMutex);
        g_dnsCache[domain] = foundIp;
    }
    
    return foundIp;
}

// ─── Ortak curl handle yapılandırması ─────────────────────────────────────
static void configureCurl(CURL* curl, const std::string& url,
                           const std::vector<std::string>& headers,
                           std::string& responseBody,
                           curl_slist*& headerList,
                           curl_slist*& resolveList) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);          // 15 sn timeout
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // SSL — Sertifika doğrulamasını kapatıyoruz
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // DNS bypass via Google DNS JSON API
    // Sadece HTTPS/HTTP adreslerinden domain'i ayıkla
    std::string domain = "";
    size_t start = url.find("://");
    if (start != std::string::npos) {
        start += 3;
        size_t end = url.find('/', start);
        if (end == std::string::npos) end = url.length();
        domain = url.substr(start, end - start);
    }
    
    if (!domain.empty() && domain != "8.8.8.8") {
        std::string ip = resolveDomainToIp(domain);
        if (!ip.empty()) {
            std::string resolveStr = domain + ":443:" + ip;
            resolveList = curl_slist_append(resolveList, resolveStr.c_str());
            curl_easy_setopt(curl, CURLOPT_RESOLVE, resolveList);
        }
    }

    for (const auto& h : headers) {
        headerList = curl_slist_append(headerList, h.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
}

namespace HttpClient {

void init() {
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_ALL);
}

void cleanup() {
    curl_global_cleanup();
    socketExit();
}

HttpResponse post(const std::string& url,
                  const std::string& body,
                  const std::vector<std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    curl_slist* headerList = nullptr;
    curl_slist* resolveList = nullptr;
    configureCurl(curl, url, headers, resp.body, headerList, resolveList);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());

    CURLcode res;
    int retries = 2;
    while (retries >= 0) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        if (retries > 0) svcSleepThread(500000000ull);
        retries--;
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    } else {
        resp.errorStr = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    if (resolveList) curl_slist_free_all(resolveList);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse get(const std::string& url,
                 const std::vector<std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    curl_slist* headerList = nullptr;
    curl_slist* resolveList = nullptr;
    configureCurl(curl, url, headers, resp.body, headerList, resolveList);

    CURLcode res;
    int retries = 2;
    while (retries >= 0) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        if (retries > 0) svcSleepThread(500000000ull);
        retries--;
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    } else {
        resp.errorStr = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    if (resolveList) curl_slist_free_all(resolveList);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse postMultipart(const std::string& url,
                            const std::vector<uint8_t>& fileData,
                            const std::string& fieldName,
                            const std::string& filename,
                            const std::vector<std::pair<std::string,std::string>>& extraFields,
                            const std::vector<std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    curl_mime* form = curl_mime_init(curl);

    // Dosya alanı
    curl_mimepart* part = curl_mime_addpart(form);
    curl_mime_name(part, fieldName.c_str());
    curl_mime_filename(part, filename.c_str());
    curl_mime_data(part, reinterpret_cast<const char*>(fileData.data()), fileData.size());
    curl_mime_type(part, "image/jpeg");

    // Ekstra text alanları
    for (const auto& [k, v] : extraFields) {
        curl_mimepart* ep = curl_mime_addpart(form);
        curl_mime_name(ep, k.c_str());
        curl_mime_data(ep, v.c_str(), CURL_ZERO_TERMINATED);
    }

    curl_slist* headerList = nullptr;
    curl_slist* resolveList = nullptr;
    configureCurl(curl, url, headers, resp.body, headerList, resolveList);
    
    // configureCurl 15L timeout ayarlıyor, multipart için 20L yapıyoruz
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    CURLcode res;
    int retries = 2;
    while (retries >= 0) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        if (retries > 0) svcSleepThread(500000000ull);
        retries--;
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    } else {
        resp.errorStr = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    if (resolveList) curl_slist_free_all(resolveList);
    curl_mime_free(form);
    curl_easy_cleanup(curl);
    return resp;
}

} // namespace HttpClient
