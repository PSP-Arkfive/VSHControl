TARGET = vshctrl
OBJS = main.o \
	src/vshpatch.o \
	src/xmbiso.o \
	src/isoreader.o \
	src/virtual_pbp.o \
	src/virtual_mp4.o \
	src/dirent_track.o \
	src/disctype.o \
	src/vshmenu.o \
	src/usbdevice.o \
	src/custom_update.o \
	src/hibernation_delete.o \
	src/registry.o \

IMPORTS = imports.o

INCDIR = include external/include
CFLAGS = -std=c99 -Os -G0 -Wall -fno-pic

PSP_FW_VERSION = 660

OBJS += $(IMPORTS)
all: $(TARGET).prx
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC=1
USE_KERNEL_LIBS=1

LIBDIR = libs external/libs
LDFLAGS =  -nostartfiles
LIBS = -lpspsystemctrl_kernel -lpspusb -lpspusbdevice_driver -lpspreg

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
