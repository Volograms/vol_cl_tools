# Makefile - vol_cl_tools
# Copyright: 2021, Volograms (http://volograms.com/)
# Author:    Anton Gerdelan <anton@volograms.com>
# to build with GCC instead (eg on a server):
# make -e SANS="" -e CC=gcc -e "FLAGS=-std=c99 -m64 -Wfatal-errors -Wextra -Wall"

CC          = clang
CPP         = clang++
# -Werror is useful when upgrading the API as it upgrades deprecation warnings to errors.
# D_POSIX_C_SOURCE flag includes POSIX commands like ftello() that are not in strict C. Alternatively; -std=gnu99.
# D_FILE_OFFSET_BITS enforces 64-bit file offsets for large file IO, even on 32-bit platforms.
FLAGS       = -m64 -Wfatal-errors -pedantic -Wextra -Wall
FLAGSC      = -std=c99
FLAGSCPP    = -std=c++11
DEBUG       = -g -D_POSIX_C_SOURCE=200808L -D_FILE_OFFSET_BITS=64
#-DVOL_AV_DEBUG -DVOL_GEOM_DEBUG
#SANS        = -fsanitize=address -fsanitize=undefined
INC_DIR     = -I lib/ -I thirdparty/
SRC_AV      = lib/vol_av.c
SRC_GEOM    = lib/vol_geom.c
STA_LIB_AV  =
STA_LIB_GL  =
DYN_LIB_AV  = -lavcodec -lavdevice -lavformat -lavutil -lswscale
DYN_LIB_OPENCL = -lOpenCL
LIB_DIR     = -L ./
BIN_EXT     = .bin
CLEAN_CMD   = rm -f *.bin *.o lib/*.o thirdparty/basis_universal/*.o tools/vol2obj/*.o tools/vol2vol/*.o

ifeq ($(OS),Windows_NT)
	CC         = GCC
	CPP        = G++
	SANS       =
	FLAGS      = -m64 -Wfatal-errors -Wextra -Wall
	FLAGS     += -D _CRT_SECURE_NO_WARNINGS
  BIN_EXT    = .exe
	INC_DIR   += -I thirdparty/ffmpeg/include/
	LIB_DIR_AV = ./thirdparty/ffmpeg/lib/vs/x64/
	LIB_DIR   += -L $(LIB_DIR_AV)
	STA_LIB_AV = $(LIB_DIR_AV)avcodec.lib $(LIB_DIR_AV)avdevice.lib $(LIB_DIR_AV)avformat.lib $(LIB_DIR_AV)avutil.lib $(LIB_DIR_AV)swscale.lib 
	DYN_LIB_OPENCL = -lOpenCL
	CLEAN_CMD  = del /Q *.bin *.o lib\*.o thirdparty\basis_universal\*.o tools\vol2obj\*.o tools\vol2vol\*.o
else
	DYN_LIB_AV  += -lm
	UNAME_S      = $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
	endif
	ifeq ($(UNAME_S),Darwin)
		#DYN_LIB_AV += -framework Cocoa -framework IOKit -framework CoreVideo
		DYN_LIB_OPENCL = -framework OpenCL
	endif
endif

all: vol2obj vol2vol

thirdparty/basis_universal/basisu_transcoder.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_transcoder.o -c thirdparty/basis_universal/transcoder/basisu_transcoder.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_enc.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_enc.o -c thirdparty/basis_universal/encoder/basisu_enc.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_comp.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_comp.o -c thirdparty/basis_universal/encoder/basisu_comp.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_resampler.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_resampler.o -c thirdparty/basis_universal/encoder/basisu_resampler.cpp $(INC_DIR)

thirdparty/basis_universal/zstd.o:
	$(CC) $(FLAGSC) -m64 -Wfatal-errors $(DEBUG) $(SANS) -DZSTD_DISABLE_ASM -o thirdparty/basis_universal/zstd.o -c thirdparty/basis_universal/zstd/zstd.c $(INC_DIR)

thirdparty/basis_universal/basisu_resample_filters.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_resample_filters.o -c thirdparty/basis_universal/encoder/basisu_resample_filters.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_backend.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_backend.o -c thirdparty/basis_universal/encoder/basisu_backend.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_frontend.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_frontend.o -c thirdparty/basis_universal/encoder/basisu_frontend.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_basis_file.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_basis_file.o -c thirdparty/basis_universal/encoder/basisu_basis_file.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_etc.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_etc.o -c thirdparty/basis_universal/encoder/basisu_etc.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_bc7enc.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_bc7enc.o -c thirdparty/basis_universal/encoder/basisu_bc7enc.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_uastc_enc.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_uastc_enc.o -c thirdparty/basis_universal/encoder/basisu_uastc_enc.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_kernels_sse.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_kernels_sse.o -c thirdparty/basis_universal/encoder/basisu_kernels_sse.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_opencl.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_opencl.o -c thirdparty/basis_universal/encoder/basisu_opencl.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_gpu_texture.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_gpu_texture.o -c thirdparty/basis_universal/encoder/basisu_gpu_texture.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_ssim.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_ssim.o -c thirdparty/basis_universal/encoder/basisu_ssim.cpp $(INC_DIR)

thirdparty/basis_universal/basisu_pvrtc1_4.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/basisu_pvrtc1_4.o -c thirdparty/basis_universal/encoder/basisu_pvrtc1_4.cpp $(INC_DIR)

thirdparty/basis_universal/jpgd.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/jpgd.o -c thirdparty/basis_universal/encoder/jpgd.cpp $(INC_DIR)

thirdparty/basis_universal/pvpngreader.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=1 -DBASISD_SUPPORT_KTX2_ZSTD=1 -DBASISU_SUPPORT_OPENCL=1 -o thirdparty/basis_universal/pvpngreader.o -c thirdparty/basis_universal/encoder/pvpngreader.cpp $(INC_DIR)

lib/vol_basis.o:
	$(CPP) $(FLAGSCPP) $(FLAGS) $(DEBUG) $(SANS) -o lib/vol_basis.o -c lib/vol_basis.cpp $(INC_DIR)

lib/vol_geom.o:
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o lib/vol_geom.o -c $(SRC_GEOM) $(INC_DIR)

lib/vol_av.o:
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o lib/vol_av.o -c $(SRC_AV) $(INC_DIR)

vol2obj: thirdparty/basis_universal/basisu_transcoder.o thirdparty/basis_universal/zstd.o lib/vol_basis.o lib/vol_geom.o lib/vol_av.o
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o tools/vol2obj/vol2obj.o -c tools/vol2obj/main.c $(INC_DIR)
	$(CPP) $(FLAGSCPP) $(FLAGS) $(DEBUG) $(SANS) -o vol2obj$(BIN_EXT) tools/vol2obj/vol2obj.o thirdparty/basis_universal/basisu_transcoder.o thirdparty/basis_universal/zstd.o lib/vol_av.o lib/vol_basis.o lib/vol_geom.o $(INC_DIR) $(STA_LIB_AV) $(LIB_DIR) $(DYN_LIB_AV)

vol2vol: thirdparty/basis_universal/basisu_transcoder.o thirdparty/basis_universal/basisu_enc.o thirdparty/basis_universal/basisu_comp.o thirdparty/basis_universal/basisu_resampler.o thirdparty/basis_universal/basisu_resample_filters.o thirdparty/basis_universal/basisu_backend.o thirdparty/basis_universal/basisu_frontend.o thirdparty/basis_universal/basisu_basis_file.o thirdparty/basis_universal/basisu_etc.o thirdparty/basis_universal/basisu_bc7enc.o thirdparty/basis_universal/basisu_uastc_enc.o thirdparty/basis_universal/basisu_kernels_sse.o thirdparty/basis_universal/basisu_opencl.o thirdparty/basis_universal/basisu_gpu_texture.o thirdparty/basis_universal/basisu_ssim.o thirdparty/basis_universal/basisu_pvrtc1_4.o thirdparty/basis_universal/jpgd.o thirdparty/basis_universal/pvpngreader.o thirdparty/basis_universal/zstd.o lib/vol_basis.o lib/vol_geom.o lib/vol_av.o
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o tools/vol2vol/vol2vol.o -c tools/vol2vol/main.c $(INC_DIR)
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o tools/vol2vol/video_processing.o -c tools/vol2vol/video_processing.c $(INC_DIR)
	$(CPP) $(FLAGSCPP) $(FLAGS) $(DEBUG) $(SANS) -o tools/vol2vol/basis_encoder_wrapper.o -c tools/vol2vol/basis_encoder_wrapper.cpp $(INC_DIR)
	$(CPP) $(FLAGSCPP) $(FLAGS) $(DEBUG) $(SANS) -o vol2vol$(BIN_EXT) tools/vol2vol/vol2vol.o tools/vol2vol/video_processing.o tools/vol2vol/basis_encoder_wrapper.o thirdparty/basis_universal/basisu_transcoder.o thirdparty/basis_universal/basisu_enc.o thirdparty/basis_universal/basisu_comp.o thirdparty/basis_universal/basisu_resampler.o thirdparty/basis_universal/basisu_resample_filters.o thirdparty/basis_universal/basisu_backend.o thirdparty/basis_universal/basisu_frontend.o thirdparty/basis_universal/basisu_basis_file.o thirdparty/basis_universal/basisu_etc.o thirdparty/basis_universal/basisu_bc7enc.o thirdparty/basis_universal/basisu_uastc_enc.o thirdparty/basis_universal/basisu_kernels_sse.o thirdparty/basis_universal/basisu_opencl.o thirdparty/basis_universal/basisu_gpu_texture.o thirdparty/basis_universal/basisu_ssim.o thirdparty/basis_universal/basisu_pvrtc1_4.o thirdparty/basis_universal/jpgd.o thirdparty/basis_universal/pvpngreader.o thirdparty/basis_universal/zstd.o lib/vol_av.o lib/vol_basis.o lib/vol_geom.o $(INC_DIR) $(STA_LIB_AV) $(LIB_DIR) $(DYN_LIB_AV) $(DYN_LIB_OPENCL)

.PHONY : clean
clean:
	$(CLEAN_CMD)
