#pragma once
#include <string>

struct AppConfig {
    std::string ocrApiKey;
    std::string visionApiKey;
    std::string deeplApiKey;
    std::string googleTransApiKey;
    std::string translateApiKey; // legacy but good to preserve
    std::string sourceLang;
    std::string targetLang;
    std::string uiLang;
};

void LoadConfig(AppConfig& cfg);
void UpdateConfigKey(const char* key, const std::string& value);
