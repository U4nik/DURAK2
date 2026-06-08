@echo off
cls
echo === COMPILING DURAK ===

rem Путь к MinGW
set MINGW_PATH=E:\mingw64\bin

rem Путь к SFML
set SFML_PATH=E:\SFML

rem Добавляем MinGW в PATH
set PATH=%MINGW_PATH%;%PATH%

echo.
echo Compiling...

g++ main.cpp ux.cpp salut.cpp -o durak.exe ^
 -I"%SFML_PATH%\include" ^
 -L"%SFML_PATH%\lib" ^
 -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio ^
 -mwindows

if %errorlevel% neq 0 (
    echo.
    echo !!! COMPILATION FAILED !!!
    pause
    exit /b
)

echo.
echo === BUILD SUCCESSFUL ===
echo Copying SFML DLLs...
copy "%SFML_PATH%\bin\sfml-graphics-2.dll" .
copy "%SFML_PATH%\bin\sfml-window-2.dll" .
copy "%SFML_PATH%\bin\sfml-system-2.dll" .
copy "%SFML_PATH%\bin\sfml-audio-2.dll" .

echo Copying MinGW DLLs...
copy "%MINGW_PATH%\libstdc++-6.dll" .
copy "%MINGW_PATH%\libgcc_s_seh-1.dll" .
copy "%MINGW_PATH%\libwinpthread-1.dll" .




echo.
echo Run durak.exe
pause





