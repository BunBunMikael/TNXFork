#include "screenshot.hpp"
#include <switch.h>
#include <cstdlib>

namespace ScreenshotCapture {

Screenshot capture(int /*quality*/) {
    Screenshot result;

    // Ekran görüntüsü servisini başlat (Screen Capture Service)
    Result rc = capsscInitialize();
    if (R_FAILED(rc)) {
        return result;
    }

    // JPEG buffer için 512 KB alan ayıralım (1.5 MB sysmodule heap'i için çok büyüktü, OOM/2168-0002 crashine yol açar)
    size_t bufSize = 512 * 1024;
    result.jpegData.resize(bufSize);

    u64 jpegSize = 0;
    // 5 saniye timeout. Önce sadece oyunu çekmeyi deneriz (ApplicationForDebug)
    rc = capsscCaptureJpegScreenShot(&jpegSize, result.jpegData.data(), bufSize, ViLayerStack_ApplicationForDebug, 5000000000ULL);
    
    // Eğer sadece oyunu çekmek başarısız olursa veya boş dönerse, Default (Tüm katmanlar) ile tekrar deneriz
    if (R_FAILED(rc) || jpegSize == 0) {
        jpegSize = 0;
        rc = capsscCaptureJpegScreenShot(&jpegSize, result.jpegData.data(), bufSize, ViLayerStack_Default, 5000000000ULL);
    }
    
    if (R_FAILED(rc) || jpegSize == 0) {
        result.jpegData.clear();
        capsscExit();
        return result;
    }

    result.jpegData.resize(jpegSize);
    result.width  = 1280;
    result.height = 720;

    capsscExit();
    return result;
}

} // namespace ScreenshotCapture
