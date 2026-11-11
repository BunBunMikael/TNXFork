// Switch Translate Overlay — L+R+ZL ile aç
// TESLA_INIT_IMPL sadece bu dosyada tanımlanıyor (tek implementation unit)
#define TESLA_INIT_IMPL
#include "tesla.hpp"

#include "config.hpp"
#include "live.hpp"
#include "screenshot.hpp"
#include "http_client.hpp"
#include "ocr.hpp"
#include "translate.hpp"

#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <switch.h>

// ─── Global durum ─────────────────────────────────────────────────────────
static Config            g_config;
static std::atomic<bool> g_translating{false};
static std::mutex        g_resultMutex;
static std::vector<OcrWord> g_ocrWords;
static std::vector<std::string> g_translatedLines;
static std::string       g_errorText;
static std::string       g_originalText;

// ─── Yardımcı Fonksiyon: UI Çevirisi ─────────────────────────────────────
static std::string L(const std::string& tr, const std::string& en) {
    return g_config.uiLang == "EN" ? en : tr;
}

// ─── Forward declarations ─────────────────────────────────────────────────
class SetupGui;
class TranslateGui;
class SettingsGui;
class OnScreenOverlayGui;

void reloadOverlay();

// ─── Arka planda çeviri yap ────────────────────────────────────────────────
static void doTranslate(std::vector<uint8_t> jpegData) {
    {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        g_ocrWords.clear();
        g_translatedLines.clear();
        g_errorText.clear();
        g_originalText.clear();
    }

    if (jpegData.empty()) {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        g_errorText    = L("OCR Hatası: Ekran görüntüsü alınamadı", "OCR Error: Screenshot could not be captured");
        g_translating  = false;
        return;
    }

    OcrResult ocr;
    if (g_config.ocrApi == OcrApi::GoogleVision) {
        if (g_config.visionApiKey.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("OCR Hatası: Google Vision API key girilmemiş", "OCR Error: Google Vision API key is missing");
            g_translating = false;
            return;
        }
        ocr = Ocr::runGoogleVision(jpegData, g_config.visionApiKey, g_config.srcLang);
    } else {
        ocr = Ocr::runOcrSpace(jpegData, g_config.ocrApiKey, g_config.srcLang);
    }

    if (!ocr.success) {
        std::lock_guard<std::mutex> lk(g_resultMutex);
        g_errorText   = ocr.errorMsg;
        g_translating = false;
        return;
    }

    std::vector<std::string> linesToTranslate;
    std::vector<OcrWord> filteredWords;
    for (const auto& w : ocr.words) {
        linesToTranslate.push_back(w.text);
        filteredWords.push_back(w);
    }

    TranslateResult tr;
    std::string langPair = g_config.srcLang + "|" + g_config.dstLang;
    if (g_config.translateApi == TranslateApi::DeepL) {
        if (g_config.deeplApiKey.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("Çeviri Hatası: DeepL API key girilmemiş", "Translation Error: DeepL API key is missing");
            g_translating = false;
            return;
        }
        std::string deeplSource = g_config.srcLang;
        std::transform(deeplSource.begin(), deeplSource.end(), deeplSource.begin(), ::toupper);
        std::string deeplTarget = g_config.dstLang;
        std::transform(deeplTarget.begin(), deeplTarget.end(), deeplTarget.begin(), ::toupper);
        tr = Translate::runDeepL(linesToTranslate, g_config.deeplApiKey, deeplSource, deeplTarget);
    } else if (g_config.translateApi == TranslateApi::GoogleCloud) {
        if (g_config.googleTransApiKey.empty()) {
            std::lock_guard<std::mutex> lk(g_resultMutex);
            g_errorText   = L("Çeviri Hatası: Google Cloud API key girilmemiş", "Translation Error: Google Cloud API key is missing");
            g_translating = false;
            return;
        }
        tr = Translate::runGoogleCloud(linesToTranslate, g_config.googleTransApiKey, g_config.srcLang, g_config.dstLang);
    } else {
        tr = Translate::runMyMemory(linesToTranslate, langPair);
    }

    std::lock_guard<std::mutex> lk(g_resultMutex);
    g_originalText = ocr.fullText;
    g_ocrWords = filteredWords;
    if (tr.success) {
        g_translatedLines = tr.translatedLines;
        g_errorText.clear();
    } else {
        g_errorText = tr.errorMsg;
    }
    g_translating = false;
}

// ═══════════════════════════════════════════════════════════════════════════
std::string getLanguageName(const std::string& code) {
    if (code == "TR") return L("Türkçe", "Turkish");
    if (code == "EN") return L("İngilizce", "English");
    if (code == "JA") return L("Japonca", "Japanese");
    if (code == "KO") return L("Korece", "Korean");
    if (code == "ZH") return L("Çince", "Chinese");
    if (code == "DE") return L("Almanca", "German");
    if (code == "FR") return L("Fransızca", "French");
    if (code == "ES") return L("İspanyolca", "Spanish");
    if (code == "IT") return L("İtalyanca", "Italian");
    if (code == "RU") return L("Rusça", "Russian");
    if (code == "BG") return L("Bulgarca", "Bulgarian");
    if (code == "CS") return L("Çekçe", "Czech");
    if (code == "DA") return L("Danca", "Danish");
    if (code == "NL") return L("Felemenkçe", "Dutch");
    if (code == "FI") return L("Fince", "Finnish");
    if (code == "EL") return L("Yunanca", "Greek");
    if (code == "HU") return L("Macarca", "Hungarian");
    if (code == "PL") return L("Lehçe", "Polish");
    if (code == "PT") return L("Portekizce", "Portuguese");
    if (code == "SL") return L("Slovence", "Slovenian");
    if (code == "SV") return L("İsveççe", "Swedish");
    return code;
}

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA 4: Dil Seçimi
// ═══════════════════════════════════════════════════════════════════════════
class OcrApiSelectGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("OCR API Secimi", "OCR API Select"), L("[B] Geri", "[B] Back"));
        auto* list  = new tsl::elm::List();

        std::vector<std::pair<OcrApi, std::string>> apis = {
            {OcrApi::OcrSpace, "OCR.space"},
            {OcrApi::GoogleVision, "Google Vision"}
        };

        for (const auto& api : apis) {
            auto* item = new tsl::elm::ListItem(api.second);
            item->setClickListener([this, api](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (g_config.ocrApi != api.first) {
                        g_config.ocrApi = api.first;
                        ConfigManager::save(g_config);
                        tsl::goBack();
                    } else {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

class TranslateApiSelectGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Çeviri API Seçimi", "Translate API Select"), L("[B] Geri", "[B] Back"));
        auto* list  = new tsl::elm::List();

        std::vector<std::pair<TranslateApi, std::string>> apis = {
            {TranslateApi::MyMemory, "MyMemory"},
            {TranslateApi::DeepL, "DeepL"},
            {TranslateApi::GoogleCloud, "Google Cloud"}
        };

        for (const auto& api : apis) {
            auto* item = new tsl::elm::ListItem(api.second);
            item->setClickListener([this, api](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (g_config.translateApi != api.first) {
                        g_config.translateApi = api.first;
                        ConfigManager::save(g_config);
                        tsl::goBack();
                    } else {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

class UiLanguageSelectGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Arayüz Dili", "UI Language"), L("[B] Geri", "[B] Back"));
        auto* list  = new tsl::elm::List();

        std::vector<std::string> langs = {"TR", "EN"};

        for (const auto& langCode : langs) {
            auto* item = new tsl::elm::ListItem(getLanguageName(langCode));
            item->setClickListener([this, langCode](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (g_config.uiLang != langCode) {
                        g_config.uiLang = langCode;
                        ConfigManager::save(g_config);
                        reloadOverlay();
                    } else {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

class LanguageSelectGui : public tsl::Gui {
    bool m_isSource;
public:
    LanguageSelectGui(bool isSource) : m_isSource(isSource) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Dil Seçimi", "Select Language"), L("[B] Geri", "[B] Back"));
        auto* list  = new tsl::elm::List();

        std::vector<std::string> langs = {
            "TR", "EN", "JA", "KO", "ZH", "DE", "FR", "ES", "IT", "RU", 
            "BG", "CS", "DA", "NL", "FI", "EL", "HU", "PL", "PT", "SL", "SV"
        };

        for (const auto& langCode : langs) {
            auto* item = new tsl::elm::ListItem(getLanguageName(langCode));
            item->setClickListener([this, langCode](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    if (this->m_isSource) {
                        g_config.srcLang = langCode;
                    } else {
                        g_config.dstLang = langCode;
                    }
                    ConfigManager::save(g_config);
                    tsl::goBack();
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA 3: Ayarlar
// ═══════════════════════════════════════════════════════════════════════════
class HelpGui;

class SettingsGui : public tsl::Gui {
    tsl::elm::ListItem* m_ocrApiItem = nullptr;
    tsl::elm::ListItem* m_transApiItem = nullptr;
    tsl::elm::ListItem* m_srcItem = nullptr;
    tsl::elm::ListItem* m_dstItem = nullptr;
    tsl::elm::ListItem* m_uiLangItem = nullptr;

    void update() override {
        if (m_srcItem) m_srcItem->setValue(getLanguageName(g_config.srcLang));
        if (m_dstItem) m_dstItem->setValue(getLanguageName(g_config.dstLang));
        if (m_uiLangItem) m_uiLangItem->setValue(g_config.uiLang == "EN" ? "English" : "Türkçe");
        
        if (m_ocrApiItem) {
            if (g_config.ocrApi == OcrApi::GoogleVision) m_ocrApiItem->setValue("Google Vision");
            else m_ocrApiItem->setValue("OCR.space");
        }
        if (m_transApiItem) {
            if (g_config.translateApi == TranslateApi::DeepL) m_transApiItem->setValue("DeepL");
            else if (g_config.translateApi == TranslateApi::GoogleCloud) m_transApiItem->setValue("Google Cloud");
            else m_transApiItem->setValue("MyMemory");
        }
    }

public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Ayarlar", "Settings"), L("[B] Geri", "[B] Back"));
        auto* list  = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader(L("API AYARLARI", "API SETTINGS")));

        m_ocrApiItem = new tsl::elm::ListItem("OCR API");
        m_ocrApiItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<OcrApiSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_ocrApiItem);

        m_transApiItem = new tsl::elm::ListItem(L("Çeviri API", "Translate API"));
        m_transApiItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<TranslateApiSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_transApiItem);

        list->addItem(new tsl::elm::CategoryHeader(L("DİL AYARLARI", "LANGUAGE SETTINGS")));

        m_srcItem = new tsl::elm::ListItem(L("Kaynak Dil", "Source Lang"));
        m_srcItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<LanguageSelectGui>(true);
                return true;
            }
            return false;
        });
        list->addItem(m_srcItem);

        m_dstItem = new tsl::elm::ListItem(L("Hedef Dil", "Target Lang"));
        m_dstItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<LanguageSelectGui>(false);
                return true;
            }
            return false;
        });
        list->addItem(m_dstItem);

        m_uiLangItem = new tsl::elm::ListItem(L("Arayüz Dili", "UI Language"));
        m_uiLangItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<UiLanguageSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_uiLangItem);

        list->addItem(new tsl::elm::CategoryHeader(L("OCR API ANAHTARLARI", "OCR API KEYS")));

        auto* editOcrItem = new tsl::elm::ListItem("OCR.space API Key");
        editOcrItem->setValue(g_config.ocrApiKey.empty() ? L("Pasif", "None") : L("Aktif", "Set"));
        editOcrItem->setValueColor(g_config.ocrApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editOcrItem);

        auto* editVisItem = new tsl::elm::ListItem("Google Vision API Key");
        editVisItem->setValue(g_config.visionApiKey.empty() ? L("Pasif", "None") : L("Aktif", "Set"));
        editVisItem->setValueColor(g_config.visionApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editVisItem);

        list->addItem(new tsl::elm::CategoryHeader(L("ÇEVİRİ API ANAHTARLARI", "TRANSLATE API KEYS")));

        auto* editDeeplItem = new tsl::elm::ListItem("DeepL API Key");
        editDeeplItem->setValue(g_config.deeplApiKey.empty() ? L("Pasif", "None") : L("Aktif", "Set"));
        editDeeplItem->setValueColor(g_config.deeplApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editDeeplItem);

        auto* editGoogleTransItem = new tsl::elm::ListItem("Google Cloud API Key");
        editGoogleTransItem->setValue(g_config.googleTransApiKey.empty() ? L("Pasif", "None") : L("Aktif", "Set"));
        editGoogleTransItem->setValueColor(g_config.googleTransApiKey.empty() ? tsl::Color(15, 0, 0, 15) : tsl::Color(0, 15, 0, 15));
        list->addItem(editGoogleTransItem);

        frame->setContent(list);
        return frame;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// DÜZ METİN VE KAYDIRMA (SCROLL) SİSTEMİ
// ═══════════════════════════════════════════════════════════════════════════
std::vector<std::string> splitTextGlobal(const std::string& text, size_t maxLen) {
    std::vector<std::string> lines;
    std::string currentLine;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (!currentLine.empty() && currentLine.length() + word.length() + 1 > maxLen) {
                lines.push_back(currentLine);
                currentLine = word;
            } else {
                if (!currentLine.empty()) currentLine += " ";
                currentLine += word;
            }
            word.clear();
            if (c == '\n') {
                lines.push_back(currentLine);
                currentLine.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (!currentLine.empty() && currentLine.length() + word.length() + 1 > maxLen) {
            lines.push_back(currentLine);
            lines.push_back(word);
        } else {
            if (!currentLine.empty()) currentLine += " ";
            currentLine += word;
            lines.push_back(currentLine);
        }
    } else if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    return lines;
}

class ScrollText : public tsl::elm::Element {
    std::vector<std::pair<std::string, tsl::Color>> m_lines;
    u16 m_fontSize;
    float m_scrollY = 0;
    float m_maxScrollY = 0;
    
public:
    ScrollText(u16 fontSize) : m_fontSize(fontSize) {}

    void addText(const std::string& text, tsl::Color color, size_t maxLen) {
        auto splitted = splitTextGlobal(text, maxLen);
        for (const auto& s : splitted) {
            m_lines.push_back({s, color});
        }
    }

    void addEmptyLine() {
        m_lines.push_back({"", tsl::Color(0,0,0,0)});
    }

    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(parentX, parentY, parentWidth, parentHeight);
        
        // Tesla overlay içerik alanı (header 97 px, footer 73 px)
        s32 contentHeight = 550; // 720 - 97 - 73
        m_maxScrollY = (s32)(m_lines.size() * (m_fontSize + 10)) - contentHeight;
        if (m_maxScrollY < 0) m_maxScrollY = 0;
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        // Overlay sınırlarını sabitliyoruz
        s32 topBound = 97;
        s32 bottomBound = 647; // 720 - 73
        
        s32 contentHeight = 550;
        s32 y = topBound - m_scrollY;

        // Satırların yarım gözükmesi durumunda çerçevenin dışına taşmaması için donanımsal kırpma (scissoring) açıyoruz
        renderer->enableScissoring(this->getX(), topBound, this->getWidth(), contentHeight);

        for (const auto& pair : m_lines) {
            s32 textTop = y;
            s32 textBottom = y + m_fontSize + 2;

            if (textBottom > topBound && textTop < bottomBound) {
                renderer->drawString(pair.first, false, this->getX() + 15, textBottom, m_fontSize, pair.second);
            }
            y += m_fontSize + 10;
        }

        renderer->disableScissoring();

        // Draw scrollbar
        if (m_maxScrollY > 0) {
            float percent = (float)m_scrollY / (float)m_maxScrollY;
            s32 barHeight = (contentHeight * contentHeight) / (contentHeight + m_maxScrollY);
            if (barHeight < 20) barHeight = 20;
            s32 barY = topBound + percent * (contentHeight - barHeight);
            renderer->drawRect(this->getX() + this->getWidth() - 10, barY, 4, barHeight, tsl::Color(255, 255, 255, 128));
        }
    }

    tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection direction) override {
        return this; // Required to receive input
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        bool handled = false;
        
        // Fast scroll with D-pad or Joystick
        if ((keysHeld & HidNpadButton_Up) || joyStickPosLeft.y > 10000 || joyStickPosRight.y > 10000) {
            m_scrollY -= 15; // Faster speed
            handled = true;
        } else if ((keysHeld & HidNpadButton_Down) || joyStickPosLeft.y < -10000 || joyStickPosRight.y < -10000) {
            m_scrollY += 15; // Faster speed
            handled = true;
        }

        // Consume horizontal inputs to prevent libtesla's boundary glow animation
        if ((keysHeld & HidNpadButton_Left) || (keysHeld & HidNpadButton_Right) || 
            joyStickPosLeft.x > 10000 || joyStickPosLeft.x < -10000 ||
            joyStickPosRight.x > 10000 || joyStickPosRight.x < -10000) {
            handled = true;
        }

        if (m_scrollY < 0) m_scrollY = 0;
        if (m_scrollY > m_maxScrollY) m_scrollY = m_maxScrollY;
        
        return handled;
    }
};

class TextLineItem : public tsl::elm::ListItem {
    std::string m_text;
    u16 m_fontSize;
    tsl::Color m_color;
public:
    TextLineItem(const std::string& text, u16 fontSize, tsl::Color color) 
        : tsl::elm::ListItem(""), m_text(text), m_fontSize(fontSize), m_color(color) {
    }
    
    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(this->getX(), this->getY(), parentWidth, m_fontSize + 10);
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        // Seçim kutucuğu çizmeyip sadece metni çizeceğiz
        renderer->drawString(m_text, false, this->getX() + 15, this->getY() + m_fontSize + 2, m_fontSize, m_color);
    }
    
    // YENİ EKLENEN KISIM: Ultrahand'in varsayılan seçim kutucuğunu (cursor) tamamen kapatıyoruz!
    void drawHighlight(tsl::gfx::Renderer* renderer) override {}
    void drawFocusBackground(tsl::gfx::Renderer* renderer) override {}
    void drawClickFlash(tsl::gfx::Renderer* renderer) override {}
    void drawClickAnimation(tsl::gfx::Renderer* renderer) override {}
};

class HelpGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Yardım & Rehber", "Help & Guide"), L("[B] Geri", "[B] Back"));
        
        auto* scroll = new ScrollText(20);

        std::vector<std::string> linesTR = {
            "Herhangi bir sorun yaşarsanız GitHub'dan veya buralardan bana ulaşabilirsiniz:",
            "",
            "İnstagram: cilsertay",
            "Discord: sertay",
            "",
            "!!!KRİTİK UYARI!!!",
            "Çökmeyi önlemek için UltraHand menüsünden (+ tuşu > System) Overlay Memory değerini 4MB'den 8MB'a yükseltin!",
            "",
            "### 1. API ALMA VE LİMİTLER",
            "Uygulamanın çalışması için bir OCR (Tarama) ve bir Çeviri API'si girmeniz gerekir. Sitelerden ücretsiz hesap açıp anahtar (Key) alabilirsiniz:",
            "",
            "## OCR (Metin Tarama) API'leri",
            "Google Cloud Vision (Önerilen): Aylık 1.000 kullanım ücretsiz. En stabil çalışanıdır.",
            "OCR.space: Günlük 500 kullanım ücretsiz. Kart bilgisi istemeyen iyi bir alternatiftir.",
            "",
            "## Çeviri API'leri",
            "DeepL (Önerilen): Tek seferlik 1.000.000 karakter ücretsiz. En kaliteli çeviriyi sunar. Kotanız bittiğinde yeni ücretsiz hesap açabilirsiniz.",
            "Google Cloud Translate: Aylık 500.000 karakter ücretsiz. Kayıt esnasında kart bilgisi isteyebilir, kotayı aşmadıkça ücret kesmez.",
            "MyMemory: Günlük 5.000 karakter ücretsiz. Uygulamada hazır kurulu gelir, ayardan seçip direkt kullanabilirsiniz.",
            "",
            "### 2. API ANAHTARLARINI YÜKLEME",
            "Aldığınız kodları sisteme kaydetmek için iki yol vardır:",
            "",
            "Switch: Hbmenu > TranslateNX uygulamasını açarak girin.",
            "PC: SD Kart içindeki config/translate/config.ini dosyasını bilgisayarda açıp düzenleyin.",
            "",
            "### 3. KULLANIM ADIMLARI",
            "Oyundayken UltraHand arayüzünü açın. (Ekranın sol kenarından sağa kaydırın veya L + R + Aşağı Yön + Sağ Analoğa basın.)",
            "Menüden TranslateNX'i başlatın (Y tuşu ile kısayol atayabilirsiniz).",
            "Ayarlar'dan kullanacağınız API'leri, Oyun Dilini (Kaynak) ve Kendi Dilinizi (Hedef) seçin.",
            "Ekranda yazı varken \"Çeviriye Başla\"ya basın. Taranan cümleler listelenecektir; detaylı çeviri için üzerine tıklamanız yeterlidir.",
        };

        std::vector<std::string> linesEN = {
            "If you have any issues, you can contact me via GitHub or these platforms:",
            "",
            "Instagram: cilsertay",
            "Discord: sertay",
            "",
            "!!!CRITICAL WARNING!!!",
            "To prevent crashes, increase the Overlay Memory from 4MB to 8MB in the UltraHand menu (+ button > System)!",
            "",
            "### 1. GETTING APIs AND LIMITS",
            "To use the app, you need to enter an OCR (Scanning) and a Translation API. You can create a free account and get a Key:",
            "",
            "## OCR (Text Scanning) APIs",
            "Google Cloud Vision (Recommended): 1,000 free uses per month. The most stable option.",
            "OCR.space: 500 free uses per day. A good alternative that doesn't require card info.",
            "",
            "## Translation APIs",
            "DeepL (Recommended): 1,000,000 free characters at once. Offers the highest quality translation. You can create a new free account when you reach the limit.",
            "Google Cloud Translate: 500,000 free characters per month. May require card info during registration, but won't charge as long as you don't exceed the quota.",
            "MyMemory: 5,000 free characters per day. Comes pre-installed, you can just select it from the settings.",
            "",
            "### 2. INSTALLING API KEYS",
            "There are two ways to save the codes you received:",
            "",
            "Switch: Open the TranslateNX app via Hbmenu.",
            "PC: Open and edit the config/translate/config.ini file on your SD Card.",
            "",
            "### 3. USAGE STEPS",
            "Open the UltraHand interface while in-game. (Swipe right from the left edge of the screen or press L + R + D-Pad Down + Right Stick.)",
            "Start TranslateNX from the menu (You can assign a shortcut using the Y button).",
            "Select the APIs, Game Language (Source) and Your Language (Target) from the Settings.",
            "When there is text on the screen, press \"Start Translation\". The scanned sentences will be listed; simply click on them for detailed translation.",
        };

        const auto& lines = (g_config.uiLang == "TR") ? linesTR : linesEN;

        for (const auto& line : lines) {
            if (line.empty()) {
                scroll->addEmptyLine();
            } else {
                scroll->addText(line, tsl::Color(255, 255, 255, 255), 38);
            }
        }

        frame->setContent(scroll);
        return frame;
    }
};

class TranslationDetailGui : public tsl::Gui {
    std::string m_trText;
    std::string m_jpText;
public:
    TranslationDetailGui(const std::string& tr, const std::string& jp) 
        : m_trText(tr), m_jpText(jp) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("Detaylı Çeviri", "Translation Detail"), L("[B] Geri", "[B] Back"));
        
        auto* scroll = new ScrollText(22);

        scroll->addText(L("Çeviri", "Translation"), tsl::Color(0, 255, 0, 255), 40);
        scroll->addText(m_trText, tsl::Color(255, 255, 255, 255), 30); 
        
        scroll->addEmptyLine();
        
        scroll->addText(L("Orijinal Metin", "Original Text"), tsl::Color(255, 0, 0, 255), 40);
        scroll->addText(m_jpText, tsl::Color(150, 150, 150, 255), 30);

        frame->setContent(scroll);
        return frame;
    }
};

class TranslationItem : public tsl::elm::ListItem {
    std::string m_trText;
    std::string m_jpText;
    std::vector<std::string> m_linesTR;
    std::vector<std::string> m_linesJP;
    u16 m_customHeight;

public:
    TranslationItem(const std::string& tr, const std::string& jp, const std::vector<std::string>& trLines, const std::vector<std::string>& jpLines, u16 height)
        : tsl::elm::ListItem(""), m_trText(tr), m_jpText(jp), m_linesTR(trLines), m_linesJP(jpLines), m_customHeight(height) {
        
        this->setBoundaries(this->getX(), this->getY(), this->getWidth(), height);
        
        this->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<TranslationDetailGui>(this->m_trText, this->m_jpText);
                return true;
            }
            return false;
        });
    }

    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        // ListItem::layout resets height to 70. We must override it!
        this->setBoundaries(this->getX() + 3, this->getY(), parentWidth - 6, m_customHeight);
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        s32 x = this->getX();
        s32 y = this->getY();
        s32 w = this->getWidth();
        s32 h = this->getHeight();
        
        if (this->m_focused) {
            renderer->drawRect(x, y, w, h, tsl::Color(255, 255, 255, 50));
        }

        s32 currY = y + 27;
        for (const auto& l : m_linesTR) {
            renderer->drawString(l, false, x + 15, currY, 22, tsl::Color(255, 255, 255, 255));
            currY += 25;
        }
        currY += 2; // Extra padding between TR and JP
        for (const auto& l : m_linesJP) {
            renderer->drawString(l, false, x + 15, currY, 18, tsl::Color(150, 150, 150, 255));
            currY += 20;
        }
        renderer->drawRect(x + 10, y + h - 2, w - 20, 1, tsl::Color(255, 255, 255, 50));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// SAYFA 2: Ana Çeviri Ekranı
// ═══════════════════════════════════════════════════════════════════════════
class TranslationResultGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("TranslateNX", "TranslateNX"), L("[B] Geri Dön", "[B] Go Back"));
        auto* list = new tsl::elm::List();

        std::lock_guard<std::mutex> lk(g_resultMutex);
        if (g_ocrWords.empty() || g_translatedLines.empty()) {
            if (!g_errorText.empty()) {
                auto* errView = new ScrollText(20);
                errView->addText("HATA:\n" + g_errorText, tsl::Color(255, 100, 100, 255), 45);
                frame->setContent(errView);
                return frame;
            } else {
                list->addItem(new tsl::elm::ListItem(L("Çevirilecek yazı bulunamadı.", "No text found to translate.")));
            }
        } else {
            struct SortedItem {
                OcrWord word;
                std::string translated;
            };
            std::vector<SortedItem> items;
            size_t count = std::min(g_ocrWords.size(), g_translatedLines.size());
            for (size_t i = 0; i < count; ++i) {
                items.push_back({g_ocrWords[i], g_translatedLines[i]});
            }

            std::sort(items.begin(), items.end(), [](const SortedItem& a, const SortedItem& b) {
                if (std::abs(a.word.y - b.word.y) < 0.05f) {
                    return a.word.x < b.word.x; 
                }
                return a.word.y < b.word.y; 
            });

            for (const auto& item : items) {
                if (item.translated.empty() || item.translated == " ") continue;
                
                auto truncateUtf8 = [](const std::string& str, size_t maxChars) -> std::string {
                    size_t charCount = 0;
                    size_t byteIndex = 0;
                    for (; byteIndex < str.length(); ++byteIndex) {
                        if ((str[byteIndex] & 0xC0) != 0x80) {
                            if (charCount >= maxChars) break;
                            charCount++;
                        }
                    }
                    if (byteIndex < str.length()) {
                        return str.substr(0, byteIndex) + "...";
                    }
                    return str;
                };

                auto linesTR = splitTextGlobal(item.translated, 30);
                auto linesJP = splitTextGlobal(item.word.text, 30);
                
                u16 itemHeight = (linesTR.size() * 25) + (linesJP.size() * 20) + 20;
                
                auto* listItem = new TranslationItem(item.translated, item.word.text, linesTR, linesJP, itemHeight);
                list->addItem(listItem);
            }
        }
        
        frame->setContent(list);
        return frame;
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (keysDown & HidNpadButton_B) {
            tsl::goBack(); // Normal menuye don
            return true;
        }
        return false;
    }
};

// ─── Global olarak çekilen fotoğrafı saklayalım ───────────────────────────
static std::vector<uint8_t> g_screenshotData;

class LoadingGui : public tsl::Gui {
    int m_frames = 0;
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(L("TranslateNX", "TranslateNX"), L("Lütfen Bekleyin...", "Please Wait..."));
        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::ListItem(L("Çeviri yapılıyor...", "Translating...")));
        frame->setContent(list);
        return frame;
    }
    
    void update() override {
        m_frames++;
        // Ekrana loading arayüzünün çizilmesi için 2 kare bekle
        if (m_frames == 2) {
            // Ana thread üzerinde senkron olarak çalıştır (sysmodule'de std::thread crash'e sebep oluyor)
            doTranslate(std::move(g_screenshotData));
            g_screenshotData.clear();
            
            tsl::goBack(); // LoadingGui'yi kapat
            tsl::changeTo<TranslationResultGui>(); // Sonuclari goster
        }
    }
    
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        return false; // İşlem bitene kadar B'ye basıp çıkmasını engelle
    }
};

class ScreenshotWaitGui : public tsl::Gui {
    int m_frames = 0;
public:
    tsl::elm::Element* createUI() override {
        // Tamamen seffaf element, menuyu gizler
        auto* dummy = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* r, s32, s32, s32, s32){
            r->clearScreen();
        });
        dummy->setBoundaries(0, 0, 1280, 720);
        return dummy;
    }
    
    void update() override {
        m_frames++;
        // Menünün tamamen ekrandan kaymasını beklemek için 40 kare (~0.6 sn) bekle
        if (m_frames == 40) {
            // Ekran tam temizken çekim yap
            auto shot = ScreenshotCapture::capture(65);
            g_screenshotData = std::move(shot.jpegData);
            
            tsl::goBack(); // ScreenshotWaitGui'yi kapat
            tsl::changeTo<LoadingGui>(); // Kullanıcıya yükleniyor ekranını göster
        }
    }
    
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        return false;
    }
};

class TranslateGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(
            "TranslateNX",
            "by SertAy - 1.0.0");

        auto* list = new tsl::elm::List();

        auto* translateBtn = new tsl::elm::ListItem(L("Çeviriye Başla", "Start Translating"));
        translateBtn->setClickListener([](u64 keys) -> bool {
            return false; // handleInput'da islenecek
        });
        list->addItem(translateBtn);

        auto* helpItem = new tsl::elm::ListItem(L("Yardım & Rehber", "Help & Guide"));
        helpItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<HelpGui>();
                return true;
            }
            return false;
        });
        list->addItem(helpItem);
        
        auto* settingsBtn = new tsl::elm::ListItem(L("Ayarlar", "Settings"));
        settingsBtn->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<SettingsGui>();
                return true;
            }
            return false;
        });
        list->addItem(settingsBtn);

        frame->setContent(list);
        return frame;
    }

    void update() override {
        // Removed g_forceClose logic to prevent use-after-free crashes.
    }

    bool handleInput(u64 keysDown, u64,
                     const HidTouchState&,
                     HidAnalogStickState, HidAnalogStickState) override {
        if ((keysDown & HidNpadButton_A) && !g_translating) {
            {
                std::lock_guard<std::mutex> lk(g_resultMutex);
                g_translatedLines.clear();
                g_errorText.clear();
                g_originalText.clear();
                g_ocrWords.clear();
            }
            g_translating = true;
            
            // Önce menüyü gizleyen wait ekranına geç
            tsl::changeTo<ScreenshotWaitGui>(); 
            
            return true;
        }
        if (keysDown & HidNpadButton_Y) {
            tsl::changeTo<SettingsGui>();
            return true;
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// OVERLAY GİRİŞ NOKTASI
// ═══════════════════════════════════════════════════════════════════════════

void reloadOverlay() {
    tsl::swapTo<TranslateGui>(SwapDepth(10));
}

#ifdef TNX_HUD
// ═══════════════════════════════════════════════════════════════════════════
// TranslateNX Live (modification): passive full-screen HUD.
//  - 640x360 framebuffer stretched over the whole screen (see main()).
//  - The game keeps all input; nothing here takes the foreground.
//  - Your Ultrahand launch combo (tsl::cfg::launchCombo) closes this overlay and returns to
//    Ultrahand. Pressing HOME stops everything too (worker, in-flight API calls, buffers).
//  - The worker thread (live.cpp) does the capture / OCR / translation; this class only draws.
// ═══════════════════════════════════════════════════════════════════════════
class TranslateHudGui : public tsl::Gui {
    struct Layout { int fs = 10; int x = 0, y = 0, w = 0, h = 0; int wrapW = 0; };

    bool            m_inputReleased = false;
    bool            m_started       = false;
    u64             m_startTick     = 0;
    std::string     m_setupError;
    uint32_t        m_ver           = 0xFFFFFFFFu;
    Live::Snapshot  m_snap;
    Layout          m_layout[Live::kMaxPatches];
    bool            m_needLayout    = true;

    static void stopAll() { Live::stop(); }   // cancels API calls, joins the worker, wipes buffers

    // Same sequence Ultrahand's Status Monitor uses to quit and land back in Ultrahand.
    static void closeToUltrahand() {
        stopAll();
        ult::launchingOverlay.store(true, std::memory_order_release);
        ult::setIniFileValue(ult::ULTRAHAND_CONFIG_INI_PATH, ult::ULTRAHAND_PROJECT_NAME,
                             ult::IN_OVERLAY_STR, ult::FALSE_STR);
        tsl::setNextOverlay(returnOverlayPath);
        tsl::Overlay::get()->close();
    }

    // HOME: end everything, do not go back to Ultrahand.
    static void closeForHome() {
        stopAll();
        tsl::Overlay::get()->close();
    }

    bool startEngine() {
        Live::Settings st;
        st.useVision = (g_config.ocrApi == OcrApi::GoogleVision);
        st.translateProvider = g_config.translateApi == TranslateApi::DeepL
            ? Live::TranslateProvider::DeepL
            : (g_config.translateApi == TranslateApi::GoogleCloud
                ? Live::TranslateProvider::GoogleCloud : Live::TranslateProvider::MyMemory);
        st.targetLang = g_config.dstLang;
        std::transform(st.targetLang.begin(), st.targetLang.end(), st.targetLang.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        st.debug = g_config.liveDebug;
        st.ocrSpaceKey = g_config.ocrApiKey;
        st.visionKey = g_config.visionApiKey;
        st.deeplKey = g_config.deeplApiKey;
        st.googleKey = g_config.googleTransApiKey;

        if (st.useVision && st.visionKey.empty()) {
            m_setupError = "Google Vision API key is missing";
            return false;
        }
        if (st.translateProvider == Live::TranslateProvider::DeepL && st.deeplKey.empty()) {
            m_setupError = "DeepL API key is missing";
            return false;
        }
        if (st.translateProvider == Live::TranslateProvider::GoogleCloud && st.googleKey.empty()) {
            m_setupError = "Google Cloud API key is missing";
            return false;
        }
        if (!Live::start(st)) { m_setupError = "Could not start live translation (memory/thread error)"; return false; }
        return true;
    }

    void computeLayout(tsl::gfx::Renderer* r) {
        for (int i = 0; i < m_snap.count; ++i) {
            const Live::Patch& p = m_snap.patches[i];
            Layout& L = m_layout[i];
            int wrapW = p.w < 150 ? 150 : p.w;
            if (wrapW > 620) wrapW = 620;
            const int wantH = p.h < 16 ? 16 : p.h;
            int fs = 14;
            std::pair<s32, s32> d{0, 0};
            for (; fs >= 8; --fs) {
                d = r->getTextDimensions(p.text, false, fs, wrapW - 8);
                if (d.second + 4 <= wantH) break;
            }
            if (fs < 8) fs = 8;
            L.fs = fs;
            L.wrapW = wrapW - 8;
            L.w = (d.first + 8 > p.w) ? d.first + 8 : p.w;
            L.h = (d.second + 4 > p.h) ? d.second + 4 : p.h;
            L.x = p.x + p.w / 2 - L.w / 2;
            L.y = p.y + p.h / 2 - L.h / 2;
            if (L.x < 0) L.x = 0;
            if (L.y < 0) L.y = 0;
            if (L.x + L.w > 640) L.x = 640 - L.w;
            if (L.y + L.h > 360) L.y = 360 - L.h;
        }
        m_needLayout = false;
    }

    void drawHud(tsl::gfx::Renderer* r) {
        const u64 elapsedMs = armTicksToNs(armGetSystemTick() - m_startTick) / 1000000ULL;

        if (!m_setupError.empty()) {
            r->drawRect(6, 6, 400, 18, tsl::Color(0, 0, 0, 12));
            r->drawString(m_setupError, false, 10, 19, 10, tsl::Color(15, 8, 8, 15));
            return;
        }

        if (Live::copySnapshot(m_snap, m_ver)) m_needLayout = true;
        if (m_needLayout) computeLayout(r);

        for (int i = 0; i < m_snap.count; ++i) {
            const Live::Patch& p = m_snap.patches[i];
            const Layout& L = m_layout[i];
            const u8 lvl = (u8)(p.lum / 17);                       // 0..15 background level
            const bool lightBg = lvl >= 8;
            // Almost opaque patch in the colour of the surroundings, so the Japanese underneath is hidden.
            r->drawRect(L.x, L.y, L.w, L.h, tsl::Color(lvl, lvl, lvl, 14));
            const tsl::Color fg = lightBg ? tsl::Color(1, 1, 1, 15) : tsl::Color(15, 15, 15, 15);
            r->drawString(p.text, false, L.x + 4, L.y + L.fs + 1, L.fs, fg, L.wrapW);
        }

        // Status: always with live_debug=1, otherwise for a few seconds and whenever something is wrong.
        const char* st = m_snap.status;
        const bool problem = !strncmp(st, "OCR:", 4) || !strncmp(st, "Translate:", 10) ||
                             !strncmp(st, "Capture", 7) || !strncmp(st, "Network", 7);
        if (st[0] && (g_config.liveDebug || problem || elapsedMs < 8000)) {
            r->drawRect(4, 342, 300, 14, tsl::Color(0, 0, 0, 11));
            r->drawString(st, false, 8, 352, 9, tsl::Color(problem ? 15 : 12, problem ? 8 : 12, problem ? 8 : 12, 15));
        }
    }

public:
    TranslateHudGui() : m_startTick(armGetSystemTick()) {}
    ~TranslateHudGui() override { Live::stop(); }

    tsl::elm::Element* createUI() override {
        auto* drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32, s32, s32, s32) {
            r->clearScreen();   // fully transparent, so the game stays visible
            this->drawHud(r);
        });
        drawer->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
        return drawer;
    }

    void update() override {
        if (!m_inputReleased) {
            m_inputReleased = true;
            tsl::disableHiding      = true;   // launch combo / touch-outside must not hide us
            tsl::overrideBackButton = true;   // B belongs to the game, not to the overlay
            tsl::hlp::requestForeground(false); // hand all input back to the game
            m_started = startEngine();
        }
        // libtesla raises this flag when HOME is pressed while hiding is disabled.
        if (tsl::homeButtonPressedInGame.exchange(false, std::memory_order_acq_rel)) {
            closeForHome();
        }
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&,
                     HidAnalogStickState, HidAnalogStickState) override {
        const u64 combo = tsl::cfg::launchCombo;
        if (combo != 0 && (keysHeld & combo) == combo && (keysDown & combo) != 0) {
            closeToUltrahand();
            return true;
        }
        return false;
    }
};


class TranslateHudSetupGui : public tsl::Gui {
    tsl::elm::ListItem* m_ocrItem = nullptr;
    tsl::elm::ListItem* m_translateItem = nullptr;
    tsl::elm::ListItem* m_statusItem = nullptr;

    void update() override {
        if (m_ocrItem) {
            m_ocrItem->setValue(g_config.ocrApi == OcrApi::GoogleVision ? "Google Vision" : "OCR.space");
        }
        if (m_translateItem) {
            const char* value = g_config.translateApi == TranslateApi::DeepL ? "DeepL" :
                (g_config.translateApi == TranslateApi::GoogleCloud ? "Google Cloud" : "MyMemory");
            m_translateItem->setValue(value);
        }
    }

    void startLive() {
        if (g_config.ocrApi == OcrApi::GoogleVision && g_config.visionApiKey.empty()) {
            m_statusItem->setValue("Missing Google Vision key (set it in the NRO)");
            return;
        }
        if (g_config.translateApi == TranslateApi::DeepL && g_config.deeplApiKey.empty()) {
            m_statusItem->setValue("Missing DeepL key (set it in the NRO)");
            return;
        }
        if (g_config.translateApi == TranslateApi::GoogleCloud && g_config.googleTransApiKey.empty()) {
            m_statusItem->setValue("Missing Google Cloud key (set it in the NRO)");
            return;
        }
        m_statusItem->setValue("Starting live translation...");
        tsl::changeTo<TranslateHudGui>();
    }

public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("TranslateNX Live", "Choose providers, then start");
        auto* list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("OCR PROVIDER"));
        m_ocrItem = new tsl::elm::ListItem("OCR API");
        m_ocrItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<OcrApiSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_ocrItem);

        list->addItem(new tsl::elm::CategoryHeader("TRANSLATION PROVIDER"));
        m_translateItem = new tsl::elm::ListItem("Translation API");
        m_translateItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<TranslateApiSelectGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_translateItem);

        auto* startItem = new tsl::elm::ListItem("Start live translation");
        startItem->setClickListener([this](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                startLive();
                return true;
            }
            return false;
        });
        list->addItem(startItem);

        m_statusItem = new tsl::elm::ListItem("Status");
        m_statusItem->setValue("Ready");
        list->addItem(m_statusItem);

        frame->setContent(list);
        return frame;
    }
};

#endif // TNX_HUD

class TranslateOverlay : public tsl::Overlay {
public:
    void initServices() override {
#ifndef TNX_HUD
        HttpClient::init();   // the Live HUD opens its own (smaller) sockets in its worker thread
#endif
        g_config = ConfigManager::load();
    }

    void exitServices() override {
#ifndef TNX_HUD
        HttpClient::cleanup();
#else
        Live::stop();   // safety net: whatever way the overlay ends, the worker and its buffers go with it
#endif
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        // Menü her açıldığında config.ini dosyasını otomatik olarak tekrar oku!
        g_config = ConfigManager::load();
        ult::useHapticFeedback = false; // Titreşimi tamamen kapat

#ifdef TNX_HUD
        return initially<TranslateHudSetupGui>();
#else
        return initially<TranslateGui>();
#endif
    }
};

int main(int argc, char **argv) {
    ult::useHapticFeedback = false;
#ifdef TNX_HUD
    // Half-resolution framebuffer stretched over the whole screen.
    // 640x360 RGBA4444 x 2 buffers = about 0.9 MB, well inside the 4 MB overlay heap.
    ult::DefaultFramebufferWidth  = 640;
    ult::DefaultFramebufferHeight = 360;
    tsl::hudLayerRefWidth  = 640;
    tsl::hudLayerRefHeight = 360;
    tsl::hudSkipDebugStack = true;   // keep the HUD out of the game-only capture stack
#endif
    return tsl::loop<TranslateOverlay>(argc, argv);
}
