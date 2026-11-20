
TARGET = XMENU
BUILD_PRX = 1

CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

INCDIR = $(ARKROOT)/libs/libpng $(ARKROOT)/common/include include
LIBDIR = $(ARKROOT)/libs/SystemCtrlForUser $(ARKROOT)/libs
LIBS = -lstdc++ -lpspgu -lpspgum -lpspsystemctrl_user -lpng_stub
LDFLAGS = -L libs

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = xMenu
PSP_EBOOT_ICON = resources/ICON0.PNG
PSP_EBOOT_PIC1 = resources/PIC1.PNG

OBJS = \
    main.o \
    src/font.o \
    src/framebuffer.o \
    src/graphics.o \
    src/debug.o \
    src/common.o \
    src/submenu.o \
    src/menu.o \
    src/entry.o \
    src/controller.o \
    src/text.o

all: $(TARGET).prx EBOOT.PBP

distclean: clean
xmenu: all

include $(ARKROOT)/common/make/global.mak
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# Override only xMenu's ELF linking rule.
$(TARGET).elf: $(OBJS) $(EXPORT_OBJ)
	$(LINK.c) $(LDFLAGS) $^ $(LIBS) -o $@
	$(FIXUP) $@

$(TARGET).prx: $(TARGET).elf
	psp-prxgen $< $@

include $(ARKROOT)/common/make/beauty.mak
