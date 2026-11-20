#ifndef _TERMIOS_H
#define _TERMIOS_H
#include <sys/types.h>
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};
/* c_iflag */
#define BRKINT 0000002
#define ICRNL  0000400
#define INPCK  0000020
#define ISTRIP 0000040
#define IXON   0002000
/* c_oflag */
#define OPOST  0000001
/* c_cflag */
#define CS8    0000060
/* c_lflag */
#define ECHO   0000010
#define ICANON 0000002
#define IEXTEN 0100000
#define ISIG   0000001
/* c_cc */
#define VMIN   6
#define VTIME  5
/* actions */
#define TCSAFLUSH 2
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int act, const struct termios *t);
#endif
