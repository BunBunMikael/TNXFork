#pragma once
#include <string>
#include <vector>
#include <functional>

struct HttpResponse {
    long        statusCode = 0;
    std::string body;
    std::string errorStr;
    bool        ok() const { return statusCode >= 200 && statusCode < 300; }
};

namespace HttpClient {
    // İlk çağrıda curl_global_init yapar (uygulama başında bir kez çağır)
    void init();
    void cleanup();

    // Senkron POST — body: JSON string veya base64 vb.
    // headers: "Key: Value" formatında ek başlıklar
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::vector<std::string>& headers = {});

    // Senkron GET
    HttpResponse get(const std::string& url,
                     const std::vector<std::string>& headers = {});

    // Multipart form POST (OCR.space için)
    HttpResponse postMultipart(const std::string& url,
                               const std::vector<uint8_t>& fileData,
                               const std::string& fieldName,
                               const std::string& filename,
                               const std::vector<std::pair<std::string,std::string>>& extraFields = {},
                               const std::vector<std::string>& headers = {});
}
