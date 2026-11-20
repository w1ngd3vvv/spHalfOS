/* spHalfOS PS/2 keyboard driver -- US layout, all printable symbols,
   Shift/Caps/Ctrl/AltGr, ANSI sequences for navigation keys. */
#include "idt.h"
#include "io.h"
#include "console.h"
#include "kernel.h"
#include "gdt.h"

static int shift_down, ctrl_down, alt_down, caps_on, extended;

static const char normal_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char shift_map[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* AltGr (right Alt) extras, mostly CP437 glyphs */
static const char altgr_map[128] = {
    0, 27, 0xFB,0xFD,0xFC,0x9C,0xBD,0x9E,0xF3,0xF2,0xF4,0x9D,0xF1,0xF8,0,0,
    '@','~','#','$','%','^','&','*','(',')','[',']',0,0,
    'a','s','d','f','g','h','j','k','l',':','"','`',0,'|',
    'z','x','c','v','b','n','m','<','>','?',0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char *ext_seq_up    = "\x1b[A";
static const char *ext_seq_down  = "\x1b[B";
static const char *ext_seq_right = "\x1b[C";
static const char *ext_seq_left  = "\x1b[D";
static const char *ext_seq_home  = "\x1b[H";
static const char *ext_seq_end   = "\x1b[F";
static const char *ext_seq_pgup  = "\x1b[5~";
static const char *ext_seq_pgdn  = "\x1b[6~";
static const char *ext_seq_ins   = "\x1b[2~";
static const char *ext_seq_del   = "\x1b[3~";

static const char *fkeys[12] = {
    "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[15~", "\x1b[17~",
    "\x1b[18~", "\x1b[19~", "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~"
};

static void push_str(const char *s) {
    while (*s) console_input_push((uint8_t)*s++);
}

void keyboard_init(void) {
    /* enable the first PS/2 port and its keyboard interrupt */
    while (inb(0x64) & 0x02) io_wait();
    outb(0x64, 0xAE);
    while (inb(0x64) & 0x02) io_wait();
    outb(0x60, 0xF4);              /* enable scanning */
    shift_down = ctrl_down = alt_down = caps_on = extended = 0;
}

static void handle_extended(uint8_t code) {
    switch (code) {
    case 0x48: push_str(ext_seq_up); break;
    case 0x50: push_str(ext_seq_down); break;
    case 0x4D: push_str(ext_seq_right); break;
    case 0x4B: push_str(ext_seq_left); break;
    case 0x47: push_str(ext_seq_home); break;
    case 0x4F: push_str(ext_seq_end); break;
    case 0x49: push_str(ext_seq_pgup); break;
    case 0x51: push_str(ext_seq_pgdn); break;
    case 0x52: push_str(ext_seq_ins); break;
    case 0x53:
        if (ctrl_down && alt_down) reboot_now();
        push_str(ext_seq_del);
        break;
    case 0x38: alt_down = 1; break;
    case 0x1C: console_input_push('\n'); break;
    case 0x35: console_input_push('/'); break;
    default: break;
    }
}

void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    int release = sc & 0x80;
    uint8_t code = sc & 0x7F;

    if (sc == 0xE0) { extended = 1; return; }
    if (sc == 0xE1) { return; } /* pause key, not supported */

    if (extended) {
        extended = 0;
        if (code == 0x38) {
            alt_down = release ? 0 : 1;
            return;
        }
        if (!release) handle_extended(code);
        return;
    }

    switch (code) {
    case 0x2A: case 0x36: shift_down = release ? 0 : 1; return;
    case 0x1D: ctrl_down = release ? 0 : 1; return;
    case 0x38: alt_down = release ? 0 : 1; return;
    case 0x3A:
        if (!release) caps_on = !caps_on;
        return;
    case 0x45: /* num lock */ return;
    case 0x46: /* scroll lock */ return;
    default: break;
    }
    if (release) return;

    if (code >= 0x3B && code <= 0x44) {
        push_str(fkeys[code - 0x3B]);
        return;
    }
    if (code == 0x57) { push_str(fkeys[10]); return; }
    if (code == 0x58) { push_str(fkeys[11]); return; }

    char ch = 0;
    if (alt_down && altgr_map[code]) {
        ch = altgr_map[code];
    } else if (code >= 0x10 && code <= 0x19) {
        ch = normal_map[code];
        if (shift_down ^ caps_on) ch -= 32;
    } else if (code >= 0x1E && code <= 0x26) {
        ch = normal_map[code];
        if (shift_down ^ caps_on) ch -= 32;
    } else if (code >= 0x2C && code <= 0x32) {
        ch = normal_map[code];
        if (shift_down ^ caps_on) ch -= 32;
    } else {
        ch = shift_down ? shift_map[code] : normal_map[code];
    }

    if (ctrl_down && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 1);
    else if (ctrl_down && ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 1);
    else if (ctrl_down) {
        switch (ch) {
        case '[': ch = 0x1B; break;
        case '\\': ch = 0x1C; break;
        case ']': ch = 0x1D; break;
        case '^': ch = 0x1E; break;
        case '_': ch = 0x1F; break;
        case '@': ch = 0x00; break;
        case '?': ch = 0x7F; break;
        default: break;
        }
    }

    if (ch) console_input_push((uint8_t)ch);
}
