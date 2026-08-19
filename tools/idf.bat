@echo off
setlocal EnableDelayedExpansion
REM Drive ESP-IDF from Windows.
REM
REM   tools\idf.bat build
REM   tools\idf.bat -p COM7 flash
REM   tools\idf.bat -DFMSUI_DEMO=reorder -DFMSUI_ROWS=40 build
REM
REM tools/idf.sh does the same thing from WSL, but it can only work while WSL's
REM binfmt_misc interop registration is alive -- and with systemd enabled that
REM registration is periodically flushed, at which point launching cmd.exe from
REM WSL fails with "Exec format error". This script needs no interop, so it is
REM the one to reach for when the device build has to work.
REM
REM Override the defaults from the environment if your install differs:
REM   set FMSUI_IDF=C:\esp\v5.5.4\esp-idf
REM   set IDF_TOOLS_PATH=C:\Espressif

if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%FMSUI_IDF%"=="" set FMSUI_IDF=C:\esp\v5.5.4\esp-idf

if not exist "%FMSUI_IDF%\export.bat" (
    echo error: no ESP-IDF at "%FMSUI_IDF%" ^(no export.bat^)>&2
    echo        set FMSUI_IDF to your esp-idf directory. This project needs 5.5:>&2
    echo        espressif/usb calls the 5.5 HAL, so 5.4.x does not build.>&2
    exit /b 1
)

REM export.bat derives its virtualenv name from whichever `python` it finds
REM first on PATH. The system default here is newer than the interpreter the
REM ESP-IDF environment was created with, so put the bundled one in front or
REM export.bat goes looking for a venv that was never created. Take the
REM highest-numbered one, which is what tools/idf.sh does with `sort -V`.
set IDF_PY_DIR=
for /d %%d in ("%IDF_TOOLS_PATH%\tools\idf-python\*") do set IDF_PY_DIR=%%d
if "%IDF_PY_DIR%"=="" (
    echo error: no idf-python under "%IDF_TOOLS_PATH%\tools\idf-python">&2
    echo        set IDF_TOOLS_PATH to your Espressif tools directory.>&2
    exit /b 1
)
set PATH=%IDF_PY_DIR%;%PATH%

REM The project is the directory above this script, wherever it was cloned.
cd /d "%~dp0.." || exit /b 1

echo [idf.bat] ESP-IDF "%FMSUI_IDF%", python "%IDF_PY_DIR%">&2

call "%FMSUI_IDF%\export.bat" >nul
if errorlevel 1 (
    echo error: export.bat failed>&2
    exit /b 1
)

REM No `|| true` anywhere: idf.sh had one, and it turned "no ESP-IDF found" into
REM a successful-looking exit that cost an afternoon.
idf.py %*
exit /b %ERRORLEVEL%
