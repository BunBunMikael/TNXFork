#include "translate.hpp"
#include "http_client.hpp"
#include <switch.h>
#include <cstring>
#include <cJSON.h>

// ─── URL encode (MyMemory için) ────────────────────────────────────────────
static std::string urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}



namespace Translate {

TranslateResult runMyMemory(const std::vector<std::string>& lines, const std::string& langPair) {
    TranslateResult result;
    if (lines.empty()) {
        result.errorMsg = "Çeviri: OCR'dan metin gelmedi";
        return result;
    }

    std::string mergedText;
    for (size_t i = 0; i < lines.size(); ++i) {
        mergedText += lines[i];
        if (i < lines.size() - 1) mergedText += " \n \n ";
    }

    std::string safeText = mergedText.size() > 500 ? mergedText.substr(0, 500) : mergedText;
    std::string url = "https://api.mymemory.translated.net/get?q=" +
                      urlEncode(safeText) +
                      "&langpair=" + langPair +
                      "&de=switchocr@example.com";

    HttpResponse resp;
    for (int retry = 0; retry < 3; retry++) {
        resp = HttpClient::get(url);
        if (resp.ok()) {
            break;
        }
        svcSleepThread(1000000000ull); // 1 saniye bekle ve tekrar dene
    }

    if (!resp.ok()) {
        if (resp.statusCode == 0) {
            result.errorMsg = "MyMemory: Bağlantı kurulamadı — " + resp.errorStr;
        } else if (resp.statusCode == 429) {
            result.errorMsg = "MyMemory: Günlük ücretsiz kota doldu (HTTP 429) — günde 1000 karakter";
        } else if (resp.statusCode == 503) {
            result.errorMsg = "MyMemory: Servis geçici olarak kullanılamıyor (HTTP 503) — daha sonra tekrar deneyin";
        } else {
            result.errorMsg = "MyMemory: HTTP " + std::to_string(resp.statusCode);
        }
        return result;
    }

    cJSON* root = cJSON_Parse(resp.body.c_str());
    if (root) {
        cJSON* respData = cJSON_GetObjectItem(root, "responseData");
        if (respData) {
            cJSON* translated = cJSON_GetObjectItem(respData, "translatedText");
            if (translated && cJSON_IsString(translated)) {
                result.translatedText = translated->valuestring;
            }
        }
        cJSON_Delete(root);
    }

    if (result.translatedText.empty()) {
        result.errorMsg = "MyMemory: Boş yanıt döndü — API sınırı aşıldı olabilir";
        return result;
    }

    if (result.translatedText.find("QUERY LENGTH") != std::string::npos) {
        result.errorMsg = "MyMemory: Metin çok uzun (500 karakter sınırı aşıldı)";
        return result;
    }

    // Split by \n \n 
    size_t pos = 0;
    std::string token;
    std::string delimiter = " \n \n ";
    std::string s = result.translatedText;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        result.translatedLines.push_back(s.substr(0, pos));
        s.erase(0, pos + delimiter.length());
    }
    result.translatedLines.push_back(s);

    // Eğer sayı uyuşmuyorsa, düz metin gibi kabul edeceğiz
    result.success = true;
    return result;
}

TranslateResult runDeepL(const std::vector<std::string>& lines,
                            const std::string& apiKey,
                            const std::string& sourceLang,
                            const std::string& targetLang) {
    TranslateResult result;
    if (lines.empty() || apiKey.empty()) {
        result.errorMsg = "DeepL: API key girilmemiş";
        return result;
    }

    std::string body = "target_lang=" + targetLang;
    if (!sourceLang.empty()) {
        body += "&source_lang=" + sourceLang;
    }
    for (const auto& line : lines) {
        body += "&text=" + urlEncode(line);
    }

    HttpResponse resp;
    for (int retry = 0; retry < 3; retry++) {
        resp = HttpClient::post(
            "https://api-free.deepl.com/v2/translate",
            body,
            {
                "Authorization: DeepL-Auth-Key " + apiKey,
                "Content-Type: application/x-www-form-urlencoded"
            }
        );
        if (resp.ok()) {
            break;
        }
        svcSleepThread(1000000000ull); // 1 saniye bekle
    }

    if (!resp.ok()) {
        if (resp.statusCode == 0) {
            result.errorMsg = "DeepL: Bağlantı kurulamadı — " + resp.errorStr;
        } else if (resp.statusCode == 400) {
            result.errorMsg = "DeepL: Geçersiz istek (HTTP 400) — dil kodu yanlış olabilir";
        } else if (resp.statusCode == 403) {
            result.errorMsg = "DeepL: Geçersiz API key (HTTP 403) — ücretsiz key mi, yoksa PRO mu?";
        } else if (resp.statusCode == 413) {
            result.errorMsg = "DeepL: Metin çok büyük (HTTP 413) — daha az metin gönderin";
        } else if (resp.statusCode == 429) {
            result.errorMsg = "DeepL: İstek oran sınırı aşıldı (HTTP 429) — biraz bekleyip tekrar deneyin";
        } else if (resp.statusCode == 456) {
            result.errorMsg = "DeepL: Aylık karakter kotası doldu (HTTP 456) — plan yenileme tarihinizi kontrol edin";
        } else if (resp.statusCode == 500) {
            result.errorMsg = "DeepL: Sunucu hatası (HTTP 500) — daha sonra tekrar deneyin";
        } else if (resp.statusCode == 503) {
            result.errorMsg = "DeepL: Servis geçici olarak kullanılamıyor (HTTP 503) — daha sonra tekrar deneyin";
        } else if (resp.statusCode == 529) {
            result.errorMsg = "DeepL: Sunucu aşırı yüklendi (HTTP 529) — dakika sonra tekrar deneyin";
        } else {
            result.errorMsg = "DeepL: HTTP " + std::to_string(resp.statusCode);
        }
        return result;
    }

    // DeepL JSON: {"translations":[{"detected_source_language":"EN","text":"Tr1"}, {"text":"Tr2"}]}
    // Biz cJSON ile tüm text alanlarını toplayacağız.
    cJSON* root = cJSON_Parse(resp.body.c_str());
    if (root) {
        cJSON* translations = cJSON_GetObjectItem(root, "translations");
        if (cJSON_IsArray(translations)) {
            int count = cJSON_GetArraySize(translations);
            for (int i = 0; i < count; ++i) {
                cJSON* tr = cJSON_GetArrayItem(translations, i);
                cJSON* txt = cJSON_GetObjectItem(tr, "text");
                if (txt && cJSON_IsString(txt)) {
                    result.translatedLines.push_back(txt->valuestring);
                    result.translatedText += std::string(txt->valuestring) + "\n";
                }
            }
        }
        cJSON_Delete(root);
    }

    if (result.translatedLines.empty()) {
        result.errorMsg = "DeepL: Boş yanıt döndü — API key doğru mu?";
        return result;
    }

    result.success = true;
    return result;
}


// ─── Google Cloud Translation API ─────────────────────────────────────────
TranslateResult runGoogleCloud(const std::vector<std::string>& lines, const std::string& apiKey, const std::string& sourceLang, const std::string& targetLang) {
    TranslateResult result;
    if (apiKey.empty()) {
        result.errorMsg = "Google Cloud Translate: API key girilmemiş";
        return result;
    }
    if (lines.empty()) {
        result.errorMsg = "Google Cloud Translate: Çevrilecek metin yok";
        return result;
    }

    std::string url = "https://translation.googleapis.com/language/translate/v2?key=" + apiKey;

    // Body: {"q": ["l1", "l2"], "target": "tr", "source": "ja", "format": "text"}
    cJSON* root = cJSON_CreateObject();
    cJSON* qArr = cJSON_CreateArray();
    for (const auto& line : lines) {
        cJSON_AddItemToArray(qArr, cJSON_CreateString(line.c_str()));
    }
    cJSON_AddItemToObject(root, "q", qArr);
    cJSON_AddStringToObject(root, "target", targetLang.c_str());
    if (!sourceLang.empty() && sourceLang != "auto") {
        cJSON_AddStringToObject(root, "source", sourceLang.c_str());
    }
    cJSON_AddStringToObject(root, "format", "text");

    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string body(jsonStr);
    free(jsonStr);
    cJSON_Delete(root);

    std::vector<std::string> headers = {
        "Content-Type: application/json"
    };

    auto resp = HttpClient::post(url, body, headers);
    if (!resp.ok()) {
        if (resp.statusCode == 0) {
            result.errorMsg = "Google Cloud Translate: Bağlantı kurulamadı — " + resp.errorStr;
        } else if (resp.statusCode == 400) {
            result.errorMsg = "Google Cloud Translate: Geçersiz istek (HTTP 400) — dil kodu yanlış olabilir";
        } else if (resp.statusCode == 401) {
            result.errorMsg = "Google Cloud Translate: Kimlik doğrulama hatası (HTTP 401) — API key geçersiz";
        } else if (resp.statusCode == 403) {
            result.errorMsg = "Google Cloud Translate: Erişim reddedildi (HTTP 403) — Cloud Translation API etkinleştirilmemiş?";
        } else if (resp.statusCode == 429) {
            result.errorMsg = "Google Cloud Translate: İstek sınırı aşıldı (HTTP 429) — kota doldu";
        } else if (resp.statusCode == 500) {
            result.errorMsg = "Google Cloud Translate: Google sunucu hatası (HTTP 500) — daha sonra tekrar deneyin";
        } else if (resp.statusCode == 503) {
            result.errorMsg = "Google Cloud Translate: Servis geçici olarak kullanılamıyor (HTTP 503)";
        } else {
            result.errorMsg = "Google Cloud Translate: HTTP " + std::to_string(resp.statusCode) + " — " + resp.errorStr;
        }
        return result;
    }

    // Response: {"data": {"translations": [{"translatedText": "ceviri"}]}}
    cJSON* respObj = cJSON_Parse(resp.body.c_str());
    if (!respObj) {
        result.errorMsg = "Google Cloud Translate: Sunucu yanıtı işlenemedi (JSON parse hatası)";
        return result;
    }

    cJSON* data = cJSON_GetObjectItem(respObj, "data");
    if (data) {
        cJSON* translations = cJSON_GetObjectItem(data, "translations");
        if (cJSON_IsArray(translations)) {
            int count = cJSON_GetArraySize(translations);
            for (int i = 0; i < count; i++) {
                cJSON* item = cJSON_GetArrayItem(translations, i);
                cJSON* textNode = cJSON_GetObjectItem(item, "translatedText");
                if (textNode && cJSON_IsString(textNode) && textNode->valuestring) {
                    result.translatedLines.push_back(textNode->valuestring);
                } else {
                    result.translatedLines.push_back("");
                }
            }
        }
    }
    cJSON_Delete(respObj);

    for (const auto& l : result.translatedLines) {
        result.translatedText += l + "\n";
    }

    if (result.translatedLines.empty()) {
        result.errorMsg = "Google Cloud Translate: Görüntüde metin bulunamadı";
    } else {
        result.success = true;
    }

    return result;
}

} // namespace Translate
