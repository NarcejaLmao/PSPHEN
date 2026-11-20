-include $(ARKROOT)/.config
include $(ARKROOT)/common/make/quiet.mak
include $(ARKROOT)/common/make/beauty_gendep.mak

# PSP HEN: newer GCC turns legacy C issues into hard errors; keep them as warnings.
# Only pass each flag if the compiler knows it (the old ARK-4 psp-gcc does not).
PSPHEN_WNOERR := $(foreach f,implicit-function-declaration int-conversion incompatible-pointer-types implicit-int,$(shell echo 'int x;' | psp-gcc -Werror -Wno-error=$(f) -x c -c -o /dev/null - 2>/dev/null && echo -Wno-error=$(f)))
CFLAGS += $(PSPHEN_WNOERR)
