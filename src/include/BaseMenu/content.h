#pragma once
// Forward declaration required by menu.cpp.
// Implemented in cheat_content.cpp, included from the main source after
// all declarations are visible.

#include <cstdint>
#include "gfx.h"   // Col, for kTabColors

class Menu;
void buildContent(Menu& m, int tab);

// Defined in cheat_content.cpp; menu.cpp (included before cheat_content.cpp
// in cheat.cpp) needs these for the tab bar.
extern const char* const kTabs[];
extern const uint32_t    kTabIcons[];   // one baked icon codepoint per tab (0xE000.. — see tools/bake_font.py)
extern const Col         kTabColors[];
extern const int         kTabCount;
