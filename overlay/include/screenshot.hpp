#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Screenshot {
    std::vector<uint8_t> jpegData;  // JPEG ham verisi
    int width  = 1280;
    int height = 720;
};

namespace ScreenshotCapture {
    // Ekranın şu anki görüntüsünü JPEG olarak alır.
    // quality: 1-100 arası JPEG kalitesi (60-70 önerilen — RAM tasarrufu)
    // Dönüş: dolu Screenshot → başarılı, boş jpegData → hata
    Screenshot capture(int quality = 65);
}
