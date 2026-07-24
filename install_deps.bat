@echo off
setlocal
cd /d "%~dp0"

echo === Bootstrap lib/vcpkg ===
call "%~dp0lib\vcpkg\bootstrap-vcpkg.bat"
if errorlevel 1 (
  echo bootstrap-vcpkg.bat failed
  exit /b 1
)

echo.
echo === Install manifest deps into lib\vcpkg\installed (x64-windows) ===
"%~dp0lib\vcpkg\vcpkg.exe" install --triplet x64-windows --x-install-root="%~dp0lib\vcpkg\installed"
if errorlevel 1 (
  echo vcpkg install failed
  exit /b 1
)

echo.
echo Done. Headers/libs: lib\vcpkg\installed\x64-windows\
endlocal
