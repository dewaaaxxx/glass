#pragma once

#include <Vector/Vectors.h>
#include <imgui/imgui.h>

#include "resources.h"

using namespace ImGui;
using namespace std;

#include "include/includes.h"

#include "8bp.h"
#include "8bp/Ruleset.h"
#include "imgui/inc/8bp.h"
#include "keylogin.h"
#include "oxorany/oxorany.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

struct MenuState {
    bool isOpen = false;
    int currentTab = 0;
    float animProgress = 0.0f;
    float menuAlpha = 0.0f;
};
static MenuState g_menu;

static float EaseOutBack(float x) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(x - 1.0f, 3.0f) + c1 * powf(x - 1.0f, 2.0f);
}

static float EaseOutQuart(float x) {
    return 1.0f - powf(1.0f - x, 4.0f);
}

static void DrawGradientRect(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 col1, ImU32 col2, bool horizontal = true) {
    if (horizontal) {
        dl->AddRectFilledMultiColor(p1, p2, col1, col2, col2, col1);
    } else {
        dl->AddRectFilledMultiColor(p1, p2, col1, col1, col2, col2);
    }
}

static bool BigToggle(const char* label, bool* v) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    // --- UKURAN DIKECILKAN ---
    float toggleW = 140.0f; // Dulu 180 -> jadi 140
    float toggleH = 50.0f;  // Dulu 80 -> jadi 50
    float rowH = 70.0f;     // Dulu 120 -> jadi 70
    
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(GetContentRegionAvail().x, rowH);

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) *v = !*v;

    static std::map<ImGuiID, float> toggleAnim;
    float& animT = toggleAnim[id];
    float targetT = *v ? 1.0f : 0.0f;
    animT += (targetT - animT) * g.IO.DeltaTime * 12.0f;

    ImDrawList* dl = window->DrawList;
    
    float toggleX = bb.Min.x + 20.0f;
    float toggleY = bb.Min.y + (rowH - toggleH) * 0.5f;
    ImVec2 toggleMin = ImVec2(toggleX, toggleY);
    ImVec2 toggleMax = ImVec2(toggleX + toggleW, toggleY + toggleH);
    float radius = toggleH * 0.5f;
    
    ImU32 bgColor = *v ? IM_COL32(100, 180, 255, 255) : IM_COL32(80, 80, 80, 255);
    dl->AddRectFilled(toggleMin, toggleMax, bgColor, radius);
    dl->AddRect(toggleMin, toggleMax, IM_COL32(255, 255, 255, 150), radius, 0, 2.0f); // Border tipis
    
    float knobRadius = radius - 5.0f; // Knob lebih kecil sedikit
    float knobX = toggleX + radius + (toggleW - toggleH) * animT;
    float knobY = toggleY + radius;
    dl->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, IM_COL32(255, 255, 255, 255));
    
    // --- TEKS DI SEBELAH KANAN TOGGLE ---
    SetWindowFontScale(0.95f); // Font sedikit lebih kecil
    ImVec2 textSize = CalcTextSize(label);
    float availWidth = GetContentRegionAvail().x;
    float textX = bb.Min.x + availWidth - textSize.x - 40.0f;  // Geser sedikit ke kiri
    float textY = bb.Min.y + (rowH - textSize.y) * 0.5f;
    dl->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), label);
    SetWindowFontScale(1.0f);

    return pressed;
}

INLINE void DrawAutoQueue() {
    if (!g_Token.empty() && !g_Auth.empty() && g_Token == g_Auth) {
        static std::chrono::steady_clock::time_point last_call_time;
        static std::chrono::steady_clock::time_point countdown_start;
        static bool counting = false;

        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_call_time).count() > 500) {
            counting = false;
        }
        last_call_time = now;

        if (!counting) {
            counting = true;
            countdown_start = now;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - countdown_start).count();
        int remaining_ms = 3000 - elapsed;

        if (remaining_ms <= 0) {
            if (sharedMenuManager.getMenuStateId() == 13) PopMenuState(13);
            StartLastMatch();
            counting = false;
            return;
        }

        SetNextWindowPos(ImVec2(Width / 2.0f, Height / 2.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        SetNextWindowSize(ImVec2(360, 260), ImGuiCond_Always);
        PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 0.98f));
        PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);

        if (Begin(O("##AutoQueue"), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            ImDrawList* dl = GetWindowDrawList();
            ImVec2 winPos = GetWindowPos();
            ImVec2 winSize = GetWindowSize();
            
            DrawGradientRect(dl, winPos, ImVec2(winPos.x + winSize.x, winPos.y + 70), IM_COL32(40, 100, 180, 255), IM_COL32(60, 140, 200, 255), true);
            dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + 20), IM_COL32(40, 100, 180, 255), 20.0f, ImDrawFlags_RoundCornersTop);
            
            ImVec2 titleSize = CalcTextSize(O("Auto Queue"));
            dl->AddText(ImVec2(winPos.x + (winSize.x - titleSize.x) * 0.5f, winPos.y + 22), IM_COL32(255, 255, 255, 255), O("Auto Queue"));

            SetCursorPosY(90);
            float font_scale = 3.5f;
            SetWindowFontScale(font_scale);

            std::string count_str = std::to_string((remaining_ms / 1000) + 1);
            auto text_size = CalcTextSize(count_str.c_str());
            SetCursorPosX((winSize.x - text_size.x) * 0.5f);
            TextColored(ImVec4(0.35f, 0.7f, 1.0f, 1.0f), "%s", count_str.c_str());

            SetWindowFontScale(1.0f);

            SetCursorPosY(winSize.y - 75);
            SetCursorPosX(25);
            PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
            PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
            PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            
            if (Button(O("Cancel"), ImVec2(winSize.x - 50, 50))) {
                persistent_bool[O("bAutoQueue")] = false;
                counting = false;
            }
            
            PopStyleVar();
            PopStyleColor(2);
            End();
        }
        PopStyleVar();
        PopStyleColor();
    }
}

INLINE void DrawESP(ImDrawList* draw) {
    if (!g_Token.empty() && !g_Auth.empty() && g_Token == g_Auth) {
        if (!sharedGameManager) return;

        UpdateScreenTable();

        sharedDirector = F(ptr, libmain + O(0x4f06288));
        if (!sharedDirector) return;

        sharedUserInfo = F(ptr, libmain + O(0x4e9feb8));
        if (!sharedUserInfo) return;

        F(bool, sharedUserInfo + 0x340) = true;

        sharedMainManager = F(ptr, libmain + O(0x4dde3e0));
        if (!sharedMainManager) return;

        sharedMenuManager = F(ptr, libmain + O(0x4dfe838));
        if (!sharedMenuManager) return;

        MainStateManager mainStateManager = sharedMainManager.mStateManager;
        if (!mainStateManager) return;
        if (!mainStateManager.isInGame()) {
        if (persistent_bool[O("bAutoQueue")]) {
            if (!sharedMenuManager.isInQueue()) DrawAutoQueue();
        } return;
        }

        auto visualCue = sharedGameManager.mVisualCue();

        Ball::Classification myclass = sharedGameManager.getPlayerClassification();

        Table table = sharedGameManager.mTable;
        if (!table) return;

        auto tableProperties = table.mTableProperties();
        if (!tableProperties) return;

        auto& pockets = tableProperties.mPockets();

        if (persistent_bool[O("bESP_DrawPockets")]) {
            for (int i = 0; i < 6; i++) {
                auto screenPos = WorldToScreen(pockets[i]);
                draw->AddCircle(ImVec2(screenPos.x, screenPos.y), 40, WHITE, 0, 3.f);
            }
        }

        GameStateManager gameStateManager = sharedGameManager.mStateManager;
        if (!gameStateManager) return;

        if (persistent_bool[O("bAutoPlay")]) AutoPlay::Update();

        auto stateId = gameStateManager.getCurrentStateId();
        if (stateId == 4) gPrediction->determineShotResult(false);
        if (stateId == 6 || stateId == 7 || stateId == 8) return;

        if (persistent_bool[O("bESP_DrawPocketsShotState")]) {
            for (int i = 0; i < 6; i++) {
                if (Prediction::pocketStatus[i]) {
                    auto screenPos = WorldToScreen(pockets[i]);
                    draw->AddCircle(ImVec2(screenPos.x, screenPos.y), 40, GREEN, 0, 5.f);
                }
            }
        }

        if (persistent_bool[O("bESP_DrawPredictionLine")]) {
            for (int i = 0; i < gPrediction->guiData.ballsCount; i++) {
                auto& ball = gPrediction->guiData.balls[i];

                if (ball.initialPosition != ball.predictedPosition) {
                    ImVec2 lastPos{};
                    for (int j = 1; j < ball.positions.size(); j++) {
                        auto point = WorldToScreen(ball.positions[j]);
                        if (lastPos.x || lastPos.y) draw->AddLine(lastPos, point, colors[i], 10.f);
                        lastPos = point;
                    }
                }
            }
        }

        if (persistent_bool[O("bESP_DrawPredictionLine")]) {
            for (int i = 0; i < gPrediction->guiData.ballsCount; i++) {
                auto& ball = gPrediction->guiData.balls[i];

                if (ball.initialPosition != ball.predictedPosition) {
                    draw->AddCircle(WorldToScreen(ball.initialPosition), 20, colors[i], 0, 6.f);
                    draw->AddCircleFilled(WorldToScreen(ball.predictedPosition), 20, colors[i]);
                }
            }
        }
    }
}

#include "ButtonClicker.h"

INLINE void DrawMenu(ImGuiIO& io) {
    if (!g_Token.empty() && !g_Auth.empty() && g_Token == g_Auth) {
        if (is_segv_handler_active()) {
            jump_buffer_active = 1;
            if (!sigsetjmp(jump_buffer, 1)) DrawESP(GetBackgroundDrawList());
            jump_buffer_active = 0;
        }

        if (g_menu.isOpen) {
            g_menu.menuAlpha += (1.0f - g_menu.menuAlpha) * io.DeltaTime * 15.0f;
        } else {
            g_menu.menuAlpha -= g_menu.menuAlpha * io.DeltaTime * 20.0f;
        }

        if (g_menu.menuAlpha > 0.01f) {
            static ImVec2 menuPos = ImVec2(-1, -1);
            
            // --- UBAH UKURAN DI SINI (SEKARANG LEBIH KECIL) ---
            float winW = 800.0f;  // Dulu 1200
            float winH = 700.0f;  // Dulu 1050
            
            if (menuPos.x < 0) {
                menuPos = ImVec2((Width - winW) * 0.5f, (Height - winH) * 0.5f);
            }
            
            SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
            SetNextWindowPos(menuPos, ImGuiCond_Always);
            
            PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.80f * g_menu.menuAlpha));
            PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            PushStyleVar(ImGuiStyleVar_Alpha, g_menu.menuAlpha);
            
            ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize;
            
            if (Begin(O("##MinimalMenu"), &g_menu.isOpen, winFlags)) {
                ImDrawList* dl = GetWindowDrawList();
                ImVec2 winPos = GetWindowPos();
                
                dl->AddRect(winPos, ImVec2(winPos.x + winW, winPos.y + winH), IM_COL32(255, 255, 255, (int)(220 * g_menu.menuAlpha)), 0.0f, 0, 4.0f);
                
                float closeX = winPos.x + 20;
                float closeY = winPos.y + 20;
                float closeSize = 40.0f;
                bool closeHovered = IsMouseHoveringRect(ImVec2(closeX - 5, closeY - 5), ImVec2(closeX + closeSize + 5, closeY + closeSize + 5));
                
                ImU32 xColor = closeHovered ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 255, 255, 220);
                dl->AddLine(ImVec2(closeX, closeY), ImVec2(closeX + closeSize, closeY + closeSize), xColor, 4.0f);
                dl->AddLine(ImVec2(closeX + closeSize, closeY), ImVec2(closeX, closeY + closeSize), xColor, 4.0f);
                
                SetCursorPos(ImVec2(15, 15));
                if (InvisibleButton(O("##CloseX"), ImVec2(50, 50))) g_menu.isOpen = false;
                
                SetWindowFontScale(1.2f);
                ImVec2 titleSize = CalcTextSize(O("AIM X"));
                float titleX = winPos.x + (winW - titleSize.x) * 0.5f;
                float titleY = winPos.y + 30;
                dl->AddText(ImVec2(titleX, titleY), IM_COL32(255, 255, 255, (int)(255 * g_menu.menuAlpha)), O("AIM X"));
                SetWindowFontScale(1.0f);
                
                SetCursorPos(ImVec2(80, 0));
                InvisibleButton(O("##DragArea"), ImVec2(winW - 120, 80));
                if (IsItemActive() && IsMouseDragging(0)) {
                    menuPos.x += io.MouseDelta.x;
                    menuPos.y += io.MouseDelta.y;
                    menuPos.x = ImClamp(menuPos.x, 0.0f, (float)Width - winW);
                    menuPos.y = ImClamp(menuPos.y, 0.0f, (float)Height - winH);
                }
                
                SetCursorPos(ImVec2(40, 100));
                BeginChild(O("##Content"), ImVec2(winW - 80, winH - 150), false, ImGuiWindowFlags_NoScrollbar);
                
                bool need_save = false;
                
                need_save |= BigToggle(O("Lines"), &persistent_bool[O("bESP_DrawPredictionLine")]);
                need_save |= BigToggle(O("Pockets"), &persistent_bool[O("bESP_DrawPockets")]);
                need_save |= BigToggle(O("States"), &persistent_bool[O("bESP_DrawPocketsShotState")]);
                
                Dummy(ImVec2(0, 30));
                
                float groupPadding = 30.0f;
                ImVec2 curPos = GetCursorScreenPos();
                ImVec2 groupStart = ImVec2(curPos.x + groupPadding, curPos.y);
                float groupW = GetContentRegionAvail().x - (groupPadding * 2);
                float groupH = 220.0f; // Dikurangi karena layar lebih kecil
                
                dl->AddRect(groupStart, ImVec2(groupStart.x + groupW, groupStart.y + groupH), IM_COL32(255, 255, 255, 180), 0.0f, 0, 3.0f);
                
                Dummy(ImVec2(0, 20));
                Indent(groupPadding + 10.0f);
                PushItemWidth(groupW - 60.0f); // Disesuaikan
                need_save |= BigToggle(O("Auto Play"), &persistent_bool[O("bAutoPlay")]);
                need_save |= BigToggle(O("Auto Queue"), &persistent_bool[O("bAutoQueue")]);
                PopItemWidth();
                Unindent(groupPadding + 10.0f);
                
                if (need_save) save_persistence();
                
                EndChild();
            }
            End();
            
            PopStyleVar(4);
            PopStyleColor();
        }
    }
}

static void DrawFloatingButton(ImGuiIO& io) {
    static ImVec2 buttonPos = ImVec2(80, 60);
    static bool isDragging = false;
    static float hoverAnim = 0.0f;
    static GLuint logo_tex = LoadTextureFromMemory(logo_8bpp_png, logo_8bpp_png_len);
    
    float buttonRadius = 75.0f;  // Dipangkas dari 150 jadi 75 (lebih kecil dan proporsional)
    float buttonSize = buttonRadius * 2.0f;
    
    bool isHovered = false;
    
    SetNextWindowPos(buttonPos, ImGuiCond_Always);
    SetNextWindowSize(ImVec2(buttonSize + 10, buttonSize + 10), ImGuiCond_Always);
    PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    if (Begin(O("##FloatBtn"), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImDrawList* dl = GetWindowDrawList();
        
        ImVec2 center = ImVec2(buttonPos.x + buttonRadius + 5, buttonPos.y + buttonRadius + 5);
        
        // --- PERBAIKAN 1: Hitbox jangan sama besar dengan gambar ---
        float hitboxSize = 60.0f; // Ukuran sensitif sentuhan (lebih kecil dari gambar 150px)
        SetCursorPos(ImVec2((buttonSize + 10 - hitboxSize) * 0.5f, (buttonSize + 10 - hitboxSize) * 0.5f));
        InvisibleButton(O("##FloatBtnHit"), ImVec2(hitboxSize, hitboxSize));
        
        isHovered = IsItemHovered();
        
        float targetHover = isHovered ? 1.0f : 0.0f;
        hoverAnim += (targetHover - hoverAnim) * io.DeltaTime * 10.0f;
        
        float currentRadius = buttonRadius + hoverAnim * 4.0f; // Animasi membesar lebih halus
        
        ImVec2 logoMin = ImVec2(center.x - currentRadius, center.y - currentRadius);
        ImVec2 logoMax = ImVec2(center.x + currentRadius, center.y + currentRadius);
        dl->AddImage((void*)(intptr_t)logo_tex, logoMin, logoMax);
        
        if (isHovered) {
            dl->AddCircle(center, currentRadius + 3, IM_COL32(255, 255, 255, (int)(100 * hoverAnim)), 32, 2.0f);
        }
        
        // --- PERBAIKAN 2: Logika Drag & Klik ---
        bool isHeld = IsItemActive(); // Apakah sedang ditekan?
        bool isDraggingNow = isHeld && IsMouseDragging(0); // Apakah sedang ditarik?

        if (isDraggingNow) {
            isDragging = true;
            buttonPos.x += io.MouseDelta.x;
            buttonPos.y += io.MouseDelta.y;
            buttonPos.x = ImClamp(buttonPos.x, 0.0f, (float)Width - buttonSize - 10);
            buttonPos.y = ImClamp(buttonPos.y, 0.0f, (float)Height - buttonSize - 10);
        }
        
        // Hanya buka menu jika TIDAK sedang drag (Tap murni)
        if (isHeld && IsMouseReleased(0) && !isDragging) {
            g_menu.isOpen = !g_menu.isOpen;
        }
        
        // Reset status drag saat tombol dilepas
        if (!isHeld) isDragging = false;
    }
    End();
    
    PopStyleVar(2);
    PopStyleColor();
}


static bool first_time = true;
INLINE void DrawLogin(ImGuiIO& io) {
    if (logged_in) return DrawMenu(io);

    SetNextWindowPos(ImVec2(0, 0));
    SetNextWindowSize(io.DisplaySize);
    PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.06f, 0.96f));
    Begin(O("##Overlay"), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus);
    PopStyleColor();

    float cardW = 580;
    float cardH = 420;

    SetNextWindowSize(ImVec2(cardW, cardH), ImGuiCond_Always);
    SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.14f, 1.0f));
    PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    Begin(O("##LoginCard"), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = GetWindowDrawList();
    ImVec2 winPos = GetWindowPos();
    
    DrawGradientRect(dl, winPos, ImVec2(winPos.x + cardW, winPos.y + 110), IM_COL32(35, 95, 170, 255), IM_COL32(55, 125, 200, 255), true);
    dl->AddRectFilled(winPos, ImVec2(winPos.x + cardW, winPos.y + 20), IM_COL32(35, 95, 170, 255), 20.0f, ImDrawFlags_RoundCornersTop);

    SetWindowFontScale(1.4f);
    ImVec2 titleSize = CalcTextSize("AIM X");
    dl->AddText(ImVec2(winPos.x + (cardW - titleSize.x) * 0.5f, winPos.y + 30), IM_COL32(255, 255, 255, 255), "AIM X");
    SetWindowFontScale(1.0f);
    
    ImVec2 subSize = CalcTextSize("Premium 8 Ball Pool Mod");
    dl->AddText(ImVec2(winPos.x + (cardW - subSize.x) * 0.5f, winPos.y + 70), IM_COL32(200, 220, 255, 200), "Premium 8 Ball Pool Mod");

    SetCursorPosY(130);

    if (!ERROR_MESSAGE.empty()) {
        SetCursorPosX(30);
        PushTextWrapPos(cardW - 30);
        TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ERROR_MESSAGE.c_str());
        PopTextWrapPos();
        Dummy(ImVec2(0, 15));
    }

    if (is_logging_in) {
        SetCursorPosY(180);
        
        static float spinner_angle = 0.0f;
        spinner_angle += io.DeltaTime * 5.0f;

        float spinner_size = 40.0f;
        ImVec2 spinnerCenter = ImVec2(winPos.x + cardW * 0.5f, winPos.y + 220);

        for (int i = 0; i < 12; i++) {
            float angle = spinner_angle + (i * PI * 2.0f / 12.0f);
            float alpha = (float)(12 - i) / 12.0f;
            ImVec2 dotPos = ImVec2(
                spinnerCenter.x + cosf(angle) * spinner_size,
                spinnerCenter.y + sinf(angle) * spinner_size
            );
            dl->AddCircleFilled(dotPos, 6.0f, IM_COL32(100, 180, 255, (int)(alpha * 255)));
        }

        ImVec2 loadingSize = CalcTextSize("Authenticating...");
        SetCursorPosX((cardW - loadingSize.x) * 0.5f);
        SetCursorPosY(290);
        TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Authenticating...");
    } else {
        SetCursorPosY(160);
        
        ImVec2 infoSize = CalcTextSize("Copy your license key and tap login");
        SetCursorPosX((cardW - infoSize.x) * 0.5f);
        TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1.0f), "Copy your license key and tap login");
        
        Dummy(ImVec2(0, 50));
        
        bool AutoLogin = first_time && !persistent_string["key"].empty();
        
        SetCursorPosX(40);
        PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.80f, 1.0f));
        PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.90f, 1.0f));
        PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.45f, 0.72f, 1.0f));
        PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        
        if (AutoLogin || Button("LOGIN WITH CLIPBOARD", ImVec2(cardW - 80, 65))) {
            JNIEnv* env;
            jint getEnvResult = VM->GetEnv((void**)&env, JNI_VERSION_1_6);
            if (getEnvResult == JNI_EDETACHED) {
                if (VM->AttachCurrentThread(&env, nullptr) != 0) ERROR_MESSAGE = O("Failed to attach thread to JVM");
            } else if (getEnvResult != JNI_OK) {
                ERROR_MESSAGE = O("Failed to get JNIEnv");
            } else {
                std::thread([](std::string androidId, std::string key) {
                    Login(androidId, key);
                }, getAndroidID(env), AutoLogin ? persistent_string["key"] : getClipboard(env)).detach();
            }

            first_time = false;
        }
        
        PopStyleVar();
        PopStyleColor(3);
        
        Dummy(ImVec2(0, 35));
        
        ImVec2 helpSize = CalcTextSize("Your key will be read from clipboard");
        SetCursorPosX((cardW - helpSize.x) * 0.5f);
        TextColored(ImVec4(0.42f, 0.42f, 0.48f, 1.0f), "Your key will be read from clipboard");
    }

    End();
    PopStyleVar(3);
    PopStyleColor();
    
    End();
}


INLINE void SetupImgui() {
    PACKAGE_NAME = string(getcmdline());

    ImGui::CreateContext();

    auto& style = ImGui::GetStyle();
    auto& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

    switch_theme(current_theme);

    load_persistence();
    load_imgui_style();

    static string INI_PATH = O("/data/user_de/0/") + PACKAGE_NAME + O("/no_backup/.ini");
    io.IniFilename = persistent_bool["bImguiAutoSave"] ? INI_PATH.c_str() : nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = persistent_bool["bMoveOnlyWithTitleBar"];

    ImFontConfig font_cfg;
    font_cfg.SizePixels = persistent_float["fFontScale"];
    io.Fonts->AddFontDefault(&font_cfg);

    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init(O("#version 300 es"));

    bImguiSetup = true;
}

DEFINES(EGLBoolean, Draw, EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH, &Width);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &Height);

    if (Width <= 0 || Height <= 0) return _Draw(dpy, surface);

    screenCenter = Vector2(Width / 2, Height / 2);

    if (!bImguiSetup) SetupImgui();

    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(Width, Height);
    ImGui::NewFrame();

    if (!is_segv_handler_active()) setup_global_segv_handler();
    if (!g_Token.empty() && !g_Auth.empty() && g_Token == g_Auth) {
        DrawFloatingButton(io);
        DrawMenu(io);
    } else {
        DrawLogin(io);
    }
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui_ClearHoverEffect();

    return _Draw(dpy, surface);
}

void __IMGUI__() {
    create_directory_recursive(CONC(O("/data/user_de/0/"), PACKAGE_NAME.c_str(), O("/no_backup")));
}
