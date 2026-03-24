@echo off
setlocal

set JAVA_HOME=C:\Program Files\Android\jdk\jdk-8.0.302.8-hotspot\jdk8u302-b08
set ANDROID_SDK=C:\Users\fengweiluo\AppData\Local\Android\sdk
set ANDROID_NDK=C:\Users\fengweiluo\AppData\Local\Android\sdk\ndk\21.1.6352462
set PATH=%JAVA_HOME%\bin;%PATH%

set CMAKE=C:\Users\fengweiluo\AppData\Local\Android\sdk\cmake\3.10.2.4988404\bin\cmake.exe
set NINJA=C:\Users\fengweiluo\AppData\Local\Android\sdk\cmake\3.10.2.4988404\bin\ninja.exe
set RENDERDOC_DIR=%~dp0
set BUILD_DIR=%RENDERDOC_DIR%build-android-arm64

echo === Step 1a: Pre-build include-bin.exe (host tool) ===
set INCLUDE_BIN=%BUILD_DIR%\bin\include-bin.exe
if not exist "%BUILD_DIR%\bin" mkdir "%BUILD_DIR%\bin"
if not exist "%INCLUDE_BIN%" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    cl.exe /nologo /EHsc /Fe:"%INCLUDE_BIN%" "%RENDERDOC_DIR%renderdoc\3rdparty\include-bin\main.cpp"
    if errorlevel 1 (
        echo ERROR: Failed to build include-bin.exe
        exit /b 1
    )
)

echo === Step 1b: CMake Configure ===
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

"%CMAKE%" .. -DBUILD_ANDROID=ON -DANDROID_ABI=arm64-v8a ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK%\build\cmake\android.toolchain.cmake ^
  -DANDROID_NATIVE_API_LEVEL=21 ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DJava_JAVA_EXECUTABLE="%JAVA_HOME%\bin\java.exe" ^
  -DJava_JAR_EXECUTABLE="%JAVA_HOME%\bin\jar.exe" ^
  -DJava_JAVAC_EXECUTABLE="%JAVA_HOME%\bin\javac.exe" ^
  -DJava_JAVAH_EXECUTABLE="%JAVA_HOME%\bin\javah.exe" ^
  -DJava_JAVADOC_EXECUTABLE="%JAVA_HOME%\bin\javadoc.exe" ^
  -G Ninja
if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

echo === Step 2: Build APK ===
"%CMAKE%" --build . --target apk
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

echo === Step 3: Copy APK to plugins ===
set APK_SRC=%BUILD_DIR%\bin\org.renderdoc.renderdoccmd.arm64.apk
set APK_DST=%RENDERDOC_DIR%x64\Development\plugins\android\org.renderdoc.renderdoccmd.arm64.apk

if not exist "%APK_SRC%" (
    echo ERROR: APK not found at %APK_SRC%
    exit /b 1
)

copy /Y "%APK_SRC%" "%APK_DST%"
echo === Done: APK copied to %APK_DST% ===
