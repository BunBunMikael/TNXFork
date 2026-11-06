#pragma once
#include <string>
#include <vector>

struct TranslateResult {
    bool        success = false;
    std::string translatedText;
    std::vector<std::string> translatedLines;
    std::string errorMsg;
};

namespace Translate {
    TranslateResult runMyMemory(const std::vector<std::string>& lines,
                            const std::string& langPair = "ja|tr");

    TranslateResult runDeepL(const std::vector<std::string>& lines,
                               const std::string& apiKey,
                               const std::string& sourceLang,
                               const std::string& targetLang);

    TranslateResult runGoogleCloud(const std::vector<std::string>& lines,
                               const std::string& apiKey,
                               const std::string& sourceLang,
                               const std::string& targetLang);
}
