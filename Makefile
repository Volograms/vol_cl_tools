# Makefile - vol_cl_tools
# Copyright: 2021, Volograms (http://volograms.com/)
# Author:    Anton Gerdelan <anton@volograms.com>
# to build with GCC instead (eg on a server):
# make -e SANS="" -e CC=gcc -e "FLAGS=-std=c99 -m64 -Wfatal-errors -Wextra -Wall"

CC          = clang
CPP			= clang++
# -Werror is useful when upgrading the API as it upgrades deprecation warnings to errors.
# D_POSIX_C_SOURCE flag includes POSIX commands like ftello() that are not in strict C. Alternatively; -std=gnu99.
# D_FILE_OFFSET_BITS enforces 64-bit file offsets for large file IO, even on 32-bit platforms.
FLAGS       = -m64 -Wfatal-errors -pedantic -Wextra -Wall
FLAGSC		= -std=c99
FLAGSCPP	= -std=c++11
DEBUG       = -g -D_POSIX_C_SOURCE=200808L -D_FILE_OFFSET_BITS=64
#-DVOL_AV_DEBUG -DVOL_GEOM_DEBUG 
#SANS        = -fsanitize=address -fsanitize=undefined 
INC_DIR     = -I lib/ -I thirdparty/
SRC_AV      = lib/vol_av.c
SRC_GEOM    = lib/vol_geom.c
STA_LIB_AV  = 
STA_LIB_GL  = 
DYN_LIB_AV  = -lavcodec -lavdevice -lavformat -lavutil -lswscale
LIB_DIR     = -L ./
BIN_EXT     = .bin
CLEAN_CMD   = rm -f *.bin *.o lib/*.o thirdparty/basis_universal/*.o

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
	CLEAN_CMD  = 
else
	DYN_LIB_AV  += -lm
	UNAME_S      = $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
	endif
	ifeq ($(UNAME_S),Darwin)
		#DYN_LIB_AV += -framework Cocoa -framework IOKit -framework CoreVideo
	endif
endif

all: vol2obj

thirdparty/basis_universal/basisu_transcoder.o:
	$(CPP) $(FLAGSCPP) -m64 -Wfatal-errors $(DEBUG) $(SANS) -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 -o thirdparty/basis_universal/basisu_transcoder.o -c thirdparty/basis_universal/transcoder/basisu_transcoder.cpp $(INC_DIR)
	
lib/vol_basis.o:
	$(CPP) $(FLAGSCPP) $(FLAGS) $(DEBUG) $(SANS) -o lib/vol_basis.o -c lib/vol_basis.cpp $(INC_DIR)

lib/vol_geom.o:
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o lib/vol_geom.o -c $(SRC_GEOM) $(INC_DIR)

lib/vol_av.o:
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o lib/vol_av.o -c $(SRC_AV) $(INC_DIR)

vol2obj: thirdparty/basis_universal/basisu_transcoder.o lib/vol_basis.o lib/vol_geom.o lib/vol_av.o
	$(CC) $(FLAGSC) $(FLAGS) $(DEBUG) $(SANS) -o tools/vol2obj/vol2obj.o -c tools/vol2obj/main.c $(INC_DIR)
	$(CPP) $(FLAGSCPP) $(FLAGS) $(DEBUG) $(SANS) -o vol2obj$(BIN_EXT) tools/vol2obj/vol2obj.o thirdparty/basis_universal/basisu_transcoder.o lib/vol_av.o lib/vol_basis.o lib/vol_geom.o $(INC_DIR) $(STA_LIB_AV) $(LIB_DIR) $(DYN_LIB_AV)

.PHONY : clean
clean:
	$(CLEAN_CMD)
