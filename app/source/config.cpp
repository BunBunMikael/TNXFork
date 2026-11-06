#include "config.hpp"
#include <cstdio>
#include <cstring>
#include <switch.h>
#include <sys/stat.h>

static const char* CONFIG_PATH = "sdmc:/config/translate/config.ini";

static std::string iniGet(const char* path, const char* key) {
    FILE* f = fopen(path, "r");
    if (!f) return {};
    char line[512];
    std::string k(key);
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        std::string lkey(line, eq - line);
        // trim
        while (!lkey.empty() && (lkey.back() == ' ' || lkey.back() == '\t')) lkey.pop_back();
        if (lkey != k) continue;
        std::string val(eq + 1);
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
        fclose(f);
        return val;
    }
    fclose(f);
    return {};
}

void LoadConfig(AppConfig& cfg) {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        mkdir("sdmc:/config", 0777);
        mkdir("sdmc:/config/translate", 0777);
        f = fopen(CONFIG_PATH, "w");
        if (f) {
            fprintf(f, "ocr_api=vision\n");
            fprintf(f, "translate_api=deepl\n");
            fprintf(f, "src_lang=en\n");
            fprintf(f, "dst_lang=tr\n");
            fprintf(f, "ui_lang=en\n");
            fprintf(f, "ocr_api_key=\n");
            fprintf(f, "vision_api_key=\n");
            fprintf(f, "deepl_api_key=\n");
            fprintf(f, "google_trans_api_key=\n");
            fclose(f);
        }
    } else {
        fclose(f);
    }

    cfg.ocrApiKey        = iniGet(CONFIG_PATH, "ocr_api_key");
    cfg.visionApiKey     = iniGet(CONFIG_PATH, "vision_api_key");
    cfg.deeplApiKey      = iniGet(CONFIG_PATH, "deepl_api_key");
    cfg.googleTransApiKey = iniGet(CONFIG_PATH, "google_trans_api_key");
    cfg.translateApiKey  = iniGet(CONFIG_PATH, "translate_api_key");
    cfg.sourceLang       = iniGet(CONFIG_PATH, "src_lang");
    if (cfg.sourceLang.empty()) cfg.sourceLang = "en";
    cfg.targetLang       = iniGet(CONFIG_PATH, "dst_lang");
    if (cfg.targetLang.empty()) cfg.targetLang = "tr";
    cfg.uiLang           = iniGet(CONFIG_PATH, "ui_lang");
    if (cfg.uiLang.empty()) cfg.uiLang = "en";
}

#include <vector>

void UpdateConfigKey(const char* key, const std::string& value) {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/translate", 0777);
    
    FILE* f = fopen(CONFIG_PATH, "r");
    std::vector<std::string> lines;
    bool found = false;
    std::string targetKey(key);
    std::string targetVal = value;
    if (targetKey == "ui_lang" || targetKey == "src_lang" || targetKey == "dst_lang") {
        for (auto& c : targetVal) c = tolower((unsigned char)c);
    }
    
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            if (!s.empty() && s.back() != '\n') s += '\n';
            char* eq = strchr(line, '=');
            if (eq) {
                std::string lkey(line, eq - line);
                while (!lkey.empty() && (lkey.back() == ' ' || lkey.back() == '\t')) lkey.pop_back();
                if (lkey == targetKey) {
                    lines.push_back(targetKey + "=" + targetVal + "\n");
                    found = true;
                    continue;
                }
            }
            lines.push_back(s);
        }
        fclose(f);
    }
    
    if (!found) {
        lines.push_back(targetKey + "=" + targetVal + "\n");
    }
    
    f = fopen(CONFIG_PATH, "w");
    if (f) {
        for (const auto& l : lines) {
            fprintf(f, "%s", l.c_str());
        }
        fclose(f);
    }
}
