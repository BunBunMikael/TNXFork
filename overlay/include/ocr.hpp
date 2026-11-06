#pragma once
#include <string>
#include <vector>

struct OcrWord {
    std::string text;
    // normalised koordinatlar [0..1]
    float x = 0, y = 0, w = 0, h = 0;
};

struct OcrResult {
    bool              success = false;
    std::string       fullText;          // tüm metin (çeviri için)
    std::vector<OcrWord> words;          // kelime bazlı (ileride koordinat overlay için)
    std::string       errorMsg;
};

namespace Ocr {
    OcrResult runOcrSpace(std::vector<uint8_t> jpegData,
                      const std::string& apiKey = "",
                      const std::string& lang = "jpn");

    OcrResult runGoogleVision(std::vector<uint8_t> jpegData,
                         const std::string& apiKey,
                         const std::string& lang = "ja");
}
