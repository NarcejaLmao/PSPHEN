TARGET = fpscounter
OBJS = main.o imports.o
CFLAGS = -std=c99 -O2 -G0 -Wall -fshort-wchar -fno-pic -mno-check-zero-division \
	-I$(ARKROOT)/extras/menus/arkMenu/include
ASFLAGS = $(CFLAGS)
LDFLAGS = -mno-crt0 -nostartfiles
BUILD_PRX = 1
PRX_EXPORTS = exports.exp
INCDIR = $(ARKROOT)/common/include
LIBDIR = $(ARKROOT)/libs
USE_PSPSDK_LIBS = 1
USE_PSPSDK_LIBC = 1
LIBS = -lpspsystemctrl_user
PSP_FW_VERSION = 660
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

all:
	psp-packer $(TARGET).prx

clean:
	rm -f *.o *.elf *.prx *.bin
