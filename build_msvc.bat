REM Volograms build for vol2obj and vol2vol tools for Visual Studio. Run periodically to check for errors.

REM Change this to the path that suits your version of Visual Studio's vcvars.bat (e.g. one of the line below)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
REM call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 13.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
REM call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64

REM "we recommend you compile by using either the /W3 or /W4 warning level"
REM C4221 is nonstandard extension used in struct literals.
set COMPILER_FLAGS=/W4 /D_CRT_SECURE_NO_WARNINGS /wd4221 /fno-strict-aliasing /DBASISD_SUPPORT_KTX2=0
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

REM Build vol2obj
echo Building vol2obj...
set LINKER_FLAGS_VOL2OBJ=/out:vol2obj.exe
set SRC_VOL2OBJ= ^
..\tools\vol2obj\main.c ^
..\lib\vol_av.c ^
..\lib\vol_basis.cpp ^
..\lib\vol_geom.c ^
..\thirdparty\basis_universal\transcoder\basisu_transcoder.cpp

cl %COMPILER_FLAGS% %SRC_VOL2OBJ% %I% /link %LINKER_FLAGS_VOL2OBJ% %LIBS%
if %errorlevel% neq 0 (
    echo Error building vol2obj
    pause
    exit /b %errorlevel%
)

REM Build vol2vol
echo Building vol2vol...
set LINKER_FLAGS_VOL2VOL=/out:vol2vol.exe
set SRC_VOL2VOL= ^
..\tools\vol2vol\main.c ^
..\lib\vol_av.c ^
..\lib\vol_basis.cpp ^
..\lib\vol_geom.c ^
..\thirdparty\basis_universal\transcoder\basisu_transcoder.cpp

cl %COMPILER_FLAGS% %SRC_VOL2VOL% %I% /link %LINKER_FLAGS_VOL2VOL% %LIBS%
if %errorlevel% neq 0 (
    echo Error building vol2vol
    pause
    exit /b %errorlevel%
)

REM Copy executables and DLLs
copy vol2obj.exe ..\
copy vol2vol.exe ..\
copy ..\thirdparty\ffmpeg\bin\vs\x64\*.dll ..\

popd

echo Build completed successfully!
pause
