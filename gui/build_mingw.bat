@echo off
setlocal
cd /d "%~dp0"

if defined MINGW_BIN set "PATH=%MINGW_BIN%;%PATH%"
if not defined CXX set CXX=g++
if not defined WINDRES set WINDRES=windres

if defined GPMUI if exist "%GPMUI%\core\gpm_ui.h" goto gpmui_ready
set GPMUI=%~dp0ui_d2d
if exist "%GPMUI%\core\gpm_ui.h" goto gpmui_ready
:gpmui_ready
if not exist "%GPMUI%\core\gpm_ui.h" (
  echo GPM UI runtime not found.
  echo Checked:
  echo   %~dp0ui_d2d
  echo You can also set GPMUI to a custom path before running this script.
  exit /b 1
)
set OUT=gpm-fluent-new.exe
set OBJ=obj

set CFLAGS=-std=c++17 -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -I"%GPMUI%\core" -O2 -flto -fdata-sections -ffunction-sections -fvisibility=hidden -fno-rtti -fno-stack-protector
if /I "%1"=="debug" set CFLAGS=%CFLAGS% -DGPM_DEBUG_UI -g
set LDFLAGS=-mwindows -municode -ld2d1 -ldwrite -lwindowscodecs -lcomctl32 -lshcore -lole32 -lgdi32 -luser32 -limm32 -lwinhttp -lshell32 -lcomdlg32 -lwinmm -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -Wl,-O2 -Wl,--strip-all -s

if not exist %OBJ% mkdir %OBJ%
del /q %OBJ%\*.o 2>nul

echo [1/3] Compiling GPM D2D UI modules...
set SRCS=%GPMUI%\core\GpmTheme.cpp %GPMUI%\core\GpmApp.cpp %GPMUI%\core\GpmUIBase.cpp %GPMUI%\core\GpmWindow.cpp
set SRCS=%SRCS% %GPMUI%\controls\GpmButton.cpp %GPMUI%\controls\GpmLabel.cpp %GPMUI%\controls\GpmCheckBox.cpp
set SRCS=%SRCS% %GPMUI%\controls\GpmSlider.cpp %GPMUI%\controls\GpmComboBox.cpp %GPMUI%\controls\GpmProgressBar.cpp
set SRCS=%SRCS% %GPMUI%\controls\GpmListBox.cpp %GPMUI%\controls\GpmImageButton.cpp %GPMUI%\controls\GpmEdit.cpp
set SRCS=%SRCS% %GPMUI%\controls\GpmTabControl.cpp %GPMUI%\controls\GpmRadioButton.cpp %GPMUI%\controls\GpmGraphButton.cpp
set SRCS=%SRCS% %GPMUI%\controls\GpmSuperListBox.cpp %GPMUI%\controls\GpmDataGrid.cpp %GPMUI%\controls\GpmMessageBox.cpp

for %%f in (%SRCS%) do (
  echo   %%~nxf
  "%CXX%" %CFLAGS% -c "%%f" -o "%OBJ%\%%~nf.o"
  if errorlevel 1 exit /b 1
)

echo [2/3] Compiling app...
"%CXX%" %CFLAGS% -c main.cpp -o "%OBJ%\main.o"
if errorlevel 1 exit /b 1

echo [3/4] Compiling resources...
"%WINDRES%" app.rc -O coff -o "%OBJ%\app_res.o"
if errorlevel 1 exit /b 1

echo [4/4] Linking %OUT%...
"%CXX%" %OBJ%\*.o -o %OUT% %LDFLAGS%
if errorlevel 1 exit /b 1

echo Build OK: %CD%\%OUT%
endlocal
