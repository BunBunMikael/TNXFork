// TranslateNX Live (modification) - background engine API used by the HUD.
#pragma once
#include <cstdint>
#include <string>

namespace Live {

constexpr int kMaxPatches = 8;
constexpr int kTextMax    = 224;     // bytes per translated block (fixed buffers, no growth)

// One translated block, already in HUD layer pixels (640x360).
struct Patch {
    int16_t x = 0, y = 0, w = 0, h = 0;   // area covering the original Japanese text
    uint8_t lum = 0;                      // brightness (0..255) of the area around it
    char    text[kTextMax] = {0};         // English
};

struct Snapshot {
    uint32_t version = 0;
    int      count   = 0;
    Patch    patches[kMaxPatches];
    char     status[96] = {0};
};

enum class TranslateProvider { MyMemory, DeepL, GoogleCloud };

struct Settings {
    bool        useVision = true;       // false = OCR.space
    bool        debug = false;          // show heap usage in the status line
    TranslateProvider translateProvider = TranslateProvider::DeepL;
    std::string targetLang = "en";
    std::string ocrSpaceKey, visionKey, deeplKey, googleKey;
};

bool start(const Settings& s);   // spawns the worker; false if buffers / thread could not be created
void stop();                     // cancels in-flight requests, joins, frees and wipes every buffer
bool running();

// Copies the newest state. Returns true when it differs from what `lastVersion` saw.
bool copySnapshot(Snapshot& out, uint32_t& lastVersion);

} // namespace Live
