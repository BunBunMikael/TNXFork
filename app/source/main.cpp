#include <switch.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string>
#include <map>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "config.hpp"
#include "logo_data.h"
#include "qr_data.h"
#include "help_texts.h"

// -------------------------------------------------------
// DIL KODLARI & ISIMLERI (UTF-8)
// -------------------------------------------------------
static const int  NUM_LANGS  = 2;
static const char* LC[]      = {
    "TR","EN"
};

static const char* LANG_NAMES[] = {
    u8"Türkçe", u8"English"
};

static const char* getLangName(const std::string& uiLang, int idx) {
    (void)uiLang; 
    if (idx < 0 || idx >= NUM_LANGS) return "?";
    return LANG_NAMES[idx]; 
}

// -------------------------------------------------------
// CEVIRI SOZLUGU
// -------------------------------------------------------
static std::map<std::string,std::map<std::string,std::string>> T;

static void initT() {
    T["TR"]={{"k0",u8"API Anahtarları"},{"k1",u8"Dil Ayarları"},{"k2",u8"Rehber & Yardım"}, {"k3",u8"İletişim"},
              {"c_title", u8"Tüm linkler"}, {"c_desc", u8"Yapımcı ile iletişim kurmak ve linklerine ulaşmak için\naşağıdaki QR kodu okutabilirsiniz."},
              {"ocr",u8"OCR Servisleri"},{"tr2",u8"Çeviri Servisleri"},
              {"emp",u8"(Boş) - düzenlemek için [A]"},{"uil",u8"Arayüz Dili"},
              {"ft",u8"[A] Seç   [L/R] Menü Geçişi   [Yukarı/Aşağı] Kaydır   [+] Çıkış"}};
              
    T["EN"]={{"k0",u8"API Keys"},{"k1",u8"Language Settings"},{"k2",u8"Guide & Help"}, {"k3",u8"Contact"},
              {"c_title", u8"All Links"}, {"c_desc", u8"You can scan the QR code below to contact the developer\nand access their links."},
              {"ocr",u8"OCR Services"},{"tr2",u8"Translation Services"},
              {"emp",u8"(Empty) - press [A] to edit"},{"uil",u8"UI Language"},
              {"ft",u8"[A] Select   [L/R] Menu Tab   [Up/Down] Scroll   [+] Exit"}};

    initHelpTexts();
    for (auto& pair : HELP_TEXTS) {
        T[pair.first]["hlp"] = pair.second;
    }
}

static const char* Tr(const char* k, const std::string& lang) {
    auto it = T.find(lang);
    if (it==T.end()) it=T.find("EN");
    if (it!=T.end()) { auto s=it->second.find(k); if(s!=it->second.end()) return s->second.c_str(); }
    auto en=T.find("EN");
    if (en!=T.end()) { auto s=en->second.find(k); if(s!=en->second.end()) return s->second.c_str(); }
    return k;
}

// -------------------------------------------------------
// KLAVYE
// -------------------------------------------------------
static std::string openKeyboard(const std::string& cur, const char* guide) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd,0))) return cur;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetInitialText(&kbd, cur.c_str());
    swkbdConfigSetGuideText(&kbd, guide);
    char buf[512]={0};
    Result rc=swkbdShow(&kbd,buf,sizeof(buf));
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc) ? std::string(buf) : cur;
}

// -------------------------------------------------------
// MAIN
// -------------------------------------------------------
int main(int argc, char* argv[]) {
    plInitialize(PlServiceType_User);
    initT(); 

    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_GAMECONTROLLER);

    SDL_Window* win = SDL_CreateWindow("TranslateNX",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { plExit(); return -1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win,-1,
        SDL_RENDERER_PRESENTVSYNC|SDL_RENDERER_ACCELERATED);
    if (!ren) { SDL_DestroyWindow(win); plExit(); return -1; }

    SDL_RenderSetLogicalSize(ren, 1280, 720);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO(); (void)io;

    // --- FONT YUKLEME ---
    PlFontData fntStd, fntChs, fntKor;
    ImFontConfig fc; 
    fc.FontDataOwnedByAtlas = false;
    
    if (R_SUCCEEDED(plGetSharedFontByType(&fntStd, PlSharedFontType_Standard)) && fntStd.address) {
        io.Fonts->AddFontFromMemoryTTF(fntStd.address, fntStd.size, 26.0f, &fc, io.Fonts->GetGlyphRangesDefault());
        fc.MergeMode = true; 
        static const ImWchar ext_ranges[] = { 0x0100, 0x04FF, 0 }; 
        io.Fonts->AddFontFromMemoryTTF(fntStd.address, fntStd.size, 26.0f, &fc, ext_ranges);
        io.Fonts->AddFontFromMemoryTTF(fntStd.address, fntStd.size, 26.0f, &fc, io.Fonts->GetGlyphRangesJapanese());
        if (R_SUCCEEDED(plGetSharedFontByType(&fntChs, PlSharedFontType_ChineseSimplified)) && fntChs.address) {
            io.Fonts->AddFontFromMemoryTTF(fntChs.address, fntChs.size, 26.0f, &fc, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
        if (R_SUCCEEDED(plGetSharedFontByType(&fntKor, PlSharedFontType_KO)) && fntKor.address) {
            io.Fonts->AddFontFromMemoryTTF(fntKor.address, fntKor.size, 26.0f, &fc, io.Fonts->GetGlyphRangesKorean());
        }
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& sty=ImGui::GetStyle();
    sty.WindowRounding=0; sty.ChildRounding=6; sty.FrameRounding=5;
    sty.WindowBorderSize=0; sty.ChildBorderSize=1;
    sty.FramePadding=ImVec2(10,8); sty.ItemSpacing=ImVec2(10,8);
    sty.Colors[ImGuiCol_WindowBg]      =ImVec4(0.07f,0.07f,0.09f,1);
    sty.Colors[ImGuiCol_ChildBg]       =ImVec4(0.09f,0.09f,0.12f,1);
    sty.Colors[ImGuiCol_Border]        =ImVec4(0.20f,0.20f,0.28f,1);
    sty.Colors[ImGuiCol_Button]        =ImVec4(0.10f,0.10f,0.13f,1);
    sty.Colors[ImGuiCol_ButtonHovered] =ImVec4(0.18f,0.36f,0.68f,1);
    sty.Colors[ImGuiCol_ButtonActive]  =ImVec4(0.14f,0.40f,0.76f,1);
    sty.Colors[ImGuiCol_ScrollbarBg]   =ImVec4(0.07f,0.07f,0.09f,1);
    sty.Colors[ImGuiCol_ScrollbarGrab] =ImVec4(0.20f,0.40f,0.70f,1);

    ImGui_ImplSDL2_InitForSDLRenderer(win,ren);
    ImGui_ImplSDLRenderer2_Init(ren);

    AppConfig cfg;
    LoadConfig(cfg);
    if (cfg.uiLang.empty()) cfg.uiLang="EN";
    int uiIdx=1;
    for(int i=0;i<NUM_LANGS;i++) if(cfg.uiLang==LC[i]){uiIdx=i;break;}

    SDL_Surface* lsurf=SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)logo_pixels,logo_width,logo_height,32,logo_width*4,SDL_PIXELFORMAT_RGBA32);
    SDL_Texture* logoTex=nullptr;
    if(lsurf){logoTex=SDL_CreateTextureFromSurface(ren,lsurf);SDL_FreeSurface(lsurf);}

    SDL_Surface* qrsurf=SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)qr_pixels,qr_width,qr_height,32,qr_width*4,SDL_PIXELFORMAT_RGBA32);
    SDL_Texture* qrTex=nullptr;
    if(qrsurf){qrTex=SDL_CreateTextureFromSurface(ren,qrsurf);SDL_FreeSurface(qrsurf);}


    const float W=1280,H=720;
    const float SBW=360;          
    const float FTH=46;           
    const float CTH=H-FTH;        
    const float CTW=W-SBW;        

    int  tab    = 0;       
    int  focus  = 0;       
    bool done   = false;
    
    float tab2_scroll_delta = 0.0f; // Kaydirma icin delta degeri

    int stick_ly = 0;
    int stick_ry = 0;
    bool dpad_up = false;
    bool dpad_down = false;
    Uint32 last_focus_move = 0;


    auto itemCount=[&]()->int{
        if(tab==0) return 4; 
        if(tab==1) return NUM_LANGS;
        return 0;
    };

    while(!done) {
        SDL_Event ev;
        while(SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if(ev.type==SDL_QUIT) done=true;
            if(ev.type==SDL_CONTROLLERBUTTONDOWN) {
                int btn=ev.cbutton.button;
                if(btn==SDL_CONTROLLER_BUTTON_START) done=true;
                if(btn==SDL_CONTROLLER_BUTTON_LEFTSHOULDER){
                    tab=(tab-1+4)%4; focus=0;
                }
                if(btn==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){
                    tab=(tab+1)%4; focus=0;
                }
                if(btn==SDL_CONTROLLER_BUTTON_DPAD_UP){
                    if(tab != 2 && tab != 3) { focus--; if(focus<0) focus=itemCount()-1; last_focus_move = SDL_GetTicks(); }
                }
                if(btn==SDL_CONTROLLER_BUTTON_DPAD_DOWN){
                    if(tab != 2 && tab != 3) { focus++; if(focus>=itemCount()) focus=0; last_focus_move = SDL_GetTicks(); }
                }
                if(btn==SDL_CONTROLLER_BUTTON_B){ 
                    if(tab==0){
                        std::string* keys[]={&cfg.ocrApiKey,&cfg.visionApiKey,
                                              &cfg.deeplApiKey,&cfg.googleTransApiKey};
                        const char* labels[]={"OCR.Space","Google Vision","DeepL","Google Cloud"};
                        if(focus>=0 && focus<4){
                            std::string nk=openKeyboard(*keys[focus],labels[focus]);
                            if(nk!=*keys[focus]){
                            *keys[focus]=nk;
                            const char* ini_keys[]={"ocr_api_key","vision_api_key","deepl_api_key","google_trans_api_key"};
                            UpdateConfigKey(ini_keys[focus], nk);
                        }
                        }
                    }
                    if(tab==1){
                        if(focus>=0 && focus<NUM_LANGS){
                            uiIdx=focus;
                            cfg.uiLang=LC[focus];
                            UpdateConfigKey("ui_lang", cfg.uiLang);
                        }
                    }
                }
            }
            if(ev.type==SDL_CONTROLLERAXISMOTION){
                if(ev.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY) stick_ly = ev.caxis.value;
                if(ev.caxis.axis==SDL_CONTROLLER_AXIS_RIGHTY) stick_ry = ev.caxis.value;
            }
            if(ev.type==SDL_CONTROLLERBUTTONDOWN || ev.type==SDL_CONTROLLERBUTTONUP){
                bool pressed = (ev.type==SDL_CONTROLLERBUTTONDOWN);
                int btn = ev.cbutton.button;
                if(btn==SDL_CONTROLLER_BUTTON_DPAD_UP) dpad_up = pressed;
                if(btn==SDL_CONTROLLER_BUTTON_DPAD_DOWN) dpad_down = pressed;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (tab != 2 && tab != 3) {
            if (now - last_focus_move > 150) {
                if (stick_ly < -20000 || stick_ry < -20000) {
                    focus--; if(focus<0) focus=itemCount()-1;
                    last_focus_move = now;
                } else if (stick_ly > 20000 || stick_ry > 20000) {
                    focus++; if(focus>=itemCount()) focus=0;
                    last_focus_move = now;
                }
            }
        }
        if (tab == 2 || tab == 3) {
            float scroll_speed = 0.0f;
            if (stick_ly < -10000) scroll_speed = -25.0f * (stick_ly / -32768.0f);
            else if (stick_ly > 10000) scroll_speed = 25.0f * (stick_ly / 32767.0f);
            
            if (stick_ry < -10000) scroll_speed = -25.0f * (stick_ry / -32768.0f);
            else if (stick_ry > 10000) scroll_speed = 25.0f * (stick_ry / 32767.0f);
            
            if (dpad_up) scroll_speed = -25.0f;
            if (dpad_down) scroll_speed = 25.0f;
            
            tab2_scroll_delta += scroll_speed;
        }

        int maxFocus=itemCount()-1;
        if(maxFocus<0) maxFocus=0;
        if(focus>maxFocus) focus=maxFocus;
        if(focus<0) focus=0;

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(W,H));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::Begin("##R",NULL,
            ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus|
            ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|
            ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar();

        // SIDEBAR
        {
            ImDrawList* dl=ImGui::GetWindowDrawList();
            dl->AddRectFilled({0,0},{SBW,H},IM_COL32(10,10,14,255));
            dl->AddLine({SBW,0},{SBW,H},IM_COL32(30,30,44,255),1.5f);

            float lw=220, lh=lw*((float)logo_height/logo_width);
            float lx=(SBW-lw)/2.0f, ly=24;
            if(logoTex) dl->AddImage((ImTextureID)logoTex,{lx,ly},{lx+lw,ly+lh});

            const char* tnames[]={Tr("k0",cfg.uiLang),Tr("k1",cfg.uiLang),Tr("k2",cfg.uiLang),Tr("k3",cfg.uiLang)};
            float ty=ly+lh+24;
            for(int i=0;i<4;i++){
                float tx=10,tw=SBW-20,th=42;
                if(tab==i) dl->AddRectFilled({tx,ty},{tx+tw,ty+th},IM_COL32(35,82,168,255),6.f);
                ImU32 tc=(tab==i)?IM_COL32(255,255,255,255):IM_COL32(125,125,145,255);
                dl->AddText({tx+16,ty+12},tc,tnames[i]);
                ty+=th+5;
            }
            
            const char* byText = "by SertAy";
            ImVec2 bySz = ImGui::CalcTextSize(byText);
            dl->AddText({(SBW-bySz.x)/2.0f, CTH - bySz.y - 15.0f}, IM_COL32(80, 160, 255, 255), byText);
        }

        // ICERIK
        float cx=SBW+24,cy=18,cw=CTW-48;
        ImGui::SetCursorPos({cx,cy});
        ImGui::BeginGroup();

        if(tab==0) {
            ImGui::TextColored({0.85f,0.85f,1.0f,1},"  %s",Tr("k0",cfg.uiLang));
            ImGui::SetCursorPosX(cx);
            ImGui::Separator();
            ImGui::Spacing();

            struct Field {const char* label; std::string* key;};
            Field fields[]={
                {"OCR.Space",    &cfg.ocrApiKey},
                {"Google Vision",&cfg.visionApiKey},
                {"DeepL",        &cfg.deeplApiKey},
                {"Google Cloud", &cfg.googleTransApiKey}
            };

            ImGui::SetCursorPosX(cx);
            ImGui::TextColored({1.0f, 0.65f, 0.2f, 1.0f},"  %s",Tr("ocr",cfg.uiLang));
            ImGui::Spacing();
            for(int fi=0;fi<2;fi++) {
                ImGui::SetCursorPosX(cx);
                ImGui::TextColored({0.2f, 0.85f, 0.9f, 1.0f},"  %s:",fields[fi].label);

                bool isFocused=(focus==fi);
                char disp[512];
                snprintf(disp,sizeof(disp),"  %s",
                    fields[fi].key->empty()?Tr("emp",cfg.uiLang):fields[fi].key->c_str());

                ImGui::SetCursorPosX(cx);
                if(isFocused){
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.14f,0.40f,0.76f,1});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.18f,0.46f,0.82f,1});
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.09f,0.09f,0.12f,1});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.09f,0.09f,0.12f,1});
                }
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0.01f,0.5f});
                ImGui::PushID(fi);
                if(ImGui::Button(disp,{cw,44})){
                    std::string nk=openKeyboard(*fields[fi].key,fields[fi].label);
                    if(nk!=*fields[fi].key){
                    *fields[fi].key=nk;
                    const char* ini_keys[]={"ocr_api_key","vision_api_key","deepl_api_key","google_trans_api_key"};
                    UpdateConfigKey(ini_keys[fi], nk);
                }
                }
                ImGui::PopID();
                ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
                ImGui::Spacing();
            }

            ImGui::SetCursorPosX(cx); ImGui::Spacing();
            ImGui::SetCursorPosX(cx);
            ImGui::TextColored({1.0f, 0.65f, 0.2f, 1.0f},"  %s",Tr("tr2",cfg.uiLang));
            ImGui::Spacing();
            for(int fi=2;fi<4;fi++) {
                ImGui::SetCursorPosX(cx);
                ImGui::TextColored({0.2f, 0.85f, 0.9f, 1.0f},"  %s:",fields[fi].label);

                bool isFocused=(focus==fi);
                char disp[512];
                snprintf(disp,sizeof(disp),"  %s",
                    fields[fi].key->empty()?Tr("emp",cfg.uiLang):fields[fi].key->c_str());

                ImGui::SetCursorPosX(cx);
                if(isFocused){
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.14f,0.40f,0.76f,1});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.18f,0.46f,0.82f,1});
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.09f,0.09f,0.12f,1});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.09f,0.09f,0.12f,1});
                }
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0.01f,0.5f});
                ImGui::PushID(fi+10);
                if(ImGui::Button(disp,{cw,44})){
                    std::string nk=openKeyboard(*fields[fi].key,fields[fi].label);
                    if(nk!=*fields[fi].key){
                    *fields[fi].key=nk;
                    const char* ini_keys[]={"ocr_api_key","vision_api_key","deepl_api_key","google_trans_api_key"};
                    UpdateConfigKey(ini_keys[fi], nk);
                }
                }
                ImGui::PopID();
                ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
                ImGui::Spacing();
            }
        }

        else if(tab==1) {
            ImGui::TextColored({0.85f,0.85f,1.0f,1},"  %s",Tr("k1",cfg.uiLang));
            ImGui::SetCursorPosX(cx);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::SetCursorPosX(cx);
            ImGui::TextColored({0.55f,0.68f,0.90f,1},"  %s:",Tr("uil",cfg.uiLang));
            ImGui::Spacing();

            ImGui::SetCursorPosX(cx);
            ImGui::BeginChild("##LL",{cw,CTH-110},true,
                ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings);

            for(int n=0;n<NUM_LANGS;n++){
                bool isSel=(uiIdx==n);
                bool isFoc=(focus==n);
                ImGui::PushID(n);
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0.04f,0.5f});
                char lname[256];
                snprintf(lname,sizeof(lname),"  %s",getLangName(cfg.uiLang,n));
                if(isFoc){
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.14f,0.40f,0.76f,1.f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.18f,0.46f,0.82f,1.f});
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.09f,0.09f,0.12f,1.f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.12f,0.12f,0.16f,1.f});
                }
                
                // Indication for selected lang
                if(isSel) {
                    snprintf(lname,sizeof(lname),"  [X] %s",getLangName(cfg.uiLang,n));
                }
                if(ImGui::Button(lname,{-FLT_MIN,42})){
                    uiIdx=n; cfg.uiLang=LC[n]; UpdateConfigKey("ui_lang", cfg.uiLang); focus=n;
                }
                ImGui::PopStyleColor(2); ImGui::PopStyleVar(); ImGui::PopID();
                if(isFoc) ImGui::SetScrollHereY(0.3f);
            }
            ImGui::EndChild();
        }

        else if(tab==2) {
            ImGui::TextColored({0.85f,0.85f,1.0f,1},"  %s",Tr("k2",cfg.uiLang));
            ImGui::SetCursorPosX(cx);
            ImGui::Separator();
            
            // Yardim metni cok uzun oldugu icin BeginChild kullaniyoruz!
            ImGui::BeginChild("##HelpChild", {cw, CTH-110}, true, ImGuiWindowFlags_NoNav);
            
            if (tab2_scroll_delta != 0.0f) {
                ImGui::SetScrollY(ImGui::GetScrollY() + tab2_scroll_delta);
                tab2_scroll_delta = 0.0f; // Sifirla
            }
            
            const char* hlpText = Tr("hlp",cfg.uiLang);
            if (hlpText) {
                std::string text(hlpText);
                size_t pos = 0;
                while (pos < text.length()) {
                    size_t end = text.find('\n', pos);
                    if (end == std::string::npos) end = text.length();
                    std::string line = text.substr(pos, end - pos);
                    
                    if (line.empty()) {
                        ImGui::Spacing();
                    } else if (line.compare(0, 4, "### ") == 0) {
                        ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "%s", line.c_str() + 4); // Yesil Baslik
                        ImGui::Separator();
                    } else if (line.compare(0, 3, "## ") == 0) {
                        ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "%s", line.c_str() + 3); // Mavi Alt Baslik
                    } else {
                        ImGui::TextWrapped("%s", line.c_str());
                    }
                    pos = end + 1;
                }
            }
            
            ImGui::EndChild();
        }

        
        else if(tab==3) {
            ImGui::TextColored({0.85f,0.85f,1.0f,1},"  %s",Tr("k3",cfg.uiLang));
            ImGui::SetCursorPosX(cx);
            ImGui::Separator();
            
            ImGui::BeginChild("##ContactChild", {cw, CTH-110}, true, ImGuiWindowFlags_NoNav);
            if (tab2_scroll_delta != 0.0f) {
                ImGui::SetScrollY(ImGui::GetScrollY() + tab2_scroll_delta);
                tab2_scroll_delta = 0.0f;
            }
            
            ImGui::Spacing(); ImGui::Spacing();
            
            const char* title = Tr("c_title", cfg.uiLang);
            ImVec2 title_sz = ImGui::CalcTextSize(title);
            ImGui::SetCursorPosX((cw - title_sz.x) * 0.5f);
            ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "%s", title);
            
            ImGui::Spacing(); ImGui::Spacing();
            
            const char* desc = Tr("c_desc", cfg.uiLang);
            std::string text(desc);
            size_t pos = 0;
            while (pos < text.length()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.length();
                std::string line = text.substr(pos, end - pos);
                ImVec2 line_sz = ImGui::CalcTextSize(line.c_str());
                ImGui::SetCursorPosX((cw - line_sz.x) * 0.5f);
                ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "%s", line.c_str());
                pos = end + 1;
            }
            
            ImGui::Spacing(); ImGui::Spacing();
            
            if (qrTex) {
                float qr_w = 200.0f;
                float qr_h = 200.0f;
                ImGui::SetCursorPosX((cw - qr_w) * 0.5f);
                ImGui::Image((ImTextureID)qrTex, {qr_w, qr_h});
            }
            
            ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
            
            const char* socials[] = {
                "İnstagram: @cilsertay",
                "Tiktok: @sertayintardis",
                "Youtube: @SertAyyy",
                "Discord: sertay",
                "Discord Server: 2q9WVb64Kx"
            };
            
            for(int i=0; i<5; i++) {
                ImVec2 sz = ImGui::CalcTextSize(socials[i]);
                ImGui::SetCursorPosX((cw - sz.x) * 0.5f);
                ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "%s", socials[i]);
            }
            
            ImGui::EndChild();
        }

        ImGui::EndGroup();

        // FOOTER
        {
            ImDrawList* dl=ImGui::GetWindowDrawList();
            dl->AddRectFilled({0,CTH},{W,H},IM_COL32(8,8,11,255));
            dl->AddLine({0,CTH},{W,CTH},IM_COL32(30,30,44,255),1.5f);
            const char* ft=Tr("ft",cfg.uiLang);
            ImVec2 ts=ImGui::CalcTextSize(ft);
            float fx=W-ts.x-24,fy=CTH+(FTH-ts.y)*0.5f;
            dl->AddText({fx,fy},IM_COL32(115,115,135,255),ft);
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(ren,10,10,14,255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),ren);
        SDL_RenderPresent(ren);
    }

    if(logoTex) SDL_DestroyTexture(logoTex);
    if(qrTex) SDL_DestroyTexture(qrTex);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    plExit();
    return 0;
}
