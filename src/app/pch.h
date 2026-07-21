#pragma once

// Precompiled header for alephium_visualizer app product TUs.
// ImGui sources use PrecompiledHeader=NotUsing (stable third-party).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// OS headers live in app/platform/*_platform_<os>.cpp — not in product PCH.

#include <glm/glm.hpp>
