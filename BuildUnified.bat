@echo off
setlocal
cd /d "%~dp0"

echo [1/2] Running win_bison on Exp2_Parser.y ...
win_bison -d -o Exp2_Parser.tab.c Exp2_Parser.y
if errorlevel 1 (
    echo.
    echo win_bison failed. Please check the error messages above.
    pause
    exit /b 1
)

echo.
echo [2/2] Building UnifiedCompiler.exe ...
gcc -DPARSER_MODE -DEXP2_DISABLE_STANDALONE_MAIN Main.c Exp3_CodeGen.c Exp2_Parser.tab.c Exp1_Lexer.c -o UnifiedCompiler.exe
if errorlevel 1 (
    echo.
    echo Build failed. Please check the error messages above.
    pause
    exit /b 1
)

echo.
echo Build finished: UnifiedCompiler.exe
pause
