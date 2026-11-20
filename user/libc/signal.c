#include <signal.h>
static sighandler_t handlers[32];
sighandler_t signal(int sig, sighandler_t handler) {
    if (sig < 0 || sig >= 32) return SIG_ERR;
    sighandler_t old = handlers[sig];
    handlers[sig] = handler;
    return old ? old : SIG_DFL;
}
int raise(int sig) { (void)sig; return 0; }
