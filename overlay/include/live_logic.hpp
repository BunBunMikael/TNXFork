// TranslateNX Live (modification) - pure logic, no libnx / libcurl / libjpeg.
// Kept separate so it can be unit-tested on a normal PC.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace LiveLogic {

// Rectangles are in capture pixels (1280x720).
struct Word  { std::string text; int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
struct Block { std::string text; int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

// true if the UTF-8 text contains kana / kanji / CJK punctuation (Japanese-only filter)
bool hasJapanese(const std::string& utf8);

// Filters to Japanese words only, joins them into lines, then lines into paragraphs.
// maxBlocks / maxBytes bound the output (fixed memory use).
std::vector<Block> groupBlocks(std::vector<Word> words, size_t maxBlocks, size_t maxBytes);

// OCR response parsers. Return false if the JSON is unusable. "no text" is true with 0 words.
bool parseVisionWords(const char* json, std::vector<Word>& out, std::string& err);
bool parseOcrSpaceWords(const char* json, std::vector<Word>& out, std::string& err);

// Translation request bodies / response parsers (JSON, no URL encoding needed).
std::string buildDeepLBody(const std::vector<std::string>& texts);
std::string buildGoogleTranslateBody(const std::vector<std::string>& texts);
bool parseDeepL(const char* json, std::vector<std::string>& out);
bool parseGoogleTranslate(const char* json, std::vector<std::string>& out);

// Streams  prefix + base64(data) + suffix  without ever building the whole string.
class B64Stream {
public:
    B64Stream(const char* prefix, const uint8_t* data, size_t len, const char* suffix);
    size_t totalSize() const { return m_total; }
    size_t read(char* out, size_t cap);   // returns 0 at end
private:
    const char*    m_prefix; size_t m_prefixLen, m_prefixPos = 0;
    const uint8_t* m_data;   size_t m_len, m_dataPos = 0;
    const char*    m_suffix; size_t m_suffixLen, m_suffixPos = 0;
    char m_pend[4]; int m_pendLen = 0, m_pendPos = 0;
    size_t m_total;
};

// 160x90 grayscale thumbnail helpers
constexpr int kThumbW = 160, kThumbH = 90;
// percentage (0..100) of pixels that differ by more than `threshold` levels
float thumbDiffPercent(const uint8_t* a, const uint8_t* b, int threshold = 28);
// mean brightness (0..255) of the ring around a capture-space rectangle
int ringBrightness(const uint8_t* thumb, int x0, int y0, int x1, int y1);

uint64_t hashText(const std::string& s);   // FNV-1a 64
std::string truncateUtf8(const std::string& s, size_t maxBytes);

} // namespace LiveLogic
