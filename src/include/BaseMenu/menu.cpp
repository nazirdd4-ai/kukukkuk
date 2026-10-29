#include "menu.h"
#include <sys/utsname.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "content.h"

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static const Col kWin    = rgba(38, 38, 40);
static const Col kSide   = rgba(30, 30, 32);
static const Col kRowCol = rgba(51, 51, 53);
static const Col kDim    = rgba(142, 142, 147);
static const Col kTxt    = rgba(245, 245, 247);

// fake-bold: the baked font has a single weight, so draw the run twice, 'off' px apart
static void boldText(Gfx& g, float x, float y, const char* s, float size, Col c, float off) {
    g.text(x, y, s, size, c);
    g.text(x + off, y, s, size, c);
}

Menu::Menu() { accent = rgba(255, 59, 59); }

Col Menu::accentL() const { return mix(accent, rgba(255, 255, 255), 0.38f); }

void Menu::recalc() {
    if (W_ <= 0) return;
    u_ = std::min(W_, H_) / 540.f * uiScale;
    lastScale_ = uiScale;
    ww_ = std::min(760 * u_, W_ - 24 * u_);
    wh_ = std::min(470 * u_, H_ - 24 * u_);
    if (!placed_) {
        wx_ = (W_ - ww_) / 2;
        wy_ = (H_ - wh_) / 2;
        placed_ = true;
    }
    wx_ = clampf(wx_, 0, std::max(0.f, W_ - ww_));
    wy_ = clampf(wy_, 0, std::max(0.f, H_ - wh_));
}

void Menu::resize(int w, int h) {
    std::lock_guard<std::mutex> l(mu_);
    W_ = w;
    H_ = h;
    placed_ = false;
    recalc();
}

void Menu::handleRect(float& x, float& y, float& w, float& h) const {
    if (hudW_ > 0) { x = hudX_; y = hudY_; w = hudW_; h = hudH_; return; }
    w = 240 * u_; h = 34 * u_; x = (W_ - w) / 2; y = 12 * u_;
}

// ---------------------------------------------------------------- ввод

bool Menu::touch(int action, int pid, float x, float y) {
    std::lock_guard<std::mutex> l(mu_);
    if (W_ <= 0) return false;
    const float th = 46 * u_;

    if (action == 0) {
        if (capId_ != -1) return false;
        float hx, hy, hw, hh;
        handleRect(hx, hy, hw, hh);
        bool inH = x >= hx - 30 * u_ && x <= hx + hw + 30 * u_ && y >= hy - 24 * u_ && y <= hy + hh + 24 * u_;
        bool inW = open_ && anim_ > 0.5f && x >= wx_ && x <= wx_ + ww_ && y >= wy_ && y <= wy_ + wh_;
        if (!inH && !inW) return false; // всё остальное уходит игре
        capId_ = pid;
        down_ = pressed_ = true;
        moved_ = false;
        px_ = ix_ = x;
        py_ = iy_ = lastY_ = y;
        handleDown_ = inH;
        dragWin_ = closeDown_ = false;
        if (!inH && y <= wy_ + th) {
            if (x >= wx_ + 16 * u_ && x <= wx_ + 42 * u_ && y <= wy_ + 34 * u_) {   // red window dot
                closeDown_ = true;
            } else {
                dragWin_ = true;
                dragDX_ = x - wx_;
                dragDY_ = y - wy_;
            }
        }
        return true;
    }

    if (pid != capId_) return false;
    ix_ = x;
    iy_ = y;

    if (action == 2) {
        if (!moved_ && (std::fabs(x - px_) > 8 * u_ || std::fabs(y - py_) > 8 * u_)) moved_ = true;
        if (dragWin_) {
            wx_ = clampf(x - dragDX_, 0, std::max(0.f, W_ - ww_));
            wy_ = clampf(y - dragDY_, 0, std::max(0.f, H_ - wh_));
        }
        return true;
    }

    down_ = false;
    released_ = true;
    capId_ = -1;
    if (action == 1) {
        if (handleDown_ && !moved_) open_ = !open_;
        if (closeDown_ && x >= wx_ + 16 * u_ && x <= wx_ + 42 * u_ && y <= wy_ + 34 * u_) open_ = false;
    }
    handleDown_ = dragWin_ = closeDown_ = false;
    return true;
}

bool Menu::hit(float x, float y, float w, float h, float tx, float ty) const {
    return tx >= x && tx <= x + w && ty >= y && ty <= y + h && ty >= top_ && ty <= bottom_;
}
bool Menu::tapped(float x, float y, float w, float h) const {
    return inputOK_ && released_ && !moved_ && hit(x, y, w, h, px_, py_) && hit(x, y, w, h, ix_, iy_);
}
bool Menu::held(float x, float y, float w, float h) const {
    return inputOK_ && down_ && !moved_ && hit(x, y, w, h, px_, py_);
}
float Menu::animTo(int id, float target) {
    auto it = anims_.find(id);
    if (it == anims_.end()) it = anims_.emplace(id, target).first;
    float& a = it->second;
    a += (target - a) * std::min(1.f, dt_ * 16.f);
    return a;
}

// ---------------------------------------------------------------- кадр

void Menu::render(Gfx& g, float dt) {
    std::lock_guard<std::mutex> l(mu_);
    if (W_ <= 0) return;
    g_ = &g;
    dt_ = clampf(dt, 0.f, 0.1f);
    time_ += dt_;
    if (dt > 0.0001f) fps_ += (1.f / dt - fps_) * 0.05f;
    if (uiScale != lastScale_) recalc();
    anim_ += ((open_ ? 1.f : 0.f) - anim_) * std::min(1.f, dt_ * 14.f);
    if (!open_ && anim_ < 0.01f) anim_ = 0.f;

    g.begin(W_, H_);
    drawWatermark();   // HUD pills; tapping them opens/closes the menu (the old bottom handle bar is gone)
    inputOK_ = false;
    if (anim_ > 0.01f) drawWindow();
    g.alpha = 1.f;
    g.oy = 0.f;
    g.end();

    pressed_ = released_ = false;
}

// ---------------------------------------------------------------- перевод (EN -> RU)

static const char* const kRu[][2] = {
    {"Home", "Главная"},
    {"Aimbot", "Аимбот"},
    {"Visuals", "Визуалы"},
    {"Memory", "Мемори"},
    {"Misc", "Прочее"},
    {"Settings", "Настройки"},
    {"Kernel:", "Ядро:"},
    {"Enemies:", "Враги:"},
    {"Bots:", "Боты:"},
    {"Creator's Telegram channel", "Telegram-канал создателя"},
    {"Open channel", "Открыть канал"},
    {"FPS settings", "Настройки FPS"},
    {"Unlock FPS", "Разблокировка FPS"},
    {"Menu status", "Статус меню"},
    {"About", "О приложении"},
    {"Mode:", "Режим работы:"},
    {"Direct", "Прямой"},
    {"Aim assist", "Автонаводка"},
    {"Hold button", "Только при зажатии"},
    {"Teammates", "Союзники"},
    {"Lead target", "Упреждение цели"},
    {"Tuning", "Параметры"},
    {"FOV radius", "Радиус FOV"},
    {"Smoothing", "Плавность наводки"},
    {"Bullet speed", "Скорость пули"},
    {"Aim height", "Высота точки наводки"},
    {"Camera VFOV", "Верт. FOV камеры"},
    {"Silent aim", "Силент аим"},
    {"Enable silent", "Включить силент"},
    {"Max distance", "Макс. дальность"},
    {"Target bone", "Куда целиться"},
    {"Head", "Голова"},
    {"Neck", "Шея"},
    {"Chest", "Грудь"},
    {"Pelvis", "Таз"},
    {"Closest", "Ближайшая"},
    {"Box", "Рамка"},
    {"Health bar", "Здоровье"},
    {"Distance", "Расстояние"},
    {"Names", "Ники"},
    {"Skeleton", "Скелет"},
    {"Tracers", "Линии к врагам"},
    {"Style", "Стиль"},
    {"Line width", "Толщина линий"},
    {"no data yet", "Данных пока нет"},
    {"No spread", "Без разброса"},
    {"No recoil", "Без отдачи"},
    {"Draw FOV circle", "Показывать круг FOV"},
    {"Config", "Конфигурация"},
    {"Slot", "Слот"},
    {"Save config", "Сохранить конфиг"},
    {"Load config", "Загрузить конфиг"},
    {"Menu opacity", "Непрозрачность меню"},
    {"UI scale", "Масштаб интерфейса"},
    {"Interface", "Интерфейс"},
    {"Language", "Язык"},
    {"Show FPS counter", "Показывать счётчик FPS"},
    {"Accent", "Акцентный цвет"},
    {"Color", "Цвет"},
    {"Red", "Красный"},
    {"Purple", "Фиолетовый"},
    {"Blue", "Синий"},
    {"Green", "Зелёный"},
    {"Orange", "Оранжевый"},
    {"Exit", "Выйти"},
    {"Tap again to confirm", "Нажми ещё раз для подтверждения"},
    {"px", "px"},
    {"m/s", "м/с"},
    {"deg", "°"},
    {"m", "м"},
};

const char* Menu::tr(const char* s) const {
    if (lang != 1 || !s) return s;
    for (const auto& r : kRu) if (!std::strcmp(r[0], s)) return r[1];
    return s;
}

void Menu::drawWatermark() {
    if (!showWatermark) return;
    Gfx& g = *g_;
    const float ts = 15 * u_, padx = 18 * u_, h = 34 * u_, gap = 12 * u_, y = 12 * u_, sp = 6 * u_, bar = 16 * u_, off = 0.7f * u_;
    char en[16], bo[16], fp[16];
    std::snprintf(en, sizeof(en), "%d", hudEnemies);
    std::snprintf(bo, sizeof(bo), "%d", hudBots);
    std::snprintf(fp, sizeof(fp), "%d", (int)(fps_ + 0.5f));
    const char* sEn = tr("Enemies:");
    const char* sBo = tr("Bots:");
    const char* sFp = "FPS";
    const float bw = g.textWidth("|", ts);
    const float w1 = padx * 2 + g.textWidth(sEn, ts) + sp + g.textWidth(en, ts) + bar + g.textWidth(sBo, ts) + sp + g.textWidth(bo, ts);
    const float w2 = padx * 2 + g.textWidth(sFp, ts) + sp + g.textWidth(fp, ts);
    const float total = w1 + (showFps ? gap + w2 : 0.f);
    const float x = (W_ - total) / 2;
    hudX_ = x; hudY_ = y; hudW_ = total; hudH_ = h;

    const Col white = rgba(236, 240, 248);
    g.rect(x, y, w1, h, h / 2, rgba(0, 0, 0));
    float tx = x + padx;
    const float ty = y + (h - ts) / 2;
    boldText(g, tx, ty, sEn, ts, white, off);            tx += g.textWidth(sEn, ts) + sp;
    boldText(g, tx, ty, en, ts, rgba(255, 74, 74), off); tx += g.textWidth(en, ts) + (bar - bw) / 2;
    g.text(tx, ty, "|", ts, rgba(110, 110, 116));        tx += bw + (bar - bw) / 2;
    boldText(g, tx, ty, sBo, ts, white, off);            tx += g.textWidth(sBo, ts) + sp;
    boldText(g, tx, ty, bo, ts, rgba(58, 160, 255), off);
    if (showFps) {
        const float x2 = x + w1 + gap;
        g.rect(x2, y, w2, h, h / 2, rgba(0, 0, 0));
        float t2 = x2 + padx;
        boldText(g, t2, ty, sFp, ts, white, off);        t2 += g.textWidth(sFp, ts) + sp;
        boldText(g, t2, ty, fp, ts, rgba(63, 224, 208), off);
    }
}

void Menu::drawHandle() {
    Gfx& g = *g_;
    float x, y, w, h;
    handleRect(x, y, w, h);
    float pulse = 0.5f + 0.5f * std::sin(time_ * 2.6f);
    float glow = open_ ? 0.18f : 0.22f + 0.16f * pulse;
    g.shadow(x - 4 * u_, y - 4 * u_, w + 8 * u_, h + 8 * u_, h, 18 * u_, withA(accent, glow));
    g.rectH(x, y, w, h, h / 2, accent, accentL());
}

void Menu::drawWindow() {
    Gfx& g = *g_;
    const float e = anim_ * anim_ * (3.f - 2.f * anim_);
    g.alpha = e;
    g.oy = (1.f - e) * 28 * u_;
    inputOK_ = anim_ > 0.9f;

    const float x = wx_, y = wy_, w = ww_, h = wh_, rad = 28 * u_;
    const Col ac = accent;

    g.shadow(x - 6 * u_, y, w + 12 * u_, h + 14 * u_, rad + 6 * u_, 38 * u_, rgba(0, 0, 0, 150));
    g.rect(x, y, w, h, rad, kWin);

    // window dots: live on the window background, not on the sidebar (red one closes)
    {
        const float r = 7 * u_, cy = y + 20 * u_, c0 = x + 29 * u_, st = 22 * u_;
        const Col dc[3] = {rgba(255, 95, 87), rgba(254, 188, 46), rgba(40, 200, 64)};
        for (int i = 0; i < 3; ++i) g.rect(c0 + i * st - r, cy - r, r * 2, r * 2, r, dc[i]);
    }

    // sidebar: avatar + kernel, then tabs with colored badges
    top_ = -1e9f;
    bottom_ = 1e9f;
    const float sx = x + 10 * u_, sy = y + 36 * u_, sw = 192 * u_, sh = h - 46 * u_;
    g.rect(sx, sy, sw, sh, 22 * u_, kSide);

    const float avS = 78 * u_, avX = sx + (sw - avS) / 2, avY = sy + 12 * u_;
    g.rect(avX - 2 * u_, avY - 2 * u_, avS + 4 * u_, avS + 4 * u_, (avS + 4 * u_) / 2, ac, 1.f);
    g.image(avX, avY, avS, avS, avS / 2);

    {
        static struct utsname uts{};
        static bool got = (uname(&uts) == 0);
        char rel[sizeof(uts.release)] = {};
        if (got) {
            // short form: up to the first '-' ("4.14.186-perf-g15..." -> "4.14.186")
            const char* dash = strchr(uts.release, '-');
            size_t n = dash ? (size_t)(dash - uts.release) : strlen(uts.release);
            if (n >= sizeof(rel)) n = sizeof(rel) - 1;
            memcpy(rel, uts.release, n);
            rel[n] = 0;
        }
        const char* kl = tr("Kernel:");
        const char* kv = got ? rel : "?";
        const float kts = 12 * u_, kty = avY + avS + 10 * u_;
        const float kw = g.textWidth(kl, kts) + 6 * u_ + g.textWidth(kv, kts);
        const float kx = sx + (sw - kw) / 2;
        g.text(kx, kty, kl, kts, kDim);
        boldText(g, kx + g.textWidth(kl, kts) + 6 * u_, kty, kv, kts, ac, 0.6f * u_);
        g.rect(sx + 14 * u_, kty + kts + 9 * u_, sw - 28 * u_, u_, 0.f, rgba(255, 255, 255, 30));
    }

    const float tabTop = avY + avS + 40 * u_;
    const float rowH = std::min(46 * u_, (sy + sh - tabTop - 8 * u_) / kTabCount);
    for (int i = 0; i < kTabCount; ++i) {
        const float tx = sx + 8 * u_, ty = tabTop + i * rowH, tw = sw - 16 * u_, thh = rowH - 4 * u_;
        const bool sel = (i == tab_);
        if (sel) {
            g.rect(tx, ty, tw, thh, thh / 2, withA(ac, 0.5f));
            g.rect(tx + u_, ty + u_, tw - 2 * u_, thh - 2 * u_, thh / 2 - u_, rgba(62, 36, 38));
        } else if (held(tx, ty, tw, thh)) {
            g.rect(tx, ty, tw, thh, thh / 2, rgba(255, 255, 255, 14));
        }
        const float bs = thh - 8 * u_, bx = tx + 5 * u_, by = ty + (thh - bs) / 2;
        g.rect(bx, by, bs, bs, bs * 0.27f, kTabColors[i]);
        if (kTabIcons[i]) {
            const float is = bs * 0.62f, io = (bs - is) / 2;
            g.icon(bx + io, by + io, is, is, kTabIcons[i], rgba(255, 255, 255));
        } else {   // Misc: "</>" drawn as text (no baked icon for it)
            const float cs = bs * 0.36f;
            boldText(g, bx + (bs - g.textWidth("</>", cs)) / 2, by + (bs - cs) / 2, "</>", cs, rgba(255, 255, 255), 0.5f * u_);
        }
        boldText(g, bx + bs + 10 * u_, ty + (thh - 15 * u_) / 2, tr(kTabs[i]), 15 * u_, sel ? ac : kDim, 0.6f * u_);
        if (tapped(tx, ty, tw, thh) && !sel) {
            tab_ = i;
            scroll_ = 0;
        }
    }

    // content: page title + scrolling area
    const float cpx = sx + sw + 14 * u_, cpy = y + 58 * u_, cpw = x + w - 12 * u_ - cpx, cph = y + h - 12 * u_ - cpy;
    {
        const char* pt = tr(kTabs[tab_]);
        const float ps = 24 * u_;
        boldText(g, cpx + (cpw - g.textWidth(pt, ps)) / 2, y + 16 * u_, pt, ps, kTxt, 0.8f * u_);
    }
    top_ = cpy + 2 * u_;
    bottom_ = cpy + cph - 2 * u_;
    cx_ = cpx + 4 * u_;
    cw_ = cpw - 14 * u_;

    bool pressInContent = px_ >= cpx && px_ <= cpx + cpw && py_ >= cpy && py_ <= cpy + cph;
    if (inputOK_ && down_ && moved_ && activeId_ == 0 && pressInContent && !dragWin_) scroll_ -= (iy_ - lastY_);
    lastY_ = iy_;

    const float startY = top_ + 4 * u_ - scroll_;
    cy_ = startY;
    idc_ = 0;
    g.clip(cpx, cpy, cpw, cph);
    buildContent(*this, tab_);
    g.unclip();

    contentH_ = cy_ - startY + 8 * u_;
    const float viewH = bottom_ - top_;
    const float maxScroll = std::max(0.f, contentH_ - viewH);
    scroll_ = clampf(scroll_, 0.f, maxScroll);
    if (maxScroll > 1.f) {
        float bh = std::max(24 * u_, viewH * viewH / contentH_);
        float by = top_ + (viewH - bh) * (scroll_ / maxScroll);
        g.rect(cpx + cpw - 5 * u_, by, 3 * u_, bh, 1.5f * u_, withA(accentL(), 0.55f));
    }
}

// ---------------------------------------------------------------- виджеты

float Menu::rowY(float h) {
    float y = cy_;
    cy_ += h + 8 * u_;
    return y;
}

static void card(Gfx& g, float x, float y, float w, float h, float r, bool pressed) {
    g.rect(x, y, w, h, r, pressed ? rgba(64, 64, 67) : kRowCol);
}

bool Menu::toggle(const char* lbl, bool* v) {
    Gfx& g = *g_;
    const int id = tab_ * 1000 + (++idc_);
    const float h = 52 * u_, y = rowY(h), x = cx_, w = cw_;
    bool changed = false;
    if (tapped(x, y, w, h)) {
        *v = !*v;
        changed = true;
    }
    float t = animTo(id, *v ? 1.f : 0.f);
    if (y + h < top_ || y > bottom_) return changed;

    card(g, x, y, w, h, 18 * u_, held(x, y, w, h));
    boldText(g, x + 16 * u_, y + (h - 15 * u_) / 2, tr(lbl), 15 * u_, kTxt, 0.5f * u_);
    const float sw = 50 * u_, sh = 28 * u_, sx = x + w - sw - 16 * u_, sy = y + (h - sh) / 2;
    g.rect(sx, sy, sw, sh, sh / 2, mix(rgba(74, 74, 78), accent, t));
    const float d = sh - 6 * u_;
    g.rect(sx + 3 * u_ + (sw - sh) * t, sy + 3 * u_, d, d, d / 2, rgba(247, 247, 247));
    return changed;
}

bool Menu::slider(const char* lbl, float* v, float mn, float mx, const char* fmt) {
    Gfx& g = *g_;
    const int id = tab_ * 1000 + (++idc_);
    const float h = 68 * u_, y = rowY(h), x = cx_, w = cw_;
    const float tx = x + 18 * u_, tw = w - 36 * u_, trh = 8 * u_, ty = y + h - 20 * u_;
    bool changed = false;

    if (inputOK_ && pressed_ && hit(x, y, w, h, px_, py_)) activeId_ = id;
    if (activeId_ == id) {
        if (down_ || released_) {
            float f = clampf((ix_ - tx) / tw, 0.f, 1.f);
            float nv = mn + (mx - mn) * f;
            if (nv != *v) {
                *v = nv;
                changed = true;
            }
        }
        if (released_) activeId_ = 0;
    }
    if (y + h < top_ || y > bottom_) return changed;

    const bool active = activeId_ == id;
    card(g, x, y, w, h, 18 * u_, active);
    boldText(g, x + 16 * u_, y + 12 * u_, tr(lbl), 15 * u_, kTxt, 0.5f * u_);
    char vb[48];
    std::snprintf(vb, sizeof(vb), fmt, *v);
    // unit suffix ("px", "m/s", "deg", "m") goes through tr(): split on the first space
    char* sp = std::strchr(vb, ' ');
    char out[64];
    if (sp) { *sp = 0; std::snprintf(out, sizeof(out), "%s %s", vb, tr(sp + 1)); } else std::snprintf(out, sizeof(out), "%s", vb);
    g.text(x + w - 16 * u_ - g.textWidth(out, 14 * u_), y + 13 * u_, out, 14 * u_, kDim);

    float f = clampf((*v - mn) / (mx - mn), 0.f, 1.f);
    g.rect(tx, ty, tw, trh, trh / 2, rgba(255, 255, 255, 28));
    g.rect(tx, ty, std::max(trh, tw * f), trh, trh / 2, accent);
    const float kr = (active ? 12 : 10) * u_, kx = tx + tw * f, ky = ty + trh / 2;
    g.rect(kx - kr, ky - kr, kr * 2, kr * 2, kr, rgba(247, 247, 247));
    return changed;
}

bool Menu::choice(const char* lbl, int* v, const char* const* opts, int n) {
    Gfx& g = *g_;
    ++idc_;
    const float h = 52 * u_, y = rowY(h), x = cx_, w = cw_;
    const float cw = 210 * u_, cl = x + w - cw - 12 * u_;
    bool changed = false;
    if (n > 0 && tapped(cl, y, cw, h)) {
        *v = (ix_ < cl + cw / 2) ? (*v + n - 1) % n : (*v + 1) % n;
        changed = true;
    }
    if (y + h < top_ || y > bottom_) return changed;

    card(g, x, y, w, h, 18 * u_, false);
    boldText(g, x + 16 * u_, y + (h - 15 * u_) / 2, tr(lbl), 15 * u_, kTxt, 0.5f * u_);
    g.rect(cl, y + 8 * u_, cw, h - 16 * u_, 12 * u_, rgba(0, 0, 0, 70));
    const char* name = (n > 0) ? tr(opts[*v]) : "-";
    g.text(cl + (cw - g.textWidth(name, 14 * u_)) / 2, y + (h - 14 * u_) / 2, name, 14 * u_, kTxt);
    g.text(cl + 14 * u_, y + (h - 15 * u_) / 2, "<", 15 * u_, accent);
    g.text(cl + cw - 14 * u_ - g.textWidth(">", 15 * u_), y + (h - 15 * u_) / 2, ">", 15 * u_, accent);
    return changed;
}

bool Menu::button(const char* lbl) {
    Gfx& g = *g_;
    ++idc_;
    const float h = 46 * u_, y = rowY(h), x = cx_, w = cw_;
    const bool clicked = tapped(x, y, w, h);
    if (y + h < top_ || y > bottom_) return clicked;
    const bool hd = held(x, y, w, h);
    g.rect(x, y, w, h, h / 2, hd ? mix(accent, rgba(0, 0, 0), 0.25f) : accent);
    const char* t = tr(lbl);
    boldText(g, x + (w - g.textWidth(t, 15 * u_)) / 2, y + (h - 15 * u_) / 2, t, 15 * u_, rgba(255, 255, 255), 0.7f * u_);
    return clicked;
}

void Menu::label(const char* text) {
    Gfx& g = *g_;
    ++idc_;
    const float h = 44 * u_, y = rowY(h);
    if (y + h < top_ || y > bottom_) return;
    card(g, cx_, y, cw_, h, 18 * u_, false);
    boldText(g, cx_ + 16 * u_, y + (h - 14 * u_) / 2, tr(text), 14 * u_, kTxt, 0.5f * u_);
}

void Menu::info(const char* left, const char* right) {
    Gfx& g = *g_;
    ++idc_;
    const float h = 48 * u_, y = rowY(h);
    if (y + h < top_ || y > bottom_) return;
    card(g, cx_, y, cw_, h, 18 * u_, false);
    g.text(cx_ + 16 * u_, y + (h - 14 * u_) / 2, tr(left), 14 * u_, kDim);
    const char* r = tr(right);
    boldText(g, cx_ + cw_ - 16 * u_ - g.textWidth(r, 14 * u_), y + (h - 14 * u_) / 2, r, 14 * u_, kTxt, 0.5f * u_);
}

void Menu::labelInf(const char* text) {
    Gfx& g = *g_;
    ++idc_;
    const float h = 44 * u_, y = rowY(h);
    if (y + h < top_ || y > bottom_) return;
    card(g, cx_, y, cw_, h, 18 * u_, false);
    const float ts = 14 * u_;
    boldText(g, cx_ + 16 * u_, y + (h - ts) / 2, tr(text), ts, kTxt, 0.5f * u_);
    // lemniscate: two overlapping rings (outer white, then card-colored holes)
    const float r = 7 * u_, th = 2.4f * u_, c1 = cx_ + 16 * u_ + g.textWidth(tr(text), ts) + 14 * u_ + r, c2 = c1 + r * 1.55f, cy = y + h / 2;
    g.rect(c1 - r, cy - r, 2 * r, 2 * r, r, kTxt);
    g.rect(c2 - r, cy - r, 2 * r, 2 * r, r, kTxt);
    g.rect(c1 - r + th, cy - r + th, 2 * (r - th), 2 * (r - th), r - th, kRowCol);
    g.rect(c2 - r + th, cy - r + th, 2 * (r - th), 2 * (r - th), r - th, kRowCol);
}

bool Menu::infoCard(const char* title, const char* subtitle, const char* btnLabel, void (*onClick)()) {
    Gfx& g = *g_;
    ++idc_;
    const float pad = 16 * u_;
    const float titleS = 20 * u_, subS = 14 * u_, btnH = 46 * u_;
    const float h = pad * 2 + titleS * 1.25f + 4 * u_ + subS * 1.2f + 14 * u_ + btnH;
    const float y = rowY(h), x = cx_, w = cw_;

    const float bx = x + pad, by = y + h - pad - btnH, bw = w - pad * 2;
    const bool clicked = tapped(bx, by, bw, btnH);
    if (clicked && onClick) onClick();
    if (y + h < top_ || y > bottom_) return clicked;

    g.rect(x, y, w, h, 24 * u_, withA(accent, 0.4f));
    g.rect(x + u_, y + u_, w - 2 * u_, h - 2 * u_, 23 * u_, rgba(60, 43, 44));
    boldText(g, x + pad, y + pad, title, titleS, kTxt, 0.7f * u_);
    g.text(x + pad, y + pad + titleS * 1.25f + 4 * u_, tr(subtitle), subS, kDim);

    const bool hd = held(bx, by, bw, btnH);
    g.rect(bx, by, bw, btnH, btnH / 2, hd ? mix(accent, rgba(0, 0, 0), 0.25f) : accent);
    const char* t = tr(btnLabel);
    boldText(g, bx + (bw - g.textWidth(t, 15 * u_)) / 2, by + (btnH - 15 * u_) / 2, t, 15 * u_, rgba(255, 255, 255), 0.7f * u_);
    return clicked;
}

void Menu::section(const char* title) {
    Gfx& g = *g_;
    ++idc_;
    const float h = 28 * u_, y = rowY(h);
    if (y + h < top_ || y > bottom_) return;
    g.text(cx_ + 6 * u_, y + (h - 14 * u_) / 2, tr(title), 14 * u_, kDim);
}
