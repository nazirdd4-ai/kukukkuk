#pragma once
#include <cstdint>
#include <vector>

struct Col {
    float r, g, b, a;
};
inline Col rgba(int r, int g, int b, int a = 255) { return Col{r / 255.f, g / 255.f, b / 255.f, a / 255.f}; }
inline Col withA(Col c, float a) { c.a *= a; return c; }
inline Col mix(Col a, Col b, float t) {
    return Col{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// Свой 2D-рендерер на OpenGL ES 2.0: все примитивы батчатся в один буфер.
// Скруглённые прямоугольники, тени и свечение считаются в шейдере (SDF).
class Gfx {
public:
    float alpha = 1.f;       // общий множитель прозрачности
    float ox = 0.f, oy = 0.f; // общий сдвиг (для анимаций)
    int   surfH = 0;          // высота реальной поверхности: если больше h в begin(), вьюпорт прижимается к ВЕРХУ (GL-origin снизу)

    bool init();
    void begin(int w, int h);
    void end();

    void rect(float x, float y, float w, float h, float r, Col c, float soft = 1.f) { rectImpl(x, y, w, h, r, c, c, c, c, soft); }
    void rect(float x, float y, float w, float h, float r, Col top, Col bot, float soft = 1.f) { rectImpl(x, y, w, h, r, top, top, bot, bot, soft); }
    void rectH(float x, float y, float w, float h, float r, Col left, Col right, float soft = 1.f) { rectImpl(x, y, w, h, r, left, right, left, right, soft); }
    void shadow(float x, float y, float w, float h, float r, float blur, Col c);

    // Baked RGBA image (currently: avatar.h). r = corner radius; r = w/2 = h/2 gives a circle,
    // same trick as rect()'s pill buttons. tint.a tints opacity; tint.rgb ignored (texture carries color).
    void image(float x, float y, float w, float h, float r, Col tint = rgba(255, 255, 255), float soft = 1.f);

    // size = высота заглавной буквы в пикселях
    void text(float x, float y, const char* s, float size, Col c);
    float textWidth(const char* s, float size) const;

    // Draws one baked vector-icon glyph (codepoints 0xE000.. — see tools/bake_font.py
    // ICON_CODEPOINTS), scaled to exactly fill [x,y,w,h]. Same SDF font atlas/shader
    // path as text(), no new GL texture or state. No-op if the codepoint isn't baked.
    void icon(float x, float y, float w, float h, uint32_t cp, Col c);

    void clip(float x, float y, float w, float h);
    void unclip();
    // helper: line as thin rect along dominant axis
    void line(float x0, float y0, float x1, float y1, float thickness, Col c) {
        float dx = x1-x0, dy = y1-y0;
        float len = sqrtf(dx*dx+dy*dy); if (len < 0.5f) return;
        float nx = -dy/len*thickness*0.5f, ny = dx/len*thickness*0.5f;
        (void)nx; (void)ny;
        float t = thickness;
        if (fabsf(dx) >= fabsf(dy)) {
            float xl = x0<x1?x0:x1, xr = x0<x1?x1:x0, yc = (y0+y1)*0.5f;
            rectImpl(xl, yc-t*0.5f, xr-xl, t, 0.f, c, c, c, c, 1.f);
        } else {
            float yt = y0<y1?y0:y1, yb = y0<y1?y1:y0, xc = (x0+x1)*0.5f;
            rectImpl(xc-t*0.5f, yt, t, yb-yt, 0.f, c, c, c, c, 1.f);
        }
    }


private:
    void rectImpl(float x, float y, float w, float h, float r, Col tl, Col tr, Col bl, Col br, float soft);
    void vtx(float x, float y, float u, float v, Col c, float e0, float e1, float e2, float e3);
    void flush();

    unsigned prog_ = 0, tex_ = 0, avatarTex_ = 0;
    int locScreen_ = -1, locTex_ = -1, locTex2_ = -1;
    bool hasAvatar_ = false;
    int W_ = 0, H_ = 0, vpY_ = 0;
    std::vector<float> buf_;
};
