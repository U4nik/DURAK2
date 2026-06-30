@echo off
cls
echo === COMPILING LAYOUT EDITOR ===

rem Путь к MinGW
set MINGW_PATH=G:\mingw64\bin

rem Путь к SFML
set SFML_PATH=G:\SFML

rem Добавляем MinGW в PATH
set PATH=%MINGW_PATH%;%PATH%

g++ layout_editor.cpp -o layout_editor.exe ^
 -I"%SFML_PATH%\include" ^
 -L"%SFML_PATH%\lib" ^
 -lsfml-graphics -lsfml-window -lsfml-system

if %errorlevel% neq 0 (
    echo.
    echo !!! COMPILATION FAILED !!!
    pause
    exit /b
)

echo.
echo === BUILD SUCCESSFUL ===
echo Copying SFML DLLs...

copy "%SFML_PATH%\bin\*.dll" .

echo.
echo Run layout_editor.exe
pause