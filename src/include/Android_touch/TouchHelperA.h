#pragma once

#include <linux/input.h>
#include <vector>
#include <functional>
#include "VectorStruct.h"

namespace Touch {
    struct touchObj {
        Vector2 pos{};
        int id = 0;
        bool isDown = false;
    };

    struct Device {
        int fd;
        float S2TX;
        float S2TY;
        input_absinfo absX, absY;
        touchObj Finger[10];

        Device() { memset((void *) this, 0, sizeof(*this)); }
    };

    bool Init(const Vector2 &s, bool p_readOnly);

    void Close();

    void Down(float x, float y);

    void Move(float x, float y);

    void Up();

    void Move(touchObj *touch, float x, float y);

    void Upload();

    void SetCallBack(const std::function<void(std::vector<Device> *)> &cb);

    Vector2 Touch2Screen(const Vector2 &coord);

    Vector2 GetScale();

    void setOrientation(int orientation);

    void setOtherTouch(bool p_otherTouch);
}

// ── shim: API that cheat.cpp expects ──
#include "ANativeWindowCreator.h"
// Rotation used by Touch2Screen. cheat.cpp's initEGL() always swaps W/H so the render canvas is
// landscape-shaped, while the touch panel stays portrait-native: canvas axes and panel axes are
// swapped relative to each other, so only the swapped cases (1 = 90deg, 3 = 270deg) can be right.
// GetDisplayInfo().orientation is not trusted when it says 0/2 (observed: "rot 0" while the canvas is
// landscape) -> fall back to 1. One rule for Init and for the periodic refresh, so they cannot disagree
// (a refresh that wrote the raw value used to undo the Init value ~30 frames later).
inline int g_touchRotRaw = -1;   // last value reported by GetDisplayInfo (diagnostics)
inline int g_touchRot    = 1;    // value actually given to Touch2Screen
inline void Touch_ApplyRotation() {
    int raw = (int)anwc::ANativeWindowCreator::GetDisplayInfo().orientation;
    g_touchRotRaw = raw;
    g_touchRot    = (raw == 1 || raw == 3) ? raw : 1;
    Touch::setOrientation(g_touchRot);
}
inline bool Touch_Init(int w, int h, int /*orient*/, bool readOnly) {
    Touch_ApplyRotation();
    return Touch::Init(Vector2((float)w, (float)h), readOnly);
}
inline void Touch_UpdateImGui() {    // legacy name (ImGui feed is gone): only keeps the rotation fresh
    static int n = 0;
    if (++n % 30 == 0) Touch_ApplyRotation();
}
inline void Touch_Close() { Touch::Close(); }
