#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#include <sys/types.h>
#define TIOCGWINSZ 0x5413
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
int ioctl(int fd, unsigned long req, void *arg);
#endif
