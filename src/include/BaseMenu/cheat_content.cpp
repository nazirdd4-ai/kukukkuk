// cheat_content.cpp — buildContent() wired to g_cfg / g_shm / cfgSave / cfgLoad
// Compiled as part of HOST binary only (-DHOST).
// Included directly from cheat_v5_basemenu.cpp after all declarations are visible.

#pragma once   // guard against accidental double-include

// forward declarations of globals defined in cheat_v5_basemenu.cpp
struct Cfg;
extern Cfg g_cfg;
extern volatile bool g_run;
extern int g_cnt, g_sw, g_sh;
struct Shm;
extern Shm* g_shm;
static void cfgSave();
static void cfgLoad();

#include "menu.h"
#include <cstdlib>   // system() for openUrl()

// Tab names / badge icon codepoints / badge colors exposed to Menu (parallel arrays, index-aligned).
// Icon codepoints are baked vector glyphs — see tools/bake_font.py ICON_CODEPOINTS. 0 = no baked icon (Misc draws "</>" as text).
const char* const kTabs[]       = {"Home",       "Aimbot",     "Visuals",    "Memory",     "Misc",       "Settings"};
const uint32_t    kTabIcons[]   = {0xE000,       0xE001,       0xE002,       0xE003,       0,            0xE004};
const Col         kTabColors[]  = {
    rgba(155,  48, 255),   // Home     — purple
    rgba(255,  32,  32),   // Aimbot   — red   (silent aim lives inside this tab)
    rgba( 47, 210,  75),   // Visuals  — green
    rgba( 10, 108, 255),   // Memory   — blue
    rgba(  0,   0,   0),   // Misc     — black
    rgba(109, 109, 114),   // Settings — gray
};
const int         kTabCount = 6;

// shells out to the Android activity manager to open a URL in the default browser.
// requires running as root (same as the rest of the host — see main()'s geteuid() check).
static void openUrl(const char* url) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "am start -a android.intent.action.VIEW -d '%s' >/dev/null 2>&1", url);
    system(cmd);
}

void buildContent(Menu& m, int tab) {
    switch (tab) {

    // ── 0: HOME ───────────────────────────────────────────────────────────
    case 0: {
        m.section("Home");
        m.infoCard("fyxod", "Creator's Telegram channel", "Open channel",
               [] { openUrl("https://t.me/fyxod_cpp"); });

        m.section("FPS settings");
        m.toggle("Unlock FPS", &g_cfg.fpsUnlock);

        m.section("Menu status");
        if (g_licExpiresAtMs > 0) {
            // FILL: g_licExpiresAtMs (declared near g_cfg) is not wired to the license server
            // response yet — licWorker() only yields valid/invalid. 0 = unknown -> shown as infinity.
            int64_t left = g_licExpiresAtMs - (int64_t)nowMs();
            if (left < 0) left = 0;
            int64_t s = left / 1000, d = s / 86400, h = (s % 86400) / 3600, mi = (s % 3600) / 60, se = s % 60;
            char buf[96];
            if (m.lang) snprintf(buf, sizeof(buf), "Осталось: %lldд %lldч %lldмин %lldсек", (long long)d, (long long)h, (long long)mi, (long long)se);
            else        snprintf(buf, sizeof(buf), "Expires in: %lldd %lldh %lldmin %lldsec", (long long)d, (long long)h, (long long)mi, (long long)se);
            m.label(buf);
        } else {
            m.labelInf(m.lang ? "Срок действия:" : "Expires in:");
        }

        m.section("About");
        m.info("Mode:", "Direct");
        break;
    }

    // ── 1: AIMBOT (incl. Silent Aim) ─────────────────────────────────────
    case 1:
        m.section("Aimbot");
        m.toggle("Aim assist",  &g_cfg.aim);
        m.toggle("Hold button", &g_cfg.aimHold);
        m.toggle("Teammates",   &g_cfg.aimTeam);
        m.toggle("Lead target", &g_cfg.lead);

        if (g_shm) {
            if (!g_shm->ctl.ready)
                m.label("payload not ready");
            else if (g_shm->updateOk < 0)
                m.label("core hook FAILED — RVAs stale, redump dump.cs");
            else if (g_shm->updateOk == 0)
                m.label("core hook pending — enter a match");
        }

        m.section("Tuning");
        m.slider("FOV radius",   &g_cfg.aimFov,    40.f,  600.f, "%.0f px");
        m.slider("Smoothing",    &g_cfg.aimSmooth,  1.f,   20.f,  "%.1f");
        m.slider("Bullet speed", &g_cfg.leadSpeed,  0.f,  800.f, "%.0f m/s");
        m.slider("Aim height",   &g_cfg.aimH,       0.3f,   1.f,  "%.2f");
        m.slider("Camera VFOV",  &g_cfg.vfov,      30.f,  110.f, "%.0f deg");

        m.section("Silent aim");
        m.toggle("Enable silent", &g_cfg.silent);

        if (g_cfg.silent && g_shm) {
            if      (g_shm->ctl.silentOk > 0) m.label("hook live");
            else if (g_shm->ctl.silentOk < 0) m.label("install failed — RVA stale");
            else                               m.label("installs on next match frame");
        }

        m.slider("FOV",          &g_cfg.silentFov, 40.f, 600.f, "%.0f px");
        m.slider("Max distance", &g_cfg.maxDist,   25.f, 600.f, "%.0f m");
        {
            static const char* const bn[5] = {"Head", "Neck", "Chest", "Pelvis", "Closest"};
            m.choice("Target bone", &g_cfg.aimBone, bn, 5);
        }
        break;

    // ── 2: VISUALS (ESP) ──────────────────────────────────────────────────
    case 2:
        m.section("ESP");
        m.toggle("ESP",        &g_cfg.esp);
        m.toggle("Box",        &g_cfg.box);
        m.toggle("Health bar", &g_cfg.hp);
        m.toggle("Distance",   &g_cfg.dist);
        m.toggle("Names",      &g_cfg.names);
        m.toggle("Skeleton",   &g_cfg.skeleton);
        m.toggle("Tracers",    &g_cfg.lines);
        m.toggle("Teammates",  &g_cfg.team);
        m.section("Style");
        m.slider("Line width", &g_cfg.lineWidth, 1.f, 5.f, "%.1f px");
        break;

    // ── 3: MEMORY ─────────────────────────────────────────────────────────
    case 3:
        m.section("Memory");
        {
            char buf[64];
            if (g_cnt > 0) {
                snprintf(buf, sizeof(buf), m.lang ? "Объектов отслеживается: %d" : "%d entities tracked", g_cnt);
                m.label(buf);
            } else {
                m.label("no data yet");
            }
            if (g_sw > 0) {
                snprintf(buf, sizeof(buf), m.lang ? "Разрешение игры: %dx%d" : "game res: %dx%d", g_sw, g_sh);
                m.label(buf);
            }
        }
        break;

    // ── 4: MISC ───────────────────────────────────────────────────────────
    case 4:
        m.section("Misc");
        m.toggle("No spread",       &g_cfg.noSpread);
        m.toggle("No recoil",       &g_cfg.noRecoil);
        m.toggle("Draw FOV circle", &g_cfg.aimCircle);
        break;

    // ── 5: SETTINGS ───────────────────────────────────────────────────────
    case 5:
        m.section("Config");
        m.choice("Slot", &g_cfgSlot, kCfgNames, kCfgSlots);
        if (m.button("Save config"))    cfgSave();
        if (m.button("Load config"))    cfgLoad();
        m.slider("Menu opacity",  &g_cfg.menuOpacity, 0.3f, 1.f, "%.2f");
        m.slider("UI scale",      &m.uiScale,         0.7f, 1.6f, "%.2f");

        m.section("Interface");
        {
            static const char* const ln[2] = {"English", "Русский"};
            m.choice("Language", &m.lang, ln, 2);
            m.toggle("Show FPS counter", &m.showFps);
        }

        m.section("Accent");
        {
            static int accentIdx = 0;
            static const char* const kAccentNames[] = {"Red", "Purple", "Blue", "Green", "Orange"};
            static const Col kAccents[5] = {
                rgba(255,  59,  59),
                rgba(139, 108, 255),
                rgba( 58, 141, 255),
                rgba( 47, 210,  75),
                rgba(255, 150,  50),
            };
            if (m.choice("Color", &accentIdx, kAccentNames, 5))
                m.accent = kAccents[accentIdx];
        }
        m.section("Exit");
        {
            static bool armed = false;
            if (!armed) {
                if (m.button("Exit")) armed = true;
            } else {
                if (m.button("Tap again to confirm")) g_run = false;
            }
        }
        break;
    }
}
