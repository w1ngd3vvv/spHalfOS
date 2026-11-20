#include <syscall.h>
int main(void) {
    for (;;) syscall0(SYS_YIELD);
    return 0;
}
