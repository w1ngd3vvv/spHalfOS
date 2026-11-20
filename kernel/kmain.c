/* spHalfOS kernel entry */
#include "types.h"
#include "kernel.h"
#include "console.h"
#include "io.h"
#include "gdt.h"
#include "idt.h"
#include "mm.h"
#include "multiboot2.h"
#include "spfs.h"
#include "string.h"
#include "process.h"
#include "vbe.h"
#include "mouse.h"
#include "rtl8139.h"

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

void kmain(uint32_t magic, uint32_t mb_info) {
    console_init();
    serial_init();

    kprintf("\n");
    kprintf("  spHalfOS %s -- booting\n", KERNEL_VERSION);
    kprintf("  -------------------------\n");

    if (magic != MB2_BOOTLOADER_MAGIC)
        panic("not loaded by a multiboot2 bootloader (magic=%x)", magic);

    gdt_init();
    kprintf("[ok] GDT/TSS\n");
    idt_init();
    kprintf("[ok] IDT/PIC\n");

    pmm_init(mb_info);
    kprintf("[ok] physical memory: %u MiB, %u free frames\n",
            (pmm_total_frames() * 4) / 1024, pmm_free_frames());

    vmm_init();
    kprintf("[ok] paging enabled\n");

    {
        mb2_info_t *mi = (mb2_info_t *)mb_info;
        kprintf("[mb] info=%x size=%u\n", mb_info, mi->total_size);
        uint8_t *p = (uint8_t *)mi + sizeof(mb2_info_t);
        for (uint8_t *e = (uint8_t *)mi + mi->total_size; p < e;) {
            mb2_tag_t *t = (mb2_tag_t *)p;
            kprintf("[mb] tag type=%u size=%u\n", t->type, t->size);
            if (t->type == MB2_TAG_FRAMEBUFFER) {
                mb2_tag_framebuffer_t *ft = (mb2_tag_framebuffer_t *)t;
                kprintf("[mb]   fb addr=%x pitch=%u %ux%u bpp=%u type=%u\n",
                        (uint32_t)ft->framebuffer_addr, ft->framebuffer_pitch,
                        ft->framebuffer_width, ft->framebuffer_height,
                        ft->framebuffer_bpp, ft->framebuffer_type);
            }
            if (t->type == MB2_TAG_END || t->size < 8) break;
            p += (t->size + 7) & ~7u;
        }
    }
    vbe_init(mb_info);
    console_attach_fb();
    mouse_init();
    rtl8139_init();

    int fsr = fs_mount();
    if (fsr == FS_OK) {
        kprintf("[ok] spFS mounted: %u free / %u blocks\n", fs_free_blocks(), fs_total_blocks());
        console_load_wallpaper();
        uint32_t root = fs_root(), ino;
        if (fs_create(root, "hello.txt", FS_MODE_FILE, &ino) == FS_OK) {
            const char *msg = "Hello from spHalfOS!\n";
            fs_write_file(ino, 0, msg, (uint32_t)strlen(msg));
            char buf[64];
            int n = fs_read_file(ino, 0, buf, sizeof(buf) - 1);
            if (n > 0) { buf[n] = 0; kprintf("[fs] read back: %s", buf); }
        }
        uint32_t cookie = 0;
        char name[60];
        uint8_t type;
        kprintf("[fs] root contains:");
        while (fs_dir_iterate(root, &cookie, name, sizeof(name), NULL, &type) == 1)
            kprintf(" %s%s", name, type == 2 ? "/" : "");
        kprintf("\n");
    } else {
        kprintf("[warn] no filesystem (%d)\n", fsr);
    }

    timer_init(60);
    keyboard_init();
    interrupts_enable();
    kprintf("[ok] interrupts on\n");
    kprintf("[ok] kernel %x..%x\n", (uint32_t)_kernel_start, (uint32_t)_kernel_end);

    if (fsr == FS_OK) {
        char *iargv[] = { "/bin/idle", NULL };
        proc_t *idle = process_create("/bin/idle", iargv, 1, NULL);
        if (idle) { idle->term = NULL; }

        char *argv[] = { "/bin/sh", NULL };
        proc_t *sh = process_create("/bin/sh", argv, 1, NULL);
        if (sh) {
            void *t = console_default_term();
            sh->term = t;
            console_set_term_owner(t, sh->pid);
            kprintf("[ok] starting /bin/sh (pid %d)\n\n", sh->pid);
            sched_start(sh);
        } else {
            kprintf("[warn] /bin/sh not found or not loadable\n");
        }
    }

    kprintf("\nType something (serial or PS/2 keyboard).\n> ");

    for (;;) {
        int c = console_input_get();
        if (c >= 0) {
            kputc((char)c);
            if (c == '\n') kputs("> ");
        } else {
            cpu_hlt();
        }
    }
}
