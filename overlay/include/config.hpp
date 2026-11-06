#pragma once
#include <string>

enum class OcrApi {
    OcrSpace,
    GoogleVision
};

enum class TranslateApi {
    MyMemory,
    DeepL,
    GoogleCloud
};

struct Config {
    OcrApi        ocrApi       = OcrApi::OcrSpace;
    TranslateApi  translateApi = TranslateApi::MyMemory;
    std::string   srcLang      = "ja";      // kaynak dil (ja, en, ...)
    std::string   dstLang      = "tr";      // hedef dil
    std::string   uiLang       = "tr";      // arayüz dili (tr, en)
    
    std::string   ocrApiKey;                // OCR.space
    std::string   visionApiKey;             // Google Vision
    
    std::string   deeplApiKey;              // DeepL
    std::string   googleTransApiKey;        // Google Cloud Translate
    bool          liveDebug    = false;     // Live HUD: always show status line + heap usage
};

// SD:/config/translate/config.ini
namespace ConfigManager {
    Config load();
    void   save(const Config& cfg);

    // Switch'in kendi sanal klavyesini açar, kullanıcıdan string alır
    // title: Klavye üstünde gösterilecek başlık
    // out:   Kullanıcının girdiği string (çıktı)
    // Dönüş: true → kullanıcı OK'ladı, false → iptal
    bool promptKeyboard(const char* title, std::string& out);
}
