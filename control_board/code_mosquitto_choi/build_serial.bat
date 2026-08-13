@echo off
setlocal
set "GCC_BIN=C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin"
set "MAKE_BIN=C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin"
set "PATH=%GCC_BIN%;%MAKE_BIN%;%PATH%"
cd /d "%~dp0Debug"
echo Fixing include paths for Windows (path with ')' ) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fix_debug_include_paths.ps1"
if errorlevel 1 (
  echo fix_debug_include_paths.ps1 failed
  exit /b 1
)
echo Building serial (-j1) ...
make -j1 all
set ERR=%ERRORLEVEL%
if %ERR% NEQ 0 (
  echo Build failed with exit code %ERR%
  exit /b %ERR%
)
echo OK: Debug\Control_board.elf
exit /b 0
