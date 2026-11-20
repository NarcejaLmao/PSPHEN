#include <pspimport.s>

// User-mode import: 0x4009 = user-library flag + version 9 (same as libs/SystemCtrlForUser)
IMPORT_START "SystemCtrlForUser", 0x40090000
IMPORT_FUNC "SystemCtrlForUser", 0xB00B1E55, sctrlArkGetConfig
