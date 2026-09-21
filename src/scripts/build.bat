@echo off

REM ### BUILD FRICTION ON WINDOWS
REM # Copyright (c) Ole-André Rodlie and contributors
REM # GPL3

set OPT=%1
set REL=OFF
set BTYPE=Release
set BDIR=release
set CBUILD=

if "%OPT%" == "release" (
    set REL=ON
)
if "%OPT%" == "ci" (
    set CBUILD=CI
)
if "%OPT%" == "debug" (
    set BTYPE=Debug
    set BDIR=debug
)

set CWD=%cd%
set SDK_DIR=%CWD%\sdk
set SDK_VERSION=1.0.0
set SDK_REV=r7
set SDK_SUFFIX=windows-x64.7z

set PATH=%SDK_DIR%\bin;%PATH%

git submodule update --init --recursive

set BRANCH=
for /f %%I in ('git rev-parse --abbrev-ref HEAD 2^> NUL') do set BRANCH=%%I

set COMMIT=
for /f %%i in ('git rev-parse --short^=8 HEAD') do set COMMIT=%%i

if not exist "sdk\" (
    curl -OL "https://github.com/friction2d/friction-sdk/releases/download/v%SDK_VERSION%/friction-sdk-%SDK_VERSION%%SDK_REV%-%SDK_SUFFIX%"
    7z x friction-sdk-%SDK_VERSION%%SDK_REV%-%SDK_SUFFIX%
)

rem FFmpeg 9.0.2 win64 gpl-shared (BtbN, pinned by immutable release id).
rem Provides the headers (sdk\ffmpeg-win), import libs (sdk\bin) and the
rem runtime DLLs staged below - the official SDK only ships 4.2 which the
rem code no longer compiles against (avcodec_get_supported_config etc.)
set FFMPEG9_ZIP=ffmpeg-n9.0-latest-win64-gpl-shared-9.0.zip
if not exist "sdk\ffmpeg-win\libavutil\avutil.h" (
    curl -L -o "%FFMPEG9_ZIP%" "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n9.0-latest-win64-gpl-shared-9.0.zip"
    7z x "%FFMPEG9_ZIP%" -osdk
    ren "sdk\ffmpeg-n9.0-latest-win64-gpl-shared-9.0" ffmpeg9
    robocopy "sdk\ffmpeg9\include" "sdk\ffmpeg-win" /E /NFL /NDL /NJH /NJS >nul
    copy "sdk\ffmpeg9\lib\*.lib" "sdk\bin\" >nul
    if exist "sdk\ffmpeg-win\libavutil\avutil.h" echo ffmpeg 9.0.2 staged
)

rem vtracer (bitmap tracing) is a Rust FFI crate - build it and drop the
rem dll into sdk\bin so the staging below finds it (official SDK has none;
rem without this the feature silently ships dead, pencil-brush-style)
if not exist "sdk\bin\vtracer.dll" (
    cd "%CWD%\src\vtracer-ffi"
    cargo build --release
    copy "target\release\vtracer.dll" "%CWD%\sdk\bin\" >nul
    cd "%CWD%"
    if exist "sdk\bin\vtracer.dll" echo vtracer built
)

if exist "build\" (
    @RD /S /Q build
)
mkdir build

cd "%CWD%\build"
mkdir output

rem Qt6 toolchain comes from install-qt-action (Qt6_DIR), qscintilla2-qt6 is
rem pre-built by the workflow into %QSCINTILLA_DIR% (qmake+nmake, release\)
rem SDK still provides skia + ffmpeg + vtracer (all Qt-independent).
if "%QT_ROOT_DIR%" == "" (
    echo ERROR: Qt6_DIR not set - install Qt 6.8 first & exit /b 1
)
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=%BTYPE% -DCMAKE_PREFIX_PATH=%SDK_DIR% -DQt6_DIR=%QT_ROOT_DIR%\lib\cmake\Qt6 -DQSCINTILLA_SRC_DIR=%QSCINTILLA_DIR% -DCUSTOM_BUILD=%CBUILD% -DBUILD_SKIA=OFF -DFRICTION_OFFICIAL_RELEASE=%REL% -DWIN_DEPLOY=ON -DGIT_COMMIT=%COMMIT% -DGIT_BRANCH=%BRANCH% ..
set /p VERSION=<version.txt
cmake --build . --config %BTYPE%

if "%REL%" == "OFF" (
    set VERSION="%VERSION%-%COMMIT%"
)

set BUILD_OUTPUT="%CWD%\build\output"
set OUTPUT_DIR="%BUILD_OUTPUT%\friction-%VERSION%"

mkdir "%OUTPUT_DIR%"

copy "%CWD%\build\src\core\%BDIR%\frictioncore.dll" "%OUTPUT_DIR%\"
copy "%CWD%\build\src\ui\%BDIR%\frictionui.dll" "%OUTPUT_DIR%\"
copy "%CWD%\build\src\app\%BDIR%\friction.exe" "%OUTPUT_DIR%\"

rem Lottie playback FFI library (runtime QLibrary load)
copy "%CWD%\build\src\skottie-ffi\%BDIR%\frictionskottie.dll" "%OUTPUT_DIR%\"

rem skia from SDK (Qt-independent)
copy "%SDK_DIR%\bin\skia.dll" "%OUTPUT_DIR%\"

rem qscintilla2-qt6 (built by workflow)
copy "%QSCINTILLA_DIR%\release\qscintilla2_qt6.dll" "%OUTPUT_DIR%\"

rem Qt6 runtime + plugins (platforms/audio/imageformats/translations) via windeployqt
"%QT_ROOT_DIR%\bin\windeployqt.exe" --release --no-compiler-runtime "%OUTPUT_DIR%\friction.exe"
if errorlevel 1 (
    echo ERROR: windeployqt failed & exit /b 1
)

rem FFmpeg 9 runtime DLLs (avcodec-63/avutil-61/swscale-10/... from BtbN)
copy "%CWD%\sdk\ffmpeg9\bin\av*.dll" "%OUTPUT_DIR%\"
copy "%CWD%\sdk\ffmpeg9\bin\sw*.dll" "%OUTPUT_DIR%\"

rem vector trace (QLibrary runtime-loaded)
copy "%SDK_DIR%\bin\vtracer.dll" "%OUTPUT_DIR%\"

rem VC++ runtime app-local: clean machines without the redistributable
rem cannot start the app; System32 always has them where a VC toolchain/redist exists
copy "%SystemRoot%\System32\msvcp140.dll" "%OUTPUT_DIR%\"
copy "%SystemRoot%\System32\msvcp140_1.dll" "%OUTPUT_DIR%\"
copy "%SystemRoot%\System32\vcruntime140.dll" "%OUTPUT_DIR%\"
copy "%SystemRoot%\System32\vcruntime140_1.dll" "%OUTPUT_DIR%\"

echo "Delete this file if you want to disable portable mode" > "%OUTPUT_DIR%\portable.txt"

cd "%BUILD_OUTPUT%"

7z a -mx9 friction-%VERSION%-windows-x64.7z friction-%VERSION%

if exist "%CWD%\build\src\app\%BDIR%\friction.iss" (
    copy "%CWD%\build\src\app\%BDIR%\friction.iss" "%OUTPUT_DIR%\"
) else (
    copy "%CWD%\build\src\app\friction.iss" "%OUTPUT_DIR%\"
)
copy "%CWD%\src\app\icons\friction.ico" "%OUTPUT_DIR%\"
copy "%CWD%\src\app\icons\friction.bmp" "%OUTPUT_DIR%\"
copy "%CWD%\LICENSE.md" "%OUTPUT_DIR%\"

cd "%OUTPUT_DIR%"

iscc friction.iss
copy "setup\friction.exe" "%BUILD_OUTPUT%\friction-%VERSION%-setup-win64.exe"
