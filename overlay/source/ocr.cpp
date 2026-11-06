#include "ocr.hpp"
#include "http_client.hpp"
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <cJSON.h>

// ─── Minimal JSON string ayıklaması (Premium Google Vision için) ────────────────
static std::string jsonExtract(const std::string& json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

// OCR.space yanıtından Satırları ve Bounding Box'ları çek
static std::vector<OcrWord> ocrSpaceExtractLines(const std::string& jsonBody) {
    std::vector<OcrWord> words;
    cJSON* root = cJSON_Parse(jsonBody.c_str());
    if (!root) return words;

    cJSON* parsedResults = cJSON_GetObjectItem(root, "ParsedResults");
    if (cJSON_IsArray(parsedResults) && cJSON_GetArraySize(parsedResults) > 0) {
        cJSON* result0 = cJSON_GetArrayItem(parsedResults, 0);
        cJSON* textOverlay = cJSON_GetObjectItem(result0, "TextOverlay");
        if (textOverlay) {
            cJSON* lines = cJSON_GetObjectItem(textOverlay, "Lines");
            if (cJSON_IsArray(lines)) {
                int lineCount = cJSON_GetArraySize(lines);
                for (int i = 0; i < lineCount; ++i) {
                    cJSON* line = cJSON_GetArrayItem(lines, i);
                    cJSON* lineText = cJSON_GetObjectItem(line, "LineText");
                    cJSON* minTop = cJSON_GetObjectItem(line, "MinTop");
                    cJSON* maxHeight = cJSON_GetObjectItem(line, "MaxHeight");
                    cJSON* wordsArr = cJSON_GetObjectItem(line, "Words");

                    if (lineText && cJSON_IsString(lineText) && minTop && maxHeight && cJSON_IsArray(wordsArr)) {
                        int wordCount = cJSON_GetArraySize(wordsArr);
                        if (wordCount > 0) {
                            float minLeft = 99999.0f;
                            float maxRight = 0.0f;

                            for (int j = 0; j < wordCount; ++j) {
                                cJSON* wObj = cJSON_GetArrayItem(wordsArr, j);
                                cJSON* left = cJSON_GetObjectItem(wObj, "Left");
                                cJSON* width = cJSON_GetObjectItem(wObj, "Width");
                                if (left && width) {
                                    float l = left->valuedouble;
                                    float r = l + width->valuedouble;
                                    if (l < minLeft) minLeft = l;
                                    if (r > maxRight) maxRight = r;
                                }
                            }

                            if (minLeft < 99999.0f) {
                                OcrWord w;
                                w.text = lineText->valuestring;
                                // Ekran 1280x720, koordinatları 0..1 aralığına normalize ediyoruz
                                w.x = minLeft / 1280.0f;
                                w.y = minTop->valuedouble / 720.0f;
                                w.w = (maxRight - minLeft) / 1280.0f;
                                w.h = maxHeight->valuedouble / 720.0f;
                                words.push_back(w);
                            }
                        }
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
    return words;
}

namespace Ocr {

OcrResult runOcrSpace(std::vector<uint8_t> jpegData, const std::string& apiKey, const std::string& lang) {
    OcrResult result;
    if (jpegData.empty()) {
        result.errorMsg = "OCR Hatası: Ekran görüntüsü boş";
        return result;
    }

    const std::string url = "https://api.ocr.space/parse/image";

    std::string ocrLang = lang;
    // Config dillerini büyük harf saklar (JA, TR vb.), OCR.Space mapping lowercase bekliyor
    for (auto& c : ocrLang) c = tolower((unsigned char)c);
    if (ocrLang == "ja") ocrLang = "jpn";
    else if (ocrLang == "en") ocrLang = "eng";
    else if (ocrLang == "tr") ocrLang = "tur";
    else if (ocrLang == "ko") ocrLang = "kor";
    else if (ocrLang == "zh") ocrLang = "chs";
    else if (ocrLang == "de") ocrLang = "ger";
    else if (ocrLang == "fr") ocrLang = "fre";
    else if (ocrLang == "es") ocrLang = "spa";
    else if (ocrLang == "it") ocrLang = "ita";
    else if (ocrLang == "ru") ocrLang = "rus";
    else if (ocrLang == "bg") ocrLang = "bul";
    else if (ocrLang == "cs") ocrLang = "cze";
    else if (ocrLang == "da") ocrLang = "dan";
    else if (ocrLang == "nl") ocrLang = "dut";
    else if (ocrLang == "fi") ocrLang = "fin";
    else if (ocrLang == "el") ocrLang = "gre";
    else if (ocrLang == "hu") ocrLang = "hun";
    else if (ocrLang == "pl") ocrLang = "pol";
    else if (ocrLang == "pt") ocrLang = "por";
    else if (ocrLang == "sl") ocrLang = "slv";
    else if (ocrLang == "sv") ocrLang = "swe";
    
    std::string ocrEngine = "2";
    if (ocrLang == "jpn" || ocrLang == "kor" || ocrLang == "chs" || ocrLang == "rus" || ocrLang == "bul" || ocrLang == "gre" || ocrLang == "tur") {
        ocrEngine = "1";
    }

    std::string keyToUse = apiKey.empty() ? "helloworld" : apiKey;
    HttpResponse resp;
    for (int retry = 0; retry < 3; retry++) {
        resp = HttpClient::postMultipart(
            url,
            jpegData,
            "file",
            "screenshot.jpg",
            {
                {"apikey",        keyToUse},
                {"language",      ocrLang},
                {"isTable",       "false"},
                {"scale",         "true"},
                {"OCREngine",     ocrEngine},
                {"isOverlayRequired", "true"}  // Bounding box için true
            }
        );
        
        if (resp.ok()) {
            break;
        }
        svcSleepThread(1000000000ull); // 1 saniye bekle
    }

    if (!resp.ok()) {
        if (resp.statusCode == 0) {
            result.errorMsg = "OCR.Space Hatası: Bağlantı kurulamadı — " + resp.errorStr;
        } else if (resp.statusCode == 408) {
            result.errorMsg = "OCR.Space Hatası: İstek zaman aşımına uğradtı (HTTP 408) — görüntü çok büyük olabilir";
        } else if (resp.statusCode == 429) {
            result.errorMsg = "OCR.Space Hatası: Günlük istek sınırı doldu (HTTP 429) — ücretsiz plan: günde 500 istek";
        } else if (resp.statusCode == 500) {
            result.errorMsg = "OCR.Space Hatası: Sunucu hatası (HTTP 500) — daha sonra tekrar deneyin";
        } else if (resp.statusCode == 503) {
            result.errorMsg = "OCR.Space Hatası: Servis geçici olarak kullanılamıyor (HTTP 503)";
        } else {
            result.errorMsg = "OCR.Space Hatası: HTTP " + std::to_string(resp.statusCode);
        }
        return result;
    }

    result.words = ocrSpaceExtractLines(resp.body);
    if (result.words.empty()) {
        std::string errStr = jsonExtract(resp.body, "ErrorMessage");
        if (!errStr.empty()) {
            result.errorMsg = "OCR.Space Hatası: " + errStr;
        } else {
            // Try to extract ParsedText as fallback
            std::string parsed = jsonExtract(resp.body, "ParsedText");
            if (!parsed.empty()) {
                OcrWord w;
                w.text = parsed;
                w.x = 0.1f; w.y = 0.1f; w.w = 0.8f; w.h = 0.8f;
                result.words.push_back(w);
                result.fullText = parsed;
                result.success = true;
                return result;
            } else {
                result.errorMsg = "OCR.Space: Görüntüde metin bulunamadı";
            }
        }
        return result;
    }

    // Geriye uyumluluk için fullText'i de oluştur
    for (const auto& w : result.words) {
        result.fullText += w.text + "\n";
    }

    result.success = true;
    return result;
}

// Google Vision yanıtından kelimeleri (artık satır/blok olarak) çek
static std::vector<OcrWord> googleVisionExtractWords(const std::string& jsonBody) {
    std::vector<OcrWord> lines;
    cJSON* root = cJSON_Parse(jsonBody.c_str());
    if (!root) return lines;

    cJSON* responses = cJSON_GetObjectItem(root, "responses");
    if (cJSON_IsArray(responses) && cJSON_GetArraySize(responses) > 0) {
        cJSON* resp0 = cJSON_GetArrayItem(responses, 0);
        
        cJSON* textAnnotations = cJSON_GetObjectItem(resp0, "textAnnotations");
        if (cJSON_IsArray(textAnnotations) && cJSON_GetArraySize(textAnnotations) > 0) {
            // 0. index tüm metindir ve satır/blok ayrımını \n ile yapar.
            cJSON* ann0 = cJSON_GetArrayItem(textAnnotations, 0);
            cJSON* desc = cJSON_GetObjectItem(ann0, "description");
            if (desc && cJSON_IsString(desc)) {
                std::string fullText = desc->valuestring;
                size_t pos = 0;
                float dummyY = 0.0f; // Sıralamanın bozulmaması için dummy koordinat

                // \n'e göre böl
                while ((pos = fullText.find('\n')) != std::string::npos) {
                    std::string lineText = fullText.substr(0, pos);
                    if (!lineText.empty()) {
                        OcrWord w;
                        w.text = lineText;
                        w.x = 0.0f; w.y = dummyY; w.w = 1.0f; w.h = 0.1f;
                        lines.push_back(w);
                        dummyY += 0.1f;
                    }
                    fullText.erase(0, pos + 1);
                }
                // Sonda kalan metin
                if (!fullText.empty()) {
                    OcrWord w;
                    w.text = fullText;
                    w.x = 0.0f; w.y = dummyY; w.w = 1.0f; w.h = 0.1f;
                    lines.push_back(w);
                }
            }
        }
    }
    cJSON_Delete(root);
    return lines;
}

OcrResult runGoogleVision(std::vector<uint8_t> jpegData,
                     const std::string& apiKey,
                     const std::string& /*lang*/) {
    OcrResult result;
    if (jpegData.empty() || apiKey.empty()) {
        result.errorMsg = "OCR Hatası: JPEG verisi veya Vision API key eksik";
        return result;
    }

    std::string body;
    // Base64 + JSON kabuğunu aynı stringe koyuyoruz, 512KB'ı aşan JPEG'lerde OOM (2168-0001) olmasını önleriz
    body.reserve(400 + ((jpegData.size() + 2) / 3) * 4);
    body += R"({"requests":[{"image":{"content":")";

    static const char* b64chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < jpegData.size(); i += 3) {
        uint32_t val = (jpegData[i] << 16);
        if (i + 1 < jpegData.size()) val |= (jpegData[i+1] << 8);
        if (i + 2 < jpegData.size()) val |= jpegData[i+2];
        body += b64chars[(val >> 18) & 0x3F];
        body += b64chars[(val >> 12) & 0x3F];
        body += (i + 1 < jpegData.size()) ? b64chars[(val >> 6) & 0x3F] : '=';
        body += (i + 2 < jpegData.size()) ? b64chars[val & 0x3F] : '=';
    }

    // JPEG hafızasını HTTP request'inden önce HEMEN boşalt (OOM Crash 2168-0001 fix)
    jpegData.clear();
    jpegData.shrink_to_fit();

    body += R"("},"features":[{"type":"DOCUMENT_TEXT_DETECTION"}]}]})";


    std::string url = "https://vision.googleapis.com/v1/images:annotate?key=" + apiKey;

    HttpResponse resp;
    for (int retry = 0; retry < 3; retry++) {
        resp = HttpClient::post(url, body,
            {"Content-Type: application/json"});
        if (resp.ok()) {
            break;
        }
        svcSleepThread(1000000000ull); // 1 saniye bekle
    }

    if (!resp.ok()) {
        if (resp.statusCode == 0) {
            result.errorMsg = "Google Vision Hatası: Bağlantı kurulamadı — " + resp.errorStr;
        } else if (resp.statusCode == 400) {
            result.errorMsg = "Google Vision Hatası: Geçersiz istek (HTTP 400) — API key formatı yanlış?";
        } else if (resp.statusCode == 401) {
            result.errorMsg = "Google Vision Hatası: Kimlik doğrulama başarısız (HTTP 401) — API key geçersiz";
        } else if (resp.statusCode == 403) {
            result.errorMsg = "Google Vision Hatası: Erişim reddedildi (HTTP 403) — Vision API etkinleştirilmemiş?";
        } else if (resp.statusCode == 429) {
            result.errorMsg = "Google Vision Hatası: İstek sınırı aşıldı (HTTP 429) — kota doldu";
        } else if (resp.statusCode == 500) {
            result.errorMsg = "Google Vision Hatası: Google sunucu hatası (HTTP 500) — daha sonra tekrar deneyin";
        } else if (resp.statusCode == 503) {
            result.errorMsg = "Google Vision Hatası: Servis geçici olarak kullanılamıyor (HTTP 503)";
        } else {
            result.errorMsg = "Google Vision Hatası: HTTP " + std::to_string(resp.statusCode);
        }
        return result;
    }

    result.words = googleVisionExtractWords(resp.body);
    if (result.words.empty()) {
        std::string errorField = jsonExtract(resp.body, "message");
        if (!errorField.empty()) {
            result.errorMsg = "Google Vision Hatası: " + errorField;
        } else {
            result.errorMsg = "Google Vision: Görüntüde metin bulunamadı";
        }
        return result;
    }

    // Geriye uyumluluk için fullText
    result.fullText = jsonExtract(resp.body, "description");

    result.success = true;
    return result;
}
} // namespace Ocr
