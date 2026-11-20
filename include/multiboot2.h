#ifndef SP_MULTIBOOT2_H
#define SP_MULTIBOOT2_H

#include "types.h"

#define MB2_BOOTLOADER_MAGIC 0x36d76289
#define MB2_TAG_END          0
#define MB2_TAG_CMDLINE      1
#define MB2_TAG_BOOTLOADER   2
#define MB2_TAG_MODULE       3
#define MB2_TAG_BASIC_MEM    4
#define MB2_TAG_MMAP         6
#define MB2_TAG_FRAMEBUFFER  8
#define MB2_TAG_ACPI_OLD     14
#define MB2_TAG_ACPI_NEW     15

#define MB2_MEM_AVAILABLE    1
#define MB2_MEM_RESERVED     2
#define MB2_MEM_ACPI         3
#define MB2_MEM_NVS          4
#define MB2_MEM_BADRAM       5

typedef struct PACKED {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} mb2_tag_basic_mem_t;

typedef struct PACKED {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} mb2_mmap_entry_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* entries follow */
} mb2_tag_mmap_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char string[0];
} mb2_tag_module_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
} mb2_tag_framebuffer_t;

#endif
