# Modifications to TranslateNX

Based on **TranslateNX by SertAy** — https://github.com/sertay1/TranslateNX
Original copyright (c) 2026 SertAy. The original LICENSE is kept unchanged and still applies
(personal / non-commercial use, author credit and a note of modifications required).

## "TranslateNX Live" (untested on hardware)

A second overlay build, `make HUD=1` -> `translatenx_live.ovl`. A plain `make` still builds the original
menu overlay `translatenx.ovl`. The Live build is a passive full-screen HUD:

* Every ~2.5 s it grabs the game picture (RAM only, nothing is written to the SD card), compares a tiny
  160x90 thumbnail with the last translated one, and only when the picture changed (and has settled) it
  sends it to OCR. Unchanged screen = no request.
* OCR (Google Vision or OCR.space) returns the text with positions. Only Japanese text is kept (kana / kanji),
  grouped into lines and paragraphs. DeepL or Google Cloud Translation translate ja -> en.
  English is drawn on a patch over the original text.
* 640x360 framebuffer stretched over the whole screen (about 0.9 MB for the two buffers).
  The game keeps all input; the Ultrahand launch combo closes the overlay and returns to Ultrahand; HOME stops it.
* Closing cancels any running API call, stops the worker thread, and wipes and frees every buffer.
* The screenshot buffer is wiped right after the OCR answer arrives. Upload is streamed (base64 on the fly).

### Files
* `overlay/source/live.cpp`, `overlay/include/live.hpp` — worker thread, capture, thumbnails, networking (libcurl multi).
* `overlay/source/live_logic.cpp`, `overlay/include/live_logic.hpp` — pure logic (Japanese filter, grouping, JSON, base64 stream),
  unit-tested on a PC.
* `overlay/source/main.cpp` — `TranslateHudGui` and `TNX_HUD` switches.
* `overlay/source/config.cpp`, `overlay/include/config.hpp` — `live_debug` key; defaults are now ja -> en.
* `overlay/Makefile` — `HUD=1` (separate target, `-DTNX_HUD=1`, links libjpeg).
* `overlay/lib/libultrahand/libtesla/include/tesla.hpp` — `hudLayerRefWidth/Height` (small framebuffer covering the
  whole screen) and `hudSkipDebugStack` (HUD layer is kept out of the game capture stack). Both are off by default.

### Not changed
The original menu overlay code (Turkish UI etc.) is untouched and still built by a plain `make`.
