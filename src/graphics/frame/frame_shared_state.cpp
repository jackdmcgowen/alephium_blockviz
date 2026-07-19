#include "graphics/pch.h"
#include "graphics/frame/frame_shared_state.hpp"
#include "graphics/debug/debug_drawer.h"
#include "graphics/mesh_arena.h"

DebugDrawer g_debugDrawer;
MeshArena*  g_meshArena = nullptr;
glm::mat4   g_viewProj{ 1.f };
