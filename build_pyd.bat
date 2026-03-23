@echo off
set RENDERDOC_PYTHON_PREFIX64=C:\Users\fengweiluo\AppData\Roaming\uv\python\cpython-3.10.0-windows-x86_64-none
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Users\fengweiluo\src\renderdoc\qrenderdoc\Code\pyrenderdoc\pyrenderdoc_module.vcxproj" /p:Configuration=Development /p:Platform=x64 /p:SolutionDir=C:\Users\fengweiluo\src\renderdoc\ /p:PlatformToolset=v143 /t:Rebuild /v:normal
