@echo off
setlocal
REM ===============================================================
REM  ESP32-P4 chip-revision checker (Waveshare ESP32-P4-WIFI6 7B)
REM  Prints:  Chip is ESP32-P4 (revision vX.Y)
REM  That silicon rev is what decides the 360 vs 400 MHz stability
REM  question (v1.3 is the one that's unstable at 400).
REM  Close the Arduino Serial Monitor before running.
REM  Usage:  p4-chip-check.bat COM5     (or just double-click and type the port)
REM ===============================================================
set PORT=%1
if "%PORT%"=="" set /p PORT=Enter the board's COM port (e.g. COM5):
echo.
echo Reading chip on %PORT% ...
echo (if it just sits there: hold BOOT, tap RESET, release BOOT, then it connects)
echo.

where esptool >nul 2>&1 && ( esptool --chip esp32p4 -p %PORT% chip_id & goto done )
python -m esptool --chip esp32p4 -p %PORT% chip_id 2>nul && goto done
py -m esptool --chip esp32p4 -p %PORT% chip_id 2>nul && goto done

echo.
echo esptool was not found on PATH.
echo Install it once with:   pip install esptool
echo (Arduino also ships one under
echo  %%LOCALAPPDATA%%\Arduino15\packages\esp32\tools\esptool_py\ -- you can run it from there.)

:done
echo.
echo ^>^> Look for the line:  Chip is ESP32-P4 (revision vX.Y)
echo.
pause
