@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /c /std:c++latest /EHsc /W4 /permissive- /Zc:preprocessor /MDd /I "c:\Users\es.nikulov\source\repos\deep-run" /Fo "build\tmp_main.obj" "c:\Users\es.nikulov\source\repos\deep-run\DeepRun\Main.cpp"
