#pragma once

// Shared frame-path state used by GraphicsSystem lifecycle + frame_loop / publish TUs.
// Not part of the public graphics API — host must not include this.

#include <glm/glm.hpp>

class DebugDrawer;
class MeshArena;

extern DebugDrawer g_debugDrawer;
extern MeshArena*  g_meshArena;
extern glm::mat4   g_viewProj;
