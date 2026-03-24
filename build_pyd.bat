@echo off
set RENDERDOC_PYTHON_PREFIX64=C:\Users\fengweiluo\AppData\Roaming\uv\python\cpython-3.10.0-windows-x86_64-none
set RENDERDOC_DIR=%~dp0
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "%RENDERDOC_DIR%qrenderdoc\Code\pyrenderdoc\pyrenderdoc_module.vcxproj" /p:Configuration=Development /p:Platform=x64 /p:SolutionDir=%RENDERDOC_DIR% /p:PlatformToolset=v143 /t:Rebuild /v:normal
