/* spHalfOS process management, ELF loader and scheduler */
#include "process.h"
#include "mm.h"
#include "spfs.h"
#include "kernel.h"
#include "string.h"
#include "io.h"
#include "gdt.h"
#include "console.h"

#define ELF_MAGIC 0x464C457Fu

typedef struct {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf32_hdr_t;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} elf32_phdr_t;

proc_t procs[PROC_MAX];
proc_t *current;
static int next_pid = 1;
static int quantum = 3;

static uint8_t kstacks[PROC_MAX][PROC_KSTACK_SIZE] __attribute__((aligned(16)));
static uint8_t elf_hdr_buf[8192];
static uint8_t seg_buf[FS_BLOCK_SIZE];

extern void resume_context(uint32_t ksp);

static uint8_t fpu_template[108] __attribute__((aligned(16)));
static int fpu_template_ready;

static void fpu_prepare(proc_t *p) {
    if (!fpu_template_ready) {
        __asm__ volatile ("fninit; fnsave %0; frstor %0" : "=m"(fpu_template));
        fpu_template_ready = 1;
    }
    memcpy(p->fpu, fpu_template, sizeof(p->fpu));
}

static inline void fpu_save(proc_t *p) {
    __asm__ volatile ("fnsave %0" : "=m"(p->fpu));
}

static inline void fpu_load(proc_t *p) {
    __asm__ volatile ("frstor %0" :: "m"(p->fpu));
}

static int pick_next_index(void) {
    int cur = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        if (&procs[i] == current) { cur = i; break; }
    }
    for (int i = 1; i <= PROC_MAX; i++) {
        int idx = (cur + i) % PROC_MAX;
        if (procs[idx].used && procs[idx].state == PROC_READY) return idx;
    }
    if (current && current->state == PROC_RUNNING) {
        for (int i = 0; i < PROC_MAX; i++)
            if (&procs[i] == current) return i;
    }
    return -1;
}

static void switch_to(regs_t *r, proc_t *next) {
    if (!current || next == current) return;
    current->ksp = (uint32_t)r;
    if (current->state == PROC_RUNNING) current->state = PROC_READY;
    fpu_save(current);
    next->state = PROC_RUNNING;
    tss_set_esp0(next->kstack_top);
    vmm_switch(next->pd);
    current = next;
    fpu_load(next);
    resume_context(next->ksp);
}

void sched_timer_tick(regs_t *r) {
    if (!current) return;
    if (current->state == PROC_ZOMBIE) {
        quantum = 0;
    } else {
        if (--quantum > 0) return;
    }
    quantum = 3;
    int idx = pick_next_index();
    if (idx < 0 && current->state == PROC_ZOMBIE) {
        void *t = console_default_term();
        if (!t) t = console_create_term("Terminal");
        if (t) {
            char *argv[2]; argv[0] = "/bin/sh"; argv[1] = 0;
            proc_t *np = process_create("/bin/sh", argv, 1, 0);
            if (np) { np->term = t; console_set_term_owner(t, np->pid); console_focus_term(t); idx = pick_next_index(); }
        }
        if (idx < 0) { kprintf("no runnable processes left, halting.\n"); for (;;) { cpu_cli(); cpu_hlt(); } }
    }
    if (idx >= 0 && &procs[idx] != current)
        switch_to(r, &procs[idx]);
}

void sched_yield(regs_t *r) {
    int idx = pick_next_index();
    if (idx >= 0 && &procs[idx] != current)
        switch_to(r, &procs[idx]);
}

static void reap_orphan_zombies(void);

void process_exit(regs_t *r, int code) {
    if (!current) panic("exit without current process");
    console_notify_process_exit(current->pid);
    current->exitcode = code;
    current->state = PROC_ZOMBIE;
    for (int i = 0; i < PROC_MAX; i++)
        if (procs[i].used && procs[i].ppid == current->pid)
            procs[i].ppid = 0;
    reap_orphan_zombies();

    int idx = pick_next_index();
    if (idx < 0) {
        void *t = console_default_term();
        if (!t) t = console_create_term("Terminal");
        if (t) {
            char *argv[2]; argv[0] = "/bin/sh"; argv[1] = 0;
            proc_t *np = process_create("/bin/sh", argv, 1, 0);
            if (np) {
                np->term = t;
                console_set_term_owner(t, np->pid);
                console_focus_term(t);
                idx = pick_next_index();
            }
        }
    }
    if (idx < 0) {
        kprintf("no runnable processes left, halting.\n");
        for (;;) { cpu_cli(); cpu_hlt(); }
    }
    proc_t *next = &procs[idx];
    next->state = PROC_RUNNING;
    tss_set_esp0(next->kstack_top);
    vmm_switch(next->pd);
    current = next;
    fpu_load(next);
    resume_context(next->ksp);
    __builtin_unreachable();
}

static int vm_write(uint32_t pd, uint32_t vaddr, const void *src, uint32_t len) {
    const uint8_t *s = src;
    uint32_t done = 0;
    while (done < len) {
        uint32_t va = vaddr + done;
        uint32_t phys = vmm_user_phys(pd, va);
        if (!phys) return -1;
        uint32_t off = va & 0xFFF;
        uint32_t chunk = PAGE_SIZE - off;
        if (chunk > len - done) chunk = len - done;
        memcpy((uint8_t *)phys_to_virt(phys & ~0xFFFu) + off, s + done, chunk);
        done += chunk;
    }
    return 0;
}

static uint32_t build_user_stack(uint32_t pd, uint32_t entry, char *const argv[], int argc) {
    uint32_t sp = USER_STACK_TOP - 16;
    uint32_t ptrs[PROC_MAX + 8];
    if (argc > PROC_MAX + 4) argc = PROC_MAX + 4;

    for (int i = argc - 1; i >= 0; i--) {
        const char *s = argv[i] ? argv[i] : "";
        uint32_t len = (uint32_t)strlen(s) + 1;
        if (sp < USER_STACK_TOP - USER_STACK_SIZE + 512) return 0;
        sp -= len;
        if (vm_write(pd, sp, s, len) < 0) return 0;
        ptrs[i] = sp;
    }
    ptrs[argc] = 0;
    uint32_t array = (sp - (uint32_t)(argc + 1) * 4) & ~3u;
    if (array < USER_STACK_TOP - USER_STACK_SIZE) return 0;
    for (int i = 0; i <= argc; i++)
        if (vm_write(pd, array + (uint32_t)i * 4, &ptrs[i], 4) < 0) return 0;
    uint32_t esp = array - 4;
    uint32_t ac = (uint32_t)argc;
    if (vm_write(pd, esp, &ac, 4) < 0) return 0;
    (void)entry;
    return esp;
}

static int load_elf(proc_t *p, uint32_t ino) {
    int n = fs_read_file(ino, 0, elf_hdr_buf, sizeof(elf_hdr_buf));
    if (n < (int)sizeof(elf32_hdr_t)) return -1;
    elf32_hdr_t *eh = (elf32_hdr_t *)elf_hdr_buf;
    if (*(uint32_t *)eh->ident != ELF_MAGIC || eh->ident[4] != 1 || eh->ident[5] != 1)
        return -1;
    if (eh->machine != 3 || eh->type != 2) return -1;
    if (eh->phnum == 0 || eh->phnum > 32) return -1;
    if (eh->phoff + (uint32_t)eh->phnum * sizeof(elf32_phdr_t) > (uint32_t)n) return -1;

    elf32_phdr_t *ph = (elf32_phdr_t *)(elf_hdr_buf + eh->phoff);
    uint32_t max_end = 0;

    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type != 1) continue;
        uint32_t vaddr = ph[i].vaddr;
        uint32_t memsz = ph[i].memsz;
        uint32_t filesz = ph[i].filesz;
        uint32_t off = ph[i].offset;
        if (memsz == 0) continue;
        if (vaddr < 0x08000000u || vaddr + memsz >= USER_VADDR_MAX) return -1;

        uint32_t page_start = vaddr & ~(PAGE_SIZE - 1);
        uint32_t page_end = (vaddr + memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (vmm_alloc_user_pages(p->pd, page_start, page_end - page_start, 1) < 0)
            return -1;

        uint32_t copied = 0;
        while (copied < filesz) {
            uint32_t va = vaddr + copied;
            uint32_t page_off = va & (PAGE_SIZE - 1);
            uint32_t chunk = PAGE_SIZE - page_off;
            if (chunk > filesz - copied) chunk = filesz - copied;
            if (chunk > sizeof(seg_buf)) chunk = sizeof(seg_buf);
            int r = fs_read_file(ino, off + copied, seg_buf, chunk);
            if (r != (int)chunk) return -1;
            uint32_t pa = vmm_user_phys(p->pd, va);
            if (!pa) return -1;
            uint8_t *dst = (uint8_t *)phys_to_virt(pa & ~(PAGE_SIZE - 1));
            memcpy(dst + page_off, seg_buf, chunk);
            copied += chunk;
        }
        if (vaddr + memsz > max_end) max_end = vaddr + memsz;
    }
    if (!max_end) return -1;
    p->entry = eh->entry;
    p->brk_start = p->brk = (max_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    p->mmap_next = USER_MMAP_BASE;
    return 0;
}

static void reap_orphan_zombies(void) {
    for (int i = 0; i < PROC_MAX; i++) {
        proc_t *p = &procs[i];
        if (!p->used || p->state != PROC_ZOMBIE) continue;
        proc_t *par = p->ppid > 0 ? process_find(p->ppid) : 0;
        if (!par || par->state == PROC_ZOMBIE) {
            if (p->pd) vmm_destroy_user_pd(p->pd);
            memset(p, 0, sizeof(*p));
            p->pid = -1;
        }
    }
}

proc_t *process_create(const char *path, char *const argv[], int argc, proc_t *parent) {
    int slot = -1;
    for (int i = 0; i < PROC_MAX; i++) {
        if (!procs[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        reap_orphan_zombies();
        for (int i = 0; i < PROC_MAX; i++) {
            if (!procs[i].used) { slot = i; break; }
        }
    }
    if (slot < 0) return NULL;

    uint32_t ino;
    uint32_t cwd = parent ? parent->cwd : fs_root();
    if (fs_resolve(cwd, path, &ino) < 0) return NULL;
    if (fs_is_dir(ino)) return NULL;

    proc_t *p = &procs[slot];
    memset(p, 0, sizeof(*p));
    p->pd = vmm_create_user_pd();
    if (!p->pd) return NULL;

    if (load_elf(p, ino) < 0) {
        vmm_destroy_user_pd(p->pd);
        memset(p, 0, sizeof(*p));
        return NULL;
    }

    if (vmm_alloc_user_pages(p->pd, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE, 1) < 0) {
        vmm_destroy_user_pd(p->pd);
        memset(p, 0, sizeof(*p));
        return NULL;
    }
    p->user_esp = build_user_stack(p->pd, p->entry, argv, argc);
    if (!p->user_esp) {
        vmm_destroy_user_pd(p->pd);
        memset(p, 0, sizeof(*p));
        return NULL;
    }

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(p->name, base, PROC_NAME_MAX - 1);

    p->used = 1;
    p->pid = next_pid++;
    p->ppid = parent ? parent->pid : 0;
    p->state = PROC_READY;
    p->cwd = cwd;
    p->kstack_top = (uint32_t)&kstacks[slot][PROC_KSTACK_SIZE];
    fpu_prepare(p);
    p->term = parent ? parent->term : console_default_term();
    if (parent) {
        for (int i = 0; i < 3; i++) p->fds[i] = parent->fds[i];
    }

    uint32_t *sp = (uint32_t *)p->kstack_top;
    *--sp = USER_DS;
    *--sp = p->user_esp;
    *--sp = 0x202;             /* IF */
    *--sp = USER_CS;
    *--sp = p->entry;
    *--sp = 0;                 /* err */
    *--sp = 0;                 /* vector */
    *--sp = 0;                 /* eax */
    *--sp = 0;                 /* ecx */
    *--sp = 0;                 /* edx */
    *--sp = 0;                 /* ebx */
    *--sp = 0;                 /* esp */
    *--sp = 0;                 /* ebp */
    *--sp = 0;                 /* esi */
    *--sp = 0;                 /* edi */
    *--sp = USER_DS;           /* ds */
    *--sp = USER_DS;           /* es */
    *--sp = USER_DS;           /* fs */
    *--sp = USER_DS;           /* gs */
    p->ksp = (uint32_t)sp;
    return p;
}

proc_t *process_find(int pid) {
    for (int i = 0; i < PROC_MAX; i++)
        if (procs[i].used && procs[i].pid == pid) return &procs[i];
    return NULL;
}

int process_wait_child(int pid, int *status) {
    if (!current) return -1;
    for (int i = 0; i < PROC_MAX; i++) {
        if (!procs[i].used) continue;
        if (procs[i].ppid != current->pid) continue;
        if (pid > 0 && procs[i].pid != pid) continue;
        if (procs[i].state == PROC_ZOMBIE) {
            if (status) *status = procs[i].exitcode;
            int cpid = procs[i].pid;
            process_reap(&procs[i]);
            return cpid;
        }
        return -2;   /* EAGAIN-ish */
    }
    return -1;
}

void process_reap(proc_t *p) {
    if (!p || !p->used) return;
    if (p->pd && p->state == PROC_ZOMBIE) vmm_destroy_user_pd(p->pd);
    memset(p, 0, sizeof(*p));
    p->pid = -1;
}

void process_kill(proc_t *p) {
    if (!p || !p->used) return;
    console_notify_process_exit(p->pid);
    p->state = PROC_ZOMBIE;
    p->exitcode = -9;
}

int process_count_used(void) {
    int n = 0;
    for (int i = 0; i < PROC_MAX; i++) if (procs[i].used) n++;
    return n;
}

void process_get_path(proc_t *p, char *buf, int size) {
    char tmp[256];
    if (fs_get_path(p ? p->cwd : fs_root(), tmp, sizeof(tmp)) < 0) {
        strncpy(buf, "/", size);
        return;
    }
    strncpy(buf, tmp, size - 1);
    buf[size - 1] = 0;
}

void sched_start(proc_t *init) {
    if (!init) panic("no init process");
    current = init;
    init->state = PROC_RUNNING;
    quantum = 3;
    tss_set_esp0(init->kstack_top);
    vmm_switch(init->pd);
    current = init;
    fpu_load(init);
    resume_context(init->ksp);
    __builtin_unreachable();
}
