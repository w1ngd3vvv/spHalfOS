/* mkfs for spFS: build a 64 MiB disk image and populate it from a staging dir */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define BLOCK 4096u
#define MAX_INODES 256u
#define MAX_NAME 58u
#define TOTAL_BLOCKS 16384u
#define INODE_TABLE 1u
#define INODE_BLOCKS 8u
#define BITMAP_START 9u
#define DATA_START 16u

#define MODE_FILE 0x8000
#define MODE_DIR  0x4000

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t links;
    uint32_t size;
    uint32_t direct[22];
    uint32_t indirect;
    uint32_t dindirect;
    uint32_t mtime;
    uint8_t  reserved[20];
} inode_t;

typedef struct __attribute__((packed)) {
    uint32_t ino;
    uint8_t  type;
    uint8_t  namelen;
    char     name[MAX_NAME];
} dirent_t;

typedef struct __attribute__((packed)) {
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
    uint8_t  reserved[BLOCK - 48];
} super_t;

static uint8_t *img;
static uint8_t bitmap[BLOCK];
static char *rootdir;

static void die(const char *msg) {
    fprintf(stderr, "mkfs: %s\n", msg);
    exit(1);
}

static int bm_test(uint32_t b) { return (bitmap[b >> 3] >> (b & 7)) & 1; }
static void bm_set(uint32_t b) { bitmap[b >> 3] |= (uint8_t)(1u << (b & 7)); }
static void bm_clear(uint32_t b) { bitmap[b >> 3] &= (uint8_t)~(1u << (b & 7)); }

static inode_t *inode_at(uint32_t ino) {
    return (inode_t *)(img + INODE_TABLE * BLOCK + ino * sizeof(inode_t));
}

static uint32_t alloc_block(void) {
    for (uint32_t b = DATA_START; b < TOTAL_BLOCKS; b++) {
        if (!bm_test(b)) {
            bm_set(b);
            memset(img + b * BLOCK, 0, BLOCK);
            return b;
        }
    }
    die("out of blocks");
    return 0;
}

static uint32_t alloc_inode(uint16_t mode) {
    for (uint32_t i = 1; i < MAX_INODES; i++) {
        inode_t *in = inode_at(i);
        if ((in->mode & 0xF000) == 0) {
            memset(in, 0, sizeof(*in));
            in->mode = mode;
            in->links = 1;
            in->mtime = (uint32_t)time(NULL);
            return i;
        }
    }
    die("out of inodes");
    return 0;
}

static uint32_t inode_block(inode_t *in, uint32_t lb, int alloc) {
    if (lb < 22) {
        if (!in->direct[lb] && alloc) in->direct[lb] = alloc_block();
        return in->direct[lb];
    }
    lb -= 22;
    if (lb < 1024) {
        if (!in->indirect && alloc) in->indirect = alloc_block();
        if (!in->indirect) return 0;
        uint32_t *p = (uint32_t *)(img + in->indirect * BLOCK);
        if (!p[lb] && alloc) {
            p[lb] = alloc_block();
            return p[lb];
        }
        return p[lb];
    }
    lb -= 1024;
    uint32_t i1 = lb / 1024, i2 = lb % 1024;
    if (!in->dindirect && alloc) in->dindirect = alloc_block();
    if (!in->dindirect) return 0;
    uint32_t *p = (uint32_t *)(img + in->dindirect * BLOCK);
    if (!p[i1] && alloc) p[i1] = alloc_block();
    if (!p[i1]) return 0;
    uint32_t *q = (uint32_t *)(img + p[i1] * BLOCK);
    if (!q[i2] && alloc) q[i2] = alloc_block();
    return q[i2];
}

static void write_inode_data(uint32_t ino, const void *data, uint32_t size) {
    inode_t *in = inode_at(ino);
    const uint8_t *src = data;
    uint32_t done = 0;
    while (done < size) {
        uint32_t lb = done / BLOCK;
        uint32_t off = done % BLOCK;
        uint32_t chunk = BLOCK - off;
        if (chunk > size - done) chunk = size - done;
        uint32_t blk = inode_block(in, lb, 1);
        if (!blk) die("out of blocks while writing file");
        memcpy(img + blk * BLOCK + off, src + done, chunk);
        done += chunk;
    }
    in->size = size;
}

static uint32_t dir_lookup(uint32_t dir, const char *name) {
    inode_t *in = inode_at(dir);
    uint32_t count = in->size / sizeof(dirent_t);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t lb = (i * sizeof(dirent_t)) / BLOCK;
        uint32_t off = (i * sizeof(dirent_t)) % BLOCK;
        uint32_t blk = inode_block(in, lb, 0);
        if (!blk) continue;
        dirent_t *de = (dirent_t *)(img + blk * BLOCK + off);
        if (de->ino && de->namelen == strlen(name) && !memcmp(de->name, name, de->namelen))
            return de->ino;
    }
    return 0;
}

static void dir_add(uint32_t dir, const char *name, uint32_t ino, uint8_t type) {
    if (strlen(name) > MAX_NAME) die("file name too long");
    inode_t *in = inode_at(dir);
    uint32_t pos = in->size;
    uint32_t lb = pos / BLOCK;
    uint32_t off = pos % BLOCK;
    uint32_t blk = inode_block(in, lb, 1);
    if (!blk) die("out of blocks for directory");
    dirent_t de;
    memset(&de, 0, sizeof(de));
    de.ino = ino;
    de.type = type;
    de.namelen = (uint8_t)strlen(name);
    memcpy(de.name, name, de.namelen);
    memcpy(img + blk * BLOCK + off, &de, sizeof(de));
    in = inode_at(dir);
    in->size = pos + sizeof(de);
}

static uint32_t make_dir(uint32_t parent, const char *name) {
    uint32_t ino = alloc_inode(MODE_DIR);
    inode_t *in = inode_at(ino);
    uint32_t blk = alloc_block();
    in->direct[0] = blk;
    in->size = 2 * sizeof(dirent_t);
    dirent_t d;
    memset(&d, 0, sizeof(d));
    d.ino = ino; d.type = 2; d.namelen = 1; d.name[0] = '.';
    memcpy(img + blk * BLOCK, &d, sizeof(d));
    memset(&d, 0, sizeof(d));
    d.ino = parent; d.type = 2; d.namelen = 2; d.name[0] = '.'; d.name[1] = '.';
    memcpy(img + blk * BLOCK + sizeof(d), &d, sizeof(d));
    if (parent) dir_add(parent, name, ino, 2);
    return ino;
}

static void copy_file(uint32_t dir, const char *path, const char *name) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkfs: cannot open %s: %s\n", path, strerror(errno)); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) die("ftell failed");
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf) die("out of memory");
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) die("read failed");
    fclose(f);
    uint32_t ino = alloc_inode(MODE_FILE);
    write_inode_data(ino, buf, (uint32_t)sz);
    dir_add(dir, name, ino, 1);
    free(buf);
}

static int cmp_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void copy_tree(uint32_t dir, const char *path) {
    DIR *d = opendir(path);
    if (!d) { fprintf(stderr, "mkfs: cannot open dir %s: %s\n", path, strerror(errno)); exit(1); }
    char *names[4096];
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < 4096) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        names[count++] = strdup(ent->d_name);
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(char *), cmp_names);

    for (int i = 0; i < count; i++) {
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, names[i]);
        struct stat st;
        if (stat(full, &st) < 0) die("stat failed");
        if (S_ISDIR(st.st_mode)) {
            uint32_t sub = make_dir(dir, names[i]);
            copy_tree(sub, full);
        } else if (S_ISREG(st.st_mode)) {
            copy_file(dir, full, names[i]);
        }
        free(names[i]);
    }
}

int main(int argc, char **argv) {
    const char *outpath = argc > 1 ? argv[1] : "build/disk.img";
    rootdir = argc > 2 ? argv[2] : NULL;

    img = calloc(TOTAL_BLOCKS, BLOCK);
    if (!img) die("out of memory");

    super_t *sb = (super_t *)img;
    memcpy(sb->magic, "spFS", 4);
    sb->version = 1;
    sb->block_size = BLOCK;
    sb->total_blocks = TOTAL_BLOCKS;
    sb->inode_count = MAX_INODES;
    sb->inode_table = INODE_TABLE;
    sb->inode_blocks = INODE_BLOCKS;
    sb->bitmap_start = BITMAP_START;
    sb->bitmap_blocks = 1;
    sb->data_start = DATA_START;
    sb->root_ino = 1;

    memset(bitmap, 0, sizeof(bitmap));
    for (uint32_t b = 0; b < DATA_START; b++) bm_set(b);
    uint32_t rootblk = alloc_block();
    sb->free_blocks = TOTAL_BLOCKS - DATA_START - 1;

    inode_t *root = inode_at(1);
    memset(root, 0, sizeof(*root));
    root->mode = MODE_DIR;
    root->links = 2;
    root->size = 2 * sizeof(dirent_t);
    root->direct[0] = rootblk;
    root->mtime = (uint32_t)time(NULL);
    dirent_t d;
    memset(&d, 0, sizeof(d));
    d.ino = 1; d.type = 2; d.namelen = 1; d.name[0] = '.';
    memcpy(img + rootblk * BLOCK, &d, sizeof(d));
    memset(&d, 0, sizeof(d));
    d.ino = 1; d.type = 2; d.namelen = 2; d.name[0] = '.'; d.name[1] = '.';
    memcpy(img + rootblk * BLOCK + sizeof(d), &d, sizeof(d));

    if (rootdir) copy_tree(1, rootdir);

    /* free block count */
    uint32_t used = 0;
    for (uint32_t b = 0; b < TOTAL_BLOCKS; b++) if (bm_test(b)) used++;
    sb->free_blocks = TOTAL_BLOCKS - used;

    memcpy(img + BITMAP_START * BLOCK, bitmap, BLOCK);

    FILE *f = fopen(outpath, "wb");
    if (!f) { fprintf(stderr, "mkfs: cannot create %s: %s\n", outpath, strerror(errno)); return 1; }
    if (fwrite(img, 1, (size_t)TOTAL_BLOCKS * BLOCK, f) != (size_t)TOTAL_BLOCKS * BLOCK)
        die("write failed");
    fclose(f);
    printf("mkfs: wrote %s (%u blocks, %u free)\n", outpath, TOTAL_BLOCKS, sb->free_blocks);
    return 0;
}
