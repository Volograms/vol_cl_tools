# Makefile - vol_cl_tools
# Copyright: 2021, Volograms (http://volograms.com/)
# Author:    Anton Gerdelan <anton@volograms.com>
# to build with GCC instead (eg on a server):
# make -e SANS="" -e CC=gcc -e "FLAGS=-std=c99 -m64 -Wfatal-errors -Wextra -Wall"

CC          = clang
# -Werror is useful when upgrading the API as it upgrades deprecation warnings to errors.
# D_POSIX_C_SOURCE flag includes POSIX commands like ftello() that are not in strict C. Alternatively; -std=gnu99.
# D_FILE_OFFSET_BITS enforces 64-bit file offsets for large file IO, even on 32-bit platforms.
FLAGS       = -std=c99 -m64 -Wfatal-errors -pedantic -Wextra -Wall
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
CLEAN_CMD   = rm -f *.bin

ifeq ($(OS),Windows_NT)
	CC         = GCC
	SANS       =
	FLAGS      = -std=c99 -m64 -Wfatal-errors -Wextra -Wall
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

vol2obj:
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o vol2obj$(BIN_EXT) tools/vol2obj/main.c $(INC_DIR) $(SRC_GEOM) $(SRC_AV) $(STA_LIB_AV) $(LIB_DIR) $(DYN_LIB_AV)

.PHONY : clean
clean:
	$(CLEAN_CMD)
