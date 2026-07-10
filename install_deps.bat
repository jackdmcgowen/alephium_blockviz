@echo off
setlocal
cd /d "%~dp0"

echo === Bootstrap vcpkg ===
call "%~dp0vcpkg\bootstrap-vcpkg.bat"
if errorlevel 1 (
  echo bootstrap-vcpkg.bat failed
  exit /b 1
)

echo.
echo === Install manifest deps into vcpkg\installed (x64-windows) ===
"%~dp0vcpkg\vcpkg.exe" install --triplet x64-windows --x-install-root="%~dp0vcpkg\installed"
if errorlevel 1 (
  echo vcpkg install failed
  exit /b 1
)

echo.
echo Done. Headers/libs: vcpkg\installed\x64-windows\
endlocal
