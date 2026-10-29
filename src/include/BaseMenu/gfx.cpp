#include "gfx.h"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "font.h"
#include "avatar.h"
#ifndef LOGE
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BaseMenu", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "BaseMenu", __VA_ARGS__)
#endif

namespace {

const char* kVS =
    "attribute vec2 aPos; attribute vec2 aUV; attribute vec4 aCol; attribute vec4 aExt;\n"
    "uniform vec2 uScreen;\n"
    "varying vec2 vUV; varying vec4 vCol; varying vec4 vExt;\n"
    "void main(){ vec2 p=aPos/uScreen*2.0-1.0; gl_Position=vec4(p.x,-p.y,0.0,1.0);"
    " vUV=aUV; vCol=aCol; vExt=aExt; }\n";

const char* kFS =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
    "uniform sampler2D uTex;\n"
    "uniform sampler2D uTex2;\n"
    "varying vec2 vUV; varying vec4 vCol; varying vec4 vExt;\n"
    "void main(){\n"
    // image (vCol.r == -1 sentinel, never a real color component): vUV/vExt use the same
    // local-offset-from-center + (hw,hh,r,soft) convention as the plain rect branch below —
    // image() builds its quad exactly like rectImpl() does — so the rounded-corner/circle mask
    // is the identical SDF math, just gating a texture sample instead of a flat color.
    "  if(vCol.r<-0.5){ vec2 uv01=vUV/(2.0*vExt.xy)+0.5; vec4 tc=texture2D(uTex2,uv01);"
    " vec2 qi=abs(vUV)-vExt.xy+vec2(vExt.z); float di=length(max(qi,0.0))+min(max(qi.x,qi.y),0.0)-vExt.z;"
    " float mi=clamp(0.5-di/vExt.w,0.0,1.0); gl_FragColor=vec4(tc.rgb,tc.a*vCol.a*mi); return; }\n"
    "  if(vExt.x<0.0){ float a=texture2D(uTex,vUV).a; float t=smoothstep(0.5-vExt.y,0.5+vExt.y,a);"
    " gl_FragColor=vec4(vCol.rgb,vCol.a*t); return; }\n"
    "  vec2 q=abs(vUV)-vExt.xy+vec2(vExt.z);\n"
    "  float d=length(max(q,0.0))+min(max(q.x,q.y),0.0)-vExt.z;\n"
    "  float a=clamp(0.5-d/vExt.w,0.0,1.0);\n"
    "  gl_FragColor=vec4(vCol.rgb,vCol.a*a);\n"
    "}\n";

GLuint compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOGE("shader error: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Шрифт: SDF-атлас, запечённый из TTF (tools/bake_font.py), ASCII + кириллица.
// Альфа атласа = 0.5 + расстояние_до_края / (2 * spread); резкость задаётся шейдером по масштабу.
void buildAtlas(GLuint& tex) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kFontAtlasW, kFontAtlasH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, kFontAtlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Аватар: baked RGBA (avatar.h), отдельный texture unit (GL_TEXTURE1), привязан один раз —
// ничего больше unit 1 не трогает, поэтому перепривязывать в begin() не нужно.
void buildAvatar(GLuint& tex) {
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAvatarW, kAvatarH, 0, GL_RGBA, GL_UNSIGNED_BYTE, kAvatarRGBA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
}

// Таблица kFontGlyphs отсортирована по cp. Нет глифа -> '?'.
const FontGlyph* findGlyph(uint32_t cp) {
    int lo = 0, hi = kFontGlyphCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (kFontGlyphs[mid].cp == cp) return &kFontGlyphs[mid];
        if (kFontGlyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return cp == '?' ? nullptr : findGlyph('?');
}

// UTF-8 -> code point. Битая последовательность даёт U+FFFD (рисуется как '?'), указатель всегда сдвигается минимум на 1 байт.
uint32_t nextCp(const char*& s) {
    const unsigned char* p = (const unsigned char*)s;
    uint32_t c = p[0];
    int n = 0;
    if (c < 0x80) { s += 1; return c; }
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { s += 1; return 0xFFFD; }
    for (int i = 1; i <= n; ++i) {
        if ((p[i] & 0xC0) != 0x80) { s += i; return 0xFFFD; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    s += n + 1;
    return c;
}

const int kStride = 12; // floats на вершину: pos2 uv2 col4 ext4

} // namespace

bool Gfx::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glBindAttribLocation(prog_, 0, "aPos");
    glBindAttribLocation(prog_, 1, "aUV");
    glBindAttribLocation(prog_, 2, "aCol");
    glBindAttribLocation(prog_, 3, "aExt");
    glLinkProgram(prog_);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("program link failed");
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    locScreen_ = glGetUniformLocation(prog_, "uScreen");
    locTex_ = glGetUniformLocation(prog_, "uTex");
    locTex2_ = glGetUniformLocation(prog_, "uTex2");
    buildAtlas(tex_);
    buildAvatar(avatarTex_);
    hasAvatar_ = true;
    buf_.reserve(kStride * 6 * 2048);
    LOGI("Gfx ready");
    return true;
}

void Gfx::begin(int w, int h) {
    W_ = w;
    H_ = h;
    alpha = 1.f;
    ox = oy = 0.f;
    buf_.clear();
    vpY_ = (surfH > h) ? surfH - h : 0;
    glViewport(0, vpY_, w, h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glUniform2f(locScreen_, (float)w, (float)h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glUniform1i(locTex_, 0);
    if (locTex2_ >= 0) glUniform1i(locTex2_, 1);   // unit 1 bound once in buildAvatar(), stays bound
}

void Gfx::end() {
    flush();
    glDisable(GL_SCISSOR_TEST);
}

void Gfx::flush() {
    if (buf_.empty()) return;
    const float* p = buf_.data();
    const GLsizei stride = kStride * sizeof(float);
    for (int i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, p + 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, p + 2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, p + 4);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, p + 8);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(buf_.size() / kStride));
    buf_.clear();
}

void Gfx::vtx(float x, float y, float u, float v, Col c, float e0, float e1, float e2, float e3) {
    const float d[kStride] = {x, y, u, v, c.r, c.g, c.b, c.a * alpha, e0, e1, e2, e3};
    buf_.insert(buf_.end(), d, d + kStride);
}

void Gfx::rectImpl(float x, float y, float w, float h, float r, Col tl, Col tr, Col bl, Col br, float soft) {
    if (w <= 0.f || h <= 0.f) return;
    r = std::min(r, std::min(w, h) * 0.5f);
    soft = std::max(soft, 1.f);
    x += ox;
    y += oy;
    const float pad = soft + 1.f;
    const float x0 = x - pad, x1 = x + w + pad, y0 = y - pad, y1 = y + h + pad;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    auto col = [&](float vx, float vy) {
        float fx = std::min(1.f, std::max(0.f, (vx - x) / w));
        float fy = std::min(1.f, std::max(0.f, (vy - y) / h));
        return mix(mix(tl, tr, fx), mix(bl, br, fx), fy);
    };
    Col c00 = col(x0, y0), c10 = col(x1, y0), c01 = col(x0, y1), c11 = col(x1, y1);
    vtx(x0, y0, x0 - cx, y0 - cy, c00, hw, hh, r, soft);
    vtx(x1, y0, x1 - cx, y0 - cy, c10, hw, hh, r, soft);
    vtx(x1, y1, x1 - cx, y1 - cy, c11, hw, hh, r, soft);
    vtx(x0, y0, x0 - cx, y0 - cy, c00, hw, hh, r, soft);
    vtx(x1, y1, x1 - cx, y1 - cy, c11, hw, hh, r, soft);
    vtx(x0, y1, x0 - cx, y1 - cy, c01, hw, hh, r, soft);
}

void Gfx::image(float x, float y, float w, float h, float r, Col tint, float soft) {
    if (!hasAvatar_ || w <= 0.f || h <= 0.f) return;
    r = std::min(r, std::min(w, h) * 0.5f);
    soft = std::max(soft, 1.f);
    x += ox;
    y += oy;
    const float pad = soft + 1.f;
    const float x0 = x - pad, x1 = x + w + pad, y0 = y - pad, y1 = y + h + pad;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    const Col c{-1.f, 0.f, 0.f, tint.a};   // r=-1 is the shader sentinel for "sample uTex2", see kFS
    vtx(x0, y0, x0 - cx, y0 - cy, c, hw, hh, r, soft);
    vtx(x1, y0, x1 - cx, y0 - cy, c, hw, hh, r, soft);
    vtx(x1, y1, x1 - cx, y1 - cy, c, hw, hh, r, soft);
    vtx(x0, y0, x0 - cx, y0 - cy, c, hw, hh, r, soft);
    vtx(x1, y1, x1 - cx, y1 - cy, c, hw, hh, r, soft);
    vtx(x0, y1, x0 - cx, y1 - cy, c, hw, hh, r, soft);
}

void Gfx::shadow(float x, float y, float w, float h, float r, float blur, Col c) {
    float g = blur * 0.25f;
    rectImpl(x - g, y - g, w + 2 * g, h + 2 * g, r + g, c, c, c, c, blur);
}

// size = высота заглавной буквы в пикселях (как и раньше), y = верх заглавных.
float Gfx::textWidth(const char* s, float size) const {
    const float k = size / kFontCap;
    float w = 0.f;
    while (*s) {
        const FontGlyph* g = findGlyph(nextCp(s));
        if (g) w += g->adv * k;
    }
    return w;
}

void Gfx::text(float x, float y, const char* s, float size, Col c) {
    const float k = size / kFontCap;                      // экранных px на базовый px атласа
    const float soft = 1.f / (4.f * kFontSpread * k);     // полширины сглаживания в единицах альфы SDF (~0.5 экранного px)
    x += ox;
    const float base = y + oy + size;                     // baseline
    while (*s) {
        const FontGlyph* g = findGlyph(nextCp(s));
        if (!g) continue;
        if (g->w) {
            const float u0 = (float)g->x / kFontAtlasW, v0 = (float)g->y / kFontAtlasH;
            const float u1 = (float)(g->x + g->w) / kFontAtlasW, v1 = (float)(g->y + g->h) / kFontAtlasH;
            const float x0 = x + g->bx * k, y0 = base - g->by * k;
            const float x1 = x0 + g->w * k, y1 = y0 + g->h * k;
            vtx(x0, y0, u0, v0, c, -1, soft, 0, 1);
            vtx(x1, y0, u1, v0, c, -1, soft, 0, 1);
            vtx(x1, y1, u1, v1, c, -1, soft, 0, 1);
            vtx(x0, y0, u0, v0, c, -1, soft, 0, 1);
            vtx(x1, y1, u1, v1, c, -1, soft, 0, 1);
            vtx(x0, y1, u0, v1, c, -1, soft, 0, 1);
        }
        x += g->adv * k;
    }
}

void Gfx::icon(float x, float y, float w, float h, uint32_t cp, Col c) {
    const FontGlyph* g = findGlyph(cp);
    if (!g || !g->w) return;            // unbaked codepoint -> silently skip, not '?': a badge is not text
    const float scale = w / (float)g->w;                   // atlas px -> screen px (icons are baked square)
    const float soft = 1.f / (4.f * kFontSpread * scale);
    const float u0 = (float)g->x / kFontAtlasW, v0 = (float)g->y / kFontAtlasH;
    const float u1 = (float)(g->x + g->w) / kFontAtlasW, v1 = (float)(g->y + g->h) / kFontAtlasH;
    const float x0 = x + ox, y0 = y + oy, x1 = x0 + w, y1 = y0 + h;
    vtx(x0, y0, u0, v0, c, -1, soft, 0, 1);
    vtx(x1, y0, u1, v0, c, -1, soft, 0, 1);
    vtx(x1, y1, u1, v1, c, -1, soft, 0, 1);
    vtx(x0, y0, u0, v0, c, -1, soft, 0, 1);
    vtx(x1, y1, u1, v1, c, -1, soft, 0, 1);
    vtx(x0, y1, u0, v1, c, -1, soft, 0, 1);
}

void Gfx::clip(float x, float y, float w, float h) {
    flush();
    glEnable(GL_SCISSOR_TEST);
    int sx = (int)std::floor(x + ox), sy = (int)std::floor(H_ - (y + oy + h));
    glScissor(sx, std::max(0, sy) + vpY_, std::max(0, (int)std::ceil(w)), std::max(0, (int)std::ceil(h)));
}

void Gfx::unclip() {
    flush();
    glDisable(GL_SCISSOR_TEST);
}
