#ifndef SP_PROCESS_H
#define SP_PROCESS_H

#include "types.h"
#include "idt.h"

#define PROC_MAX 32
#define FD_MAX 16
#define PROC_KSTACK_SIZE 65536
#define PROC_NAME_MAX 32

#define PROC_FREE 0
#define PROC_READY 1
#define PROC_RUNNING 2
#define PROC_ZOMBIE 3

#define FD_FLAG_READ  1
#define FD_FLAG_WRITE 2
#define FD_FLAG_APPEND 4

typedef struct {
    int used;
    uint32_t ino;
    uint32_t off;
    int flags;
    int is_dir;
    uint32_t dir_cookie;
} fd_t;

typedef struct {
    int used;
    int pid;
    int ppid;
    int state;
    int exitcode;
    char name[PROC_NAME_MAX];

    uint32_t pd;
    uint32_t kstack_top;
    uint32_t ksp;
    uint32_t entry;
    uint32_t user_esp;
    uint32_t brk_start;
    uint32_t brk;
    uint32_t mmap_next;
    uint32_t cwd;

    uint8_t fpu[108] __attribute__((aligned(16)));

    fd_t fds[FD_MAX];
    void *term;
} proc_t;

extern proc_t *current;
extern proc_t procs[PROC_MAX];

void sched_start(proc_t *init);
void sched_timer_tick(regs_t *r);
void sched_yield(regs_t *r);
void process_exit(regs_t *r, int code) NORETURN;
proc_t *process_create(const char *path, char *const argv[], int argc, proc_t *parent);
proc_t *process_find(int pid);
int process_wait_child(int pid, int *status);
void process_reap(proc_t *p);
void process_kill(proc_t *p);
int process_count_used(void);
void process_get_path(proc_t *p, char *buf, int size);

#endif
