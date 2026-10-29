#pragma once
#include <mutex>
#include <string>
#include <unordered_map>

#include "gfx.h"

// Меню: окно с вкладками, ватермарк справа сверху и полоска-ручка внизу по центру.
// Потоки: touch() вызывается из UI-потока, render() из GL-потока (защищено мьютексом).
class Menu {
public:
    Menu();

    // ---- настройки внешнего вида (можно менять из content.cpp) ----
    Col accent;
    bool showWatermark = true;
    std::string watermark = "BASE";
    float uiScale = 1.f;
    float handleY = 0.86f; // положение полоски по высоте экрана (0..1)
    int  lang = 0;               // 0 = English, 1 = Русский (see tr())
    bool showFps = false;        // FPS pill next to the Enemies/Bots pill (Settings -> Interface)
    int  hudEnemies = 0, hudBots = 0;
    const char* tr(const char* s) const;

    void resize(int w, int h);
    // action: 0 = down, 1 = up, 2 = move, 3 = cancel. true = событие забрало меню.
    bool touch(int action, int pointerId, float x, float y);
    void render(Gfx& g, float dt);

    bool isOpen() const { return open_; }
    void setOpen(bool o) { open_ = o; }

    // ---- виджеты (вызываются внутри buildContent) ----
    bool toggle(const char* label, bool* v);
    bool slider(const char* label, float* v, float mn, float mx, const char* fmt = "%.0f");
    bool choice(const char* label, int* v, const char* const* opts, int n);
    bool button(const char* label);
    void label(const char* text);
    void section(const char* title);
    void info(const char* left, const char* right);   // dim label left, white value right
    void labelInf(const char* text);                  // row "<text> ∞" (the glyph is drawn, it is not in the baked font)
    // highlighted info card: title + subtitle + full-width action button. onClick fires on tap,
    // clicked flag also returned so callers can react inline if they'd rather not pass a callback.
    bool infoCard(const char* title, const char* subtitle, const char* btnLabel, void (*onClick)() = nullptr);

    Col accentL() const;

private:
    void recalc();
    void handleRect(float& x, float& y, float& w, float& h) const;
    void drawWatermark();
    void drawHandle();
    void drawWindow();
    float rowY(float h);
    bool hit(float x, float y, float w, float h, float tx, float ty) const;
    bool tapped(float x, float y, float w, float h) const;
    bool held(float x, float y, float w, float h) const;
    float animTo(int id, float target);

    std::mutex mu_;
    Gfx* g_ = nullptr;

    int W_ = 0, H_ = 0;
    float u_ = 1.f, lastScale_ = 1.f;
    float wx_ = 0, wy_ = 0, ww_ = 0, wh_ = 0;
    bool placed_ = false;

    bool open_ = false;
    float anim_ = 0.f, time_ = 0.f, fps_ = 60.f, dt_ = 0.016f;

    // ввод
    bool down_ = false, pressed_ = false, released_ = false, moved_ = false;
    float ix_ = 0, iy_ = 0, px_ = 0, py_ = 0, lastY_ = 0;
    int capId_ = -1;
    bool dragWin_ = false, handleDown_ = false, closeDown_ = false;
    float dragDX_ = 0, dragDY_ = 0;
    float hudX_ = 0, hudY_ = 0, hudW_ = 0, hudH_ = 0;   // HUD pill block rect (tap = open/close menu)
    bool inputOK_ = false;

    // раскладка содержимого
    float cx_ = 0, cy_ = 0, cw_ = 0, top_ = 0, bottom_ = 0;
    float scroll_ = 0, contentH_ = 0;
    int idc_ = 0, activeId_ = 0, tab_ = 0;
    std::unordered_map<int, float> anims_;
};
