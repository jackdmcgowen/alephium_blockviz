#!/usr/bin/env bash
# Bootstrap deps for Linux (and optional CMake toolchain via vcpkg).
# Prefer distro packages when available; fall back to vcpkg x64-linux.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

TRIPLET="${VCPKG_DEFAULT_TRIPLET:-x64-linux}"
INSTALL_ROOT="${ROOT}/vcpkg/installed"

echo "=== Submodules (imgui, vcpkg) ==="
git submodule update --init --recursive imgui vcpkg || true

if [[ -x /usr/bin/apt-get ]] && command -v sudo >/dev/null 2>&1; then
  echo "=== Optional: apt packages (needs sudo) ==="
  echo "  sudo apt-get install -y build-essential cmake ninja-build python3 \\"
  echo "    libvulkan-dev vulkan-tools glslang-tools libglfw3-dev \\"
  echo "    libcurl4-openssl-dev libcjson-dev libglm-dev zlib1g-dev"
fi

echo "=== Bootstrap vcpkg ==="
if [[ ! -x "${ROOT}/vcpkg/vcpkg" ]]; then
  (cd "${ROOT}/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
fi

echo "=== vcpkg install (triplet ${TRIPLET}) ==="
# Manifest mode + extra GLFW for Linux host
"${ROOT}/vcpkg/vcpkg" install --triplet "${TRIPLET}" --x-install-root="${INSTALL_ROOT}"
# Ensure glfw3 present (not always in manifest historically)
"${ROOT}/vcpkg/vcpkg" install glfw3 vulkan-headers glslang --triplet "${TRIPLET}" --x-install-root="${INSTALL_ROOT}" || true

echo "=== glslc check ==="
if command -v glslc >/dev/null 2>&1; then
  glslc --version | head -1
else
  # glslang from vcpkg
  if [[ -x "${INSTALL_ROOT}/${TRIPLET}/tools/glslang/glslc" ]]; then
    echo "glslc at ${INSTALL_ROOT}/${TRIPLET}/tools/glslang/glslc"
  else
    echo "WARNING: glslc not found — add Vulkan SDK or glslang-tools to PATH"
  fi
fi

echo "Done."
echo "Configure example:"
echo "  cmake -S . -B build -G Ninja \\"
echo "    -DCMAKE_TOOLCHAIN_FILE=${ROOT}/vcpkg/scripts/buildsystems/vcpkg.cmake \\"
echo "    -DVCPKG_TARGET_TRIPLET=${TRIPLET} \\"
echo "    -DCMAKE_BUILD_TYPE=Debug"
echo "  cmake --build build"
echo "  ./build/bin/alephium_visualizer   # cwd = repo root"
