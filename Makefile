# spHalfOS build system
ARCH      := -m32
CC        := gcc
AS        := gcc
LD        := ld
AR        := ar
CFLAGS    := $(ARCH) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
             -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
             -fno-asynchronous-unwind-tables -fno-unwind-tables \
             -fno-omit-frame-pointer -mno-sse -mno-sse2 -mno-mmx \
             -Iinclude -Iuser/include
LDFLAGS   := -m elf_i386 -nostdlib -T boot/linker.ld --build-id=none

KERNEL_C  := $(wildcard kernel/*.c)
KERNEL_S  := boot/boot.S kernel/gdt_flush.S kernel/isr.S
KERNEL_O  := $(patsubst %.c,build/%.o,$(KERNEL_C)) \
             $(patsubst %.S,build/%.o,$(KERNEL_S))

QEMU      := qemu-system-x86_64
DISK      := build/disk.img
QEMUFLAGS := -m 256M -boot d -drive file=$(DISK),format=raw,if=ide,index=0 -no-reboot -nic user,model=rtl8139

# ---------------- userland ----------------
UCFLAGS   := $(ARCH) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
             -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
             -fno-asynchronous-unwind-tables -fno-unwind-tables \
             -mno-sse -mno-sse2 -mno-mmx -Iuser/include
ULDFLAGS  := -m elf_i386 -nostdlib -static -T user/linker.ld --build-id=none
LIBGCC    := $(shell $(CC) -m32 -print-libgcc-file-name)

USER_C    := $(wildcard user/libc/*.c)
USER_LIB_O := $(patsubst user/%.c,build/user/%.o,$(USER_C)) \
              build/user/libc/setjmp.o
LIBC_A    := build/user/lib/libc.a
CRT1      := build/user/lib/crt1.o
CRTI      := build/user/lib/crti.o
CRTN      := build/user/lib/crtn.o

PROGS     := sh edit fire cmatrix hello badapple doom files idle glgears ie
PROG_BINS := $(addprefix build/user/bin/,$(PROGS))
UTILS     := ls cat cp mv rm mkdir rmdir touch wc head tail date ps free \
             uname sleep reboot echo true false

MKBAD     := build/mkbadvideo
BADAPPLE  := assets/badapple.bav

# ---------------- doom (doomgeneric port) ----------------
DOOM_DIR    := doomgeneric/doomgeneric
DOOM_WAD    := doomgeneric/DOOM1.WAD
DOOM_CFLAGS := $(ARCH) -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter \
               -Wno-sign-compare -Wno-missing-field-initializers \
               -Wno-unused-function -Wno-unused-variable \
               -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
               -fno-asynchronous-unwind-tables -fno-unwind-tables \
               -mno-sse -mno-sse2 -mno-mmx -Iuser/include -I$(DOOM_DIR) \
               -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DNORMALUNIX
DOOM_NAMES  := dummy am_map doomdef doomstat dstrings d_event d_items d_iwad \
               d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib \
               hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound \
               i_system i_timer memio m_argv m_bbox m_cheat m_config \
               m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors \
               p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj \
               p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch \
               p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane \
               r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff \
               s_sound tables v_video wi_stuff w_checksum w_file w_main \
               w_wad z_zone w_file_stdc i_input i_video doomgeneric
DOOM_OBJS   := $(patsubst %,build/user/obj/doom/%.o,$(DOOM_NAMES)) \
               build/user/obj/doom/doomgeneric_sphalfos.o

TCC_SRC   ?= /tmp/opencode/tinycc-release_0_9_27
TCC_DEFS  := -DTCC_TARGET_I386 -DCONFIG_TCC_STATIC -DCONFIG_TCCBOOT \
             -DCONFIG_TCCDIR='"/usr/lib/tcc"' \
             -DCONFIG_TCC_CRTPREFIX='"/usr/lib"' \
             -DCONFIG_TCC_LIBPATHS='"/usr/lib"' \
             -DCONFIG_TCC_SYSINCLUDEPATHS='"/usr/lib/tcc/include:/usr/include"'
TCC_BIN   := build/user/bin/tcc
Libtcc1   := build/user/lib/libtcc1.a
HAVE_TCC  := $(wildcard $(TCC_SRC)/tcc.c)

.PHONY: all iso run run-nographic disk stage clean test tcc badapple-data doom

all: build/spHalfOS.elf

$(DISK): build/mkfs
	./build/mkfs $(DISK)

build/mkfs: tools/mkfs.c
	@mkdir -p build
	$(CC) -O2 -Wall -o $@ $<

disk: stage build/mkfs
	./build/mkfs $(DISK) build/root

# ---------------- kernel ----------------
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ARCH) -ffreestanding -fno-pie -c $< -o $@

build/spHalfOS.elf: $(KERNEL_O) boot/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_O)
	@echo "kernel built: $$(stat -c%s $@) bytes"

iso: build/spHalfOS.iso

build/spHalfOS.iso: build/spHalfOS.elf iso/boot/grub/grub.cfg
	@mkdir -p iso/boot/grub
	cp build/spHalfOS.elf iso/boot/spHalfOS.elf
	grub-mkrescue -o $@ iso >/dev/null 2>&1
	@echo "iso built: $@"

# ---------------- user library ----------------
build/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

build/user/%.o: user/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ARCH) -ffreestanding -fno-pie -c $< -o $@

$(LIBC_A): $(USER_LIB_O)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

$(CRT1): build/user/libc/crt0.o
	@mkdir -p $(dir $@)
	cp $< $@
$(CRTI): build/user/libc/crti.o
	@mkdir -p $(dir $@)
	cp $< $@
$(CRTN): build/user/libc/crtn.o
	@mkdir -p $(dir $@)
	cp $< $@

# ---------------- TinyGL (portable software OpenGL subset) ----------------
TINYGL_SRCS := api arrays clear clip error get image_util init light list \
               matrix memory misc msghandling select specbuf texture vertex \
               zbuffer zdither zline zmath ztriangle
TINYGL_OBJS := $(patsubst %,build/user/tinygl/src/%.o,$(TINYGL_SRCS)) \
               build/user/tinygl/sphalfos.o
TINYGL_A    := build/user/lib/libtinygl.a
TGL_CFLAGS  := $(UCFLAGS) -Iuser/tinygl/include -Iuser/tinygl/src \
               -DBYTE_ORDER=1234 -DBIG_ENDIAN=4321 -Wno-unused \
               -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers

$(TINYGL_A): $(TINYGL_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

build/user/tinygl/src/%.o: user/tinygl/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TGL_CFLAGS) -c $< -o $@

build/user/tinygl/sphalfos.o: user/tinygl/sphalfos.c
	@mkdir -p $(dir $@)
	$(CC) $(TGL_CFLAGS) -c $< -o $@

build/user/obj/glgears.o: user/glgears/glgears.c
	@mkdir -p $(dir $@)
	$(CC) $(TGL_CFLAGS) -c $< -o $@

build/user/bin/glgears: build/user/obj/glgears.o $(CRT1) $(TINYGL_A) $(LIBC_A)
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@ $(CRT1) $< $(TINYGL_A) $(LIBC_A) $(LIBGCC)

build/user/obj/ie.o: user/ie/ie.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

# ---------------- programs ----------------
build/user/bin/%: build/user/obj/%.o $(CRT1) $(LIBC_A)
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@ $(CRT1) $< $(LIBC_A) $(LIBGCC)

build/user/obj/sh.o: user/shell/shell.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/edit.o: user/editor/editor.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/fire.o: user/fire/fire.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/cmatrix.o: user/cmatrix/cmatrix.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/hello.o: user/hello/hello.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/badapple.o: user/badapple/badapple.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/files.o: user/files/files.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/obj/idle.o: user/idle/idle.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
build/user/bin/utils: build/user/obj/utils.o $(CRT1) $(LIBC_A)
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@ $(CRT1) $< $(LIBC_A) $(LIBGCC)
build/user/obj/utils.o: user/coreutils/utils.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

# ---------------- doom (doomgeneric port) ----------------
build/user/obj/doom/doomgeneric_sphalfos.o: user/doom/doomgeneric_sphalfos.c
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

build/user/obj/doom/%.o: $(DOOM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

build/user/bin/doom: $(DOOM_OBJS) $(CRT1) $(LIBC_A)
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@ $(CRT1) $(DOOM_OBJS) $(LIBC_A) $(LIBGCC)

doom: build/user/bin/doom

# ---------------- TinyCC port ----------------
ifeq ($(HAVE_TCC),)
$(TCC_BIN):
	@echo "tcc source not found at $(TCC_SRC); skipping tcc" >&2
	@exit 1
else
build/user/tcc/tcc.o: $(TCC_SRC)/tcc.c
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -O2 -std=gnu11 -fno-pie -fno-stack-protector -w \
	    -mno-sse -mno-sse2 -mno-mmx \
	    $(TCC_DEFS) -Iuser/include -I$(TCC_SRC) -c $< -o $@

build/user/lib/libtcc1.o: $(TCC_SRC)/lib/libtcc1.c
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
	    -mno-sse -mno-sse2 -mno-mmx \
	    -Iuser/include -c $< -o $@

build/user/lib/alloca86.o: $(TCC_SRC)/lib/alloca86.S
	@mkdir -p $(dir $@)
	$(AS) $(ARCH) -c $< -o $@

build/user/lib/gcc_compat.o: user/tccrt/gcc_compat.c
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
	    -mno-sse -mno-sse2 -mno-mmx -c $< -o $@

$(Libtcc1): build/user/lib/libtcc1.o build/user/lib/alloca86.o build/user/lib/gcc_compat.o
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

$(TCC_BIN): build/user/tcc/tcc.o $(CRT1) $(LIBC_A) $(Libtcc1)
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@ $(CRT1) $< $(LIBC_A) $(Libtcc1) $(LIBGCC)
endif

tcc: $(TCC_BIN)

# ---------------- staging + disk ----------------
stage: $(PROG_BINS) build/user/bin/utils $(TCC_BIN) $(CRT1) $(CRTI) $(CRTN) $(LIBC_A) $(Libtcc1) $(BADAPPLE)
	rm -rf build/root
	mkdir -p build/root/bin build/root/usr/include build/root/usr/lib/tcc/include \
	         build/root/usr/share build/root/usr/share/doom build/root/home build/root/etc build/root/tmp
	cp $(PROG_BINS) build/user/bin/utils build/root/bin/
	for u in $(UTILS); do cp build/user/bin/utils build/root/bin/$$u; done
	cp -f fasm/fasm.sphalfos build/root/bin/fasm
	cp -f $(TCC_BIN) build/root/bin/tcc 2>/dev/null || true
	cp -f $(CRT1) $(CRTI) $(CRTN) $(LIBC_A) $(Libtcc1) build/root/usr/lib/
	cp -f $(Libtcc1) build/root/usr/lib/tcc/ 2>/dev/null || true
	cp -f $(BADAPPLE) build/root/usr/share/badapple.bav
	if [ -f "$(DOOM_WAD)" ]; then cp -f $(DOOM_WAD) build/root/usr/share/doom/DOOM1.WAD; cp -f $(DOOM_WAD) build/root/home/DOOM1.WAD; fi
	if [ -f wallhaven-4dkreo.jpg ]; then ffmpeg -y -i wallhaven-4dkreo.jpg -vf "scale=1920:1080:force_original_aspect_ratio=increase,crop=1920:1080,format=bgra" -f rawvideo build/root/usr/share/wallpaper.raw >/dev/null 2>&1 || true; fi
	cp -f user/badapple/badapple.c build/root/home/badapple.c
	cp -rf user/include/. build/root/usr/include/
	if [ -d "$(TCC_SRC)/include" ]; then cp -f $(TCC_SRC)/include/*.h build/root/usr/lib/tcc/include/; fi
	printf 'Welcome to spHalfOS!\n\nTry:\n  ls /bin\n  edit /home/welcome.txt\n  fasm hello.asm hello\n  fire\n  cmatrix\n  badapple\n  doom\n  tcc hello.c -o hello && ./hello\n' > build/root/home/welcome.txt
	printf 'spHalfOS 0.1.0\n' > build/root/etc/motd
	printf '#include <stdio.h>\n\nint main(void) {\n    printf("Hello from TinyCC on spHalfOS!\\n");\n    return 0;\n}\n' > build/root/home/hello.c
	printf '#include <stdio.h>\n\nint main(void) {\n    for (int i = 1; i <= 10; i++)\n        printf("%%d squared is %%d\\n", i, i * i);\n    return 0;\n}\n' > build/root/home/squares.c
	printf 'format ELF executable 3\nentry start\nsegment readable executable\nstart:\n    mov eax,2\n    mov ebx,1\n    mov ecx,msg\n    mov edx,msg_len\n    int 0x80\n    mov eax,1\n    xor ebx,ebx\n    int 0x80\nsegment readable writeable\nmsg db "Hello from fasm!",10\nmsg_len = $$ - msg\n' > build/root/home/hello.asm

$(MKBAD): tools/mkbadvideo.c
	@mkdir -p build
	$(CC) -O2 -Wall -o $@ $<

$(BADAPPLE): badapple.mp4 $(MKBAD) tools/make_badapple.sh
	@mkdir -p assets
	./tools/make_badapple.sh

badapple-data: $(BADAPPLE)

# ---------------- running ----------------
run: iso disk
	$(QEMU) $(QEMUFLAGS) -cdrom build/spHalfOS.iso -serial stdio

run-nographic: iso disk
	$(QEMU) $(QEMUFLAGS) -cdrom build/spHalfOS.iso -nographic

clean:
	rm -rf build iso/boot/spHalfOS.elf
