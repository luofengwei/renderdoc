@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist "E:\UGitSpace\RDCLoopRunner\renderdoc_src\build-android-arm64\bin" mkdir "E:\UGitSpace\RDCLoopRunner\renderdoc_src\build-android-arm64\bin"
cl.exe /nologo /EHsc /Fe:"E:\UGitSpace\RDCLoopRunner\renderdoc_src\build-android-arm64\bin\include-bin" "E:\UGitSpace\RDCLoopRunner\renderdoc_src\renderdoc\3rdparty\include-bin\main.cpp"
if errorlevel 1 (
    echo FAILED
    exit /b 1
)
echo SUCCESS
dir "E:\UGitSpace\RDCLoopRunner\renderdoc_src\build-android-arm64\bin\include-bin*"
