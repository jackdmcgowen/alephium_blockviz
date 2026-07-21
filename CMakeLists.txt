cmake_minimum_required(VERSION 3.20)
project(alephium_blockviz LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_ROOT "${REPO_ROOT}/src")
set(IMGUI_ROOT "${REPO_ROOT}/imgui")

option(BLOCKVIZ_BUILD_APP "Build product executable" ON)
option(BLOCKVIZ_BUILD_VNV "Build mod_* VnV executables (CPU-only)" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_EXECUTABLE)
  find_program(Python3_EXECUTABLE NAMES python3 python REQUIRED)
endif()

find_path(CJSON_INCLUDE_DIR cjson/cJSON.h)
find_library(CJSON_LIBRARY NAMES cjson cJSON)
if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIBRARY)
  message(FATAL_ERROR "cJSON not found (headers + library). Install libcjson-dev or vcpkg cjson.")
endif()

find_path(GLM_INCLUDE_DIR glm/glm.hpp)
if(NOT GLM_INCLUDE_DIR)
  message(FATAL_ERROR "glm not found. Install libglm-dev or vcpkg glm.")
endif()

if(NOT WIN32)
  find_package(glfw3 CONFIG QUIET)
  if(NOT TARGET glfw AND NOT TARGET glfw3)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(GLFW3 IMPORTED_TARGET glfw3)
    endif()
    if(NOT TARGET PkgConfig::GLFW3)
      find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h)
      find_library(GLFW_LIBRARY NAMES glfw glfw3)
      if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
        message(FATAL_ERROR "GLFW not found. Install libglfw3-dev or vcpkg glfw3.")
      endif()
      add_library(glfw UNKNOWN IMPORTED)
      set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${GLFW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}")
    endif()
  endif()
endif()

find_program(GLSLC_EXECUTABLE NAMES glslc
  HINTS
    "$ENV{VULKAN_SDK}/bin"
    "${REPO_ROOT}/vcpkg/installed/x64-linux/tools/glslang"
    "${REPO_ROOT}/vcpkg/installed/x64-windows/tools/glslang"
)
if(NOT GLSLC_EXECUTABLE)
  message(WARNING "glslc not found — pre-build .spv must already exist under src/graphics/shaders/")
endif()

# ---------------------------------------------------------------------------
# Platform sources (exactly one OS variant)
# ---------------------------------------------------------------------------
if(WIN32)
  set(APP_PLATFORM_SRC src/app/platform/app_platform_win32.cpp)
  set(GFX_PLATFORM_SRC src/graphics/platform/gfx_platform_win32.cpp)
  set(NET_PLATFORM_SRC src/network/platform/net_platform_win32.cpp)
  set(IMGUI_PLATFORM_SRC ${IMGUI_ROOT}/backends/imgui_impl_win32.cpp)
else()
  set(APP_PLATFORM_SRC src/app/platform/app_platform_linux.cpp)
  set(GFX_PLATFORM_SRC src/graphics/platform/gfx_platform_linux.cpp)
  set(NET_PLATFORM_SRC src/network/platform/net_platform_linux.cpp)
  set(IMGUI_PLATFORM_SRC ${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp)
endif()

function(blockviz_link_glfw tgt)
  if(WIN32)
    return()
  endif()
  if(TARGET glfw)
    target_link_libraries(${tgt} PUBLIC glfw)
  elseif(TARGET glfw3)
    target_link_libraries(${tgt} PUBLIC glfw3)
  elseif(TARGET PkgConfig::GLFW3)
    target_link_libraries(${tgt} PUBLIC PkgConfig::GLFW3)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------
set(SHADER_DIR "${SRC_ROOT}/graphics/shaders")
if(GLSLC_EXECUTABLE AND Python3_EXECUTABLE)
  add_custom_target(compile_shaders
    COMMAND ${CMAKE_COMMAND} -E env "PATH=$ENV{PATH}"
            ${Python3_EXECUTABLE} "${SHADER_DIR}/compile_shaders.py"
    WORKING_DIRECTORY "${SHADER_DIR}"
    COMMENT "Compile GLSL → SPIR-V (glslc)"
    VERBATIM)
else()
  add_custom_target(compile_shaders
    COMMAND ${CMAKE_COMMAND} -E echo "Skipping shader compile (glslc/python missing)"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Common flags
# ---------------------------------------------------------------------------
set(BLOCKVIZ_PUBLIC_INCLUDES ${SRC_ROOT} ${GLM_INCLUDE_DIR} ${CJSON_INCLUDE_DIR})

function(blockviz_target_common tgt)
  target_include_directories(${tgt} PRIVATE ${BLOCKVIZ_PUBLIC_INCLUDES})
  # GLM force flags are defined in product pch.h — do not redefine on command line.
  # Vulkan-only: never pull OpenGL headers via GLFW (imgui_impl_glfw includes glfw3.h).
  if(NOT WIN32)
    target_compile_definitions(${tgt} PRIVATE GLFW_INCLUDE_NONE)
  endif()
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W3 /MP /permissive-)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# network
# ---------------------------------------------------------------------------
add_library(network STATIC
  src/network/commands.c
  src/network/network_system.cpp
  src/network/alephium/network_poller.cpp
  src/network/alephium/block_fetch_pool.cpp
  src/network/alephium/http_transport.cpp
  src/network/alephium/http_io_pool.cpp
  src/network/alephium/alephium_adapter.cpp
  src/network/alephium/main_chain_cache.cpp
  src/network/alephium/alph_detail_store.cpp
  src/network/alephium/segment_disk_cache.cpp
  src/network/fake/fake_chain_simulator.cpp
  ${NET_PLATFORM_SRC}
)
blockviz_target_common(network)
target_include_directories(network PRIVATE ${SRC_ROOT}/network)
target_link_libraries(network PUBLIC CURL::libcurl ZLIB::ZLIB ${CJSON_LIBRARY} Threads::Threads)

# ---------------------------------------------------------------------------
# graphics
# ---------------------------------------------------------------------------
add_library(graphics STATIC
  src/graphics/buffer.cpp
  src/graphics/buffer_manager.cpp
  src/graphics/camera.cpp
  src/graphics/debug/debug_drawer.cpp
  src/graphics/mesh_arena.cpp
  src/graphics/device.cpp
  src/graphics/descriptor.cpp
  src/graphics/engine_requirements.cpp
  src/graphics/image.cpp
  src/graphics/instance.cpp
  src/graphics/pipeline.cpp
  src/graphics/shader.cpp
  src/graphics/surface.cpp
  src/graphics/swapchain.cpp
  src/graphics/validation.cpp
  src/graphics/graphics_system.cpp
  src/graphics/frame/frame_sync.cpp
  src/graphics/frame/frame_resources.cpp
  src/graphics/frame/frame_descriptors.cpp
  src/graphics/frame/frame_recorder.cpp
  src/graphics/frame/frame_presenter.cpp
  src/graphics/frame/frame_graph/frame_task_graph.cpp
  src/graphics/frame/profiling/timestamp_query_pool.cpp
  src/graphics/frame/profiling/frame_profiler.cpp
  src/graphics/frame/frame_shared_state.cpp
  src/graphics/frame/selection_state.cpp
  src/graphics/frame/gpu_frame_publish.cpp
  src/graphics/frame/sobel_async_pass.cpp
  src/graphics/frame/frame_loop.cpp
  src/graphics/frame/screenshot.cpp
  src/graphics/frame/swapchain_targets.cpp
  src/graphics/frame/picker.cpp
  src/graphics/pipelines/sobel_pipeline.cpp
  src/graphics/pipelines/cube_pipeline.cpp
  src/graphics/pipelines/picker_pipeline.cpp
  ${GFX_PLATFORM_SRC}
  ${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp
)
blockviz_target_common(graphics)
target_include_directories(graphics PRIVATE
  ${SRC_ROOT}/graphics
  ${IMGUI_ROOT}
  ${IMGUI_ROOT}/backends
  ${Vulkan_INCLUDE_DIRS}
)
target_link_libraries(graphics PUBLIC Vulkan::Vulkan Threads::Threads ${CMAKE_DL_LIBS})
blockviz_link_glfw(graphics)
add_dependencies(graphics compile_shaders)

# ---------------------------------------------------------------------------
# engine
# ---------------------------------------------------------------------------
add_library(blockviz_engine STATIC
  src/engine/engine.cpp
)
blockviz_target_common(blockviz_engine)
target_include_directories(blockviz_engine PRIVATE ${SRC_ROOT}/engine)
target_link_libraries(blockviz_engine PUBLIC graphics network)

# ---------------------------------------------------------------------------
# app
# ---------------------------------------------------------------------------
if(BLOCKVIZ_BUILD_APP)
  add_executable(alephium_visualizer
    src/app/main.cpp
    src/app/config.c
    src/app/blockflow_overlay.cpp
    src/app/scene_presenter.cpp
    src/domain/block_graph.cpp
    src/domain/block_scene.cpp
    src/domain/layout.cpp
    ${APP_PLATFORM_SRC}
    ${IMGUI_ROOT}/imgui.cpp
    ${IMGUI_ROOT}/imgui_draw.cpp
    ${IMGUI_ROOT}/imgui_tables.cpp
    ${IMGUI_ROOT}/imgui_widgets.cpp
    ${IMGUI_PLATFORM_SRC}
  )
  blockviz_target_common(alephium_visualizer)
  target_include_directories(alephium_visualizer PRIVATE
    ${IMGUI_ROOT}
    ${IMGUI_ROOT}/backends
    ${Vulkan_INCLUDE_DIRS}
  )
  target_link_libraries(alephium_visualizer PRIVATE blockviz_engine ${CJSON_LIBRARY})
  blockviz_link_glfw(alephium_visualizer)
  set_target_properties(alephium_visualizer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
endif()

# ---------------------------------------------------------------------------
# VnV mod (CPU-only — mirrors sln/mod_domain + sln/mod_network)
# ---------------------------------------------------------------------------
if(BLOCKVIZ_BUILD_VNV)
  enable_testing()

  add_executable(mod_domain
    vnv/mod/tests/domain/main.cpp
    vnv/mod/tests/domain/add_idempotent_and_confirm/test.cpp
    vnv/mod/tests/domain/remove_erases_confirmed/test.cpp
    vnv/mod/tests/domain/confirm_readmit_after_remove/test.cpp
    vnv/mod/tests/domain/detail_slim_pin/test.cpp
    vnv/mod/tests/domain/detail_via_scene/test.cpp
    vnv/mod/tests/domain/graph_lane/test.cpp
    vnv/mod/tests/domain/prune_protects_frontier/test.cpp
    vnv/mod/tests/domain/alph_out_sum_slim/test.cpp
    src/domain/block_graph.cpp
    src/domain/block_scene.cpp
    src/network/alephium/alph_detail_store.cpp
  )
  target_include_directories(mod_domain PRIVATE ${REPO_ROOT}/vnv ${SRC_ROOT} ${CJSON_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
  target_compile_definitions(mod_domain PRIVATE GLM_FORCE_LEFT_HANDED GLM_FORCE_DEPTH_ZERO_TO_ONE)
  if(NOT MSVC)
    target_compile_options(mod_domain PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-class-memaccess)
  endif()
  target_link_libraries(mod_domain PRIVATE ${CJSON_LIBRARY})
  set_target_properties(mod_domain PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_test(NAME mod_domain COMMAND mod_domain WORKING_DIRECTORY ${REPO_ROOT})

  add_executable(mod_network
    vnv/mod/tests/network/main.cpp
    vnv/mod/tests/network/enqueue_block_and_drain/test.cpp
    vnv/mod/tests/network/parallel_intervals/test.cpp
    vnv/mod/tests/network/interval_dedupe/test.cpp
    vnv/mod/tests/network/stop_clean/test.cpp
    vnv/mod/tests/network/inflight_interval_cap/test.cpp
    src/network/alephium/http_transport.cpp
    src/network/alephium/http_io_pool.cpp
  )
  target_include_directories(mod_network PRIVATE ${REPO_ROOT}/vnv ${SRC_ROOT})
  if(NOT MSVC)
    target_compile_options(mod_network PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
  target_link_libraries(mod_network PRIVATE CURL::libcurl Threads::Threads)
  set_target_properties(mod_network PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_test(NAME mod_network COMMAND mod_network WORKING_DIRECTORY ${REPO_ROOT})
endif()

message(STATUS "alephium_blockviz platform=${CMAKE_SYSTEM_NAME}")
message(STATUS "  Vulkan=${Vulkan_LIBRARIES}")
message(STATUS "  cJSON=${CJSON_LIBRARY}")
message(STATUS "  glm=${GLM_INCLUDE_DIR}")
message(STATUS "  glslc=${GLSLC_EXECUTABLE}")
message(STATUS "  VnV mod=${BLOCKVIZ_BUILD_VNV}")
