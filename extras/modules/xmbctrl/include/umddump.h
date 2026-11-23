#ifndef __UMDDUMP_H__
#define __UMDDUMP_H__

// Status shown as the third option of the "UMD Dumper" debug setting.
extern char umddump_status[48];

// 0 = idle (nothing to report), 2 = a status line is available (running/done/failed)
int umddump_get_value(void);

// Starts dumping the inserted UMD to ms0:/ISO/<DISC_ID>.iso in a background thread.
void umddump_start(void);

#endif
