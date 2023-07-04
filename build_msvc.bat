REM Volograms build for 1 demo for Visual Studio. Run periodically to check for errors.

REM Change this to the path that suits your version of Visual Studio's vcvars.bat (e.g. one of the line below)
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
REM call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 13.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64

REM "we recommend you compile by using either the /W3 or /W4 warning level"
REM C4221 is nonstandard extension used in struct literals.
set COMPILER_FLAGS=/W4 /D_CRT_SECURE_NO_WARNINGS /wd4221
set LINKER_FLAGS=/out:vol2obj.exe
set LIBS= ^
..\thirdparty\ffmpeg\lib\vs\x64\avcodec.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avdevice.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avformat.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avutil.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\swscale.lib

set BUILD_DIR=".\build"
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
pushd %BUILD_DIR%

set I=/I ..\lib\ ^
/I ..\examples\common\ ^
/I ..\thirdparty\ ^
/I ..\thirdparty\ffmpeg\include\

set SRC= ^
..\tools\vol2obj\main.c ^
..\lib\vol_av.c ^
..\lib\vol_basis.cpp ^
..\lib\vol_geom.c ^
..\thirdparty\basis_universal\transcoder\basisu_transcoder.cpp

cl %COMPILER_FLAGS% %SRC% %I% /link %LINKER_FLAGS% %LIBS%
copy vol2obj.exe ..\
copy ..\thirdparty\ffmpeg\bin\vs\x64\*.dll ..\

popd

pause
