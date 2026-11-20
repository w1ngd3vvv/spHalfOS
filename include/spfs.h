#ifndef SP_SPFS_H
#define SP_SPFS_H

#include "types.h"

#define FS_BLOCK_SIZE   4096u
#define FS_SECTORS_PER_BLOCK 8u
#define FS_MAX_INODES   256u
#define FS_MAX_NAME     58u

#define FS_MODE_FREE    0
#define FS_MODE_FILE    0x8000
#define FS_MODE_DIR     0x4000

#define FS_OK           0
#define FS_ERR          (-1)
#define FS_ENOENT       (-2)
#define FS_EEXIST       (-3)
#define FS_ENOTDIR      (-4)
#define FS_EISDIR       (-5)
#define FS_ENOSPC       (-6)
#define FS_EINVAL       (-7)
#define FS_ENAMETOOLONG (-8)
#define FS_ENOTEMPTY    (-9)
#define FS_EIO          (-10)

typedef struct PACKED {
    uint16_t mode;
    uint16_t links;
    uint32_t size;
    uint32_t direct[22];
    uint32_t indirect;
    uint32_t dindirect;
    uint32_t mtime;
    uint8_t  reserved[20];
} fs_inode_t;

typedef struct PACKED {
    uint32_t ino;
    uint8_t  type;      /* 1 file, 2 dir */
    uint8_t  namelen;
    char     name[FS_MAX_NAME];
} fs_dirent_t;

typedef struct PACKED {
    char     magic[4];
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_table;
    uint32_t inode_blocks;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t data_start;
    uint32_t root_ino;
    uint32_t free_blocks;
    uint8_t  reserved[FS_BLOCK_SIZE - 48];
} fs_super_t;

int fs_mount(void);
int fs_sync(void);
int fs_format(void);
uint32_t fs_root(void);
uint32_t fs_free_blocks(void);
uint32_t fs_total_blocks(void);

int fs_inode_get(uint32_t ino, fs_inode_t *out);
int fs_inode_put(uint32_t ino, const fs_inode_t *in);
int fs_lookup(uint32_t dir, const char *name, uint32_t *out);
int fs_resolve(uint32_t cwd, const char *path, uint32_t *out);
int fs_split_path(const char *path, char *dirbuf, int dirsz, char *namebuf, int namesz);

int fs_read_file(uint32_t ino, uint32_t off, void *buf, uint32_t n);
int fs_write_file(uint32_t ino, uint32_t off, const void *buf, uint32_t n);
int fs_truncate(uint32_t ino);

int fs_create(uint32_t dir, const char *name, uint16_t mode, uint32_t *out_ino);
int fs_mkdir(uint32_t dir, const char *name, uint32_t *out_ino);
int fs_unlink(uint32_t dir, const char *name);
int fs_rename(uint32_t olddir, const char *oldname, uint32_t newdir, const char *newname);

/* returns 1 entry, 0 end, negative error */
int fs_dir_iterate(uint32_t dir, uint32_t *cookie, char *name, int namesz,
                   uint32_t *ino, uint8_t *type);
int fs_is_dir(uint32_t ino);
int fs_get_path(uint32_t ino, char *buf, int size);

#endif
