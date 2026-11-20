/* spHalfOS filesystem "spFS": simple inode/bitmap filesystem on ATA disk */
#include "spfs.h"
#include "ata.h"
#include "kernel.h"
#include "string.h"
#include "mm.h"

static fs_super_t sb;
static uint32_t bitmap[FS_BLOCK_SIZE / 4];
static int mounted;
static uint8_t zero_block[FS_BLOCK_SIZE];

static int disk_read_block(uint32_t blk, void *buf) {
    if (blk >= sb.total_blocks) return FS_EIO;
    return ata_read_sectors(blk * FS_SECTORS_PER_BLOCK, FS_SECTORS_PER_BLOCK, buf) < 0 ? FS_EIO : FS_OK;
}

static int disk_write_block(uint32_t blk, const void *buf) {
    if (blk >= sb.total_blocks) return FS_EIO;
    return ata_write_sectors(blk * FS_SECTORS_PER_BLOCK, FS_SECTORS_PER_BLOCK, buf) < 0 ? FS_EIO : FS_OK;
}

static inline int bm_test(uint32_t b) { return (bitmap[b >> 5] >> (b & 31)) & 1; }
static inline void bm_set(uint32_t b) { bitmap[b >> 5] |= (1u << (b & 31)); }
static inline void bm_clear(uint32_t b) { bitmap[b >> 5] &= ~(1u << (b & 31)); }

int fs_sync(void) {
    if (!mounted) return FS_EIO;
    int r = disk_write_block(sb.bitmap_start, bitmap);
    if (r < 0) return r;
    return disk_write_block(0, &sb);
}

static uint32_t fs_alloc_block(void) {
    for (uint32_t b = sb.data_start; b < sb.total_blocks; b++) {
        if (!bm_test(b)) {
            bm_set(b);
            if (sb.free_blocks) sb.free_blocks--;
            disk_write_block(b, zero_block);
            fs_sync();
            return b;
        }
    }
    return 0;
}

static void fs_free_block(uint32_t b) {
    if (b < sb.data_start || b >= sb.total_blocks) return;
    if (bm_test(b)) {
        bm_clear(b);
        sb.free_blocks++;
        fs_sync();
    }
}

static int inode_location(uint32_t ino, uint32_t *blk, uint32_t *off) {
    if (ino == 0 || ino >= sb.inode_count) return FS_EINVAL;
    uint32_t byte = ino * sizeof(fs_inode_t);
    *blk = sb.inode_table + byte / FS_BLOCK_SIZE;
    *off = byte % FS_BLOCK_SIZE;
    return FS_OK;
}

int fs_inode_get(uint32_t ino, fs_inode_t *out) {
    uint32_t blk, off;
    if (!mounted) return FS_EIO;
    if (inode_location(ino, &blk, &off) < 0) return FS_EINVAL;
    uint8_t buf[FS_BLOCK_SIZE];
    if (disk_read_block(blk, buf) < 0) return FS_EIO;
    memcpy(out, buf + off, sizeof(fs_inode_t));
    return FS_OK;
}

int fs_inode_put(uint32_t ino, const fs_inode_t *in) {
    uint32_t blk, off;
    if (!mounted) return FS_EIO;
    if (inode_location(ino, &blk, &off) < 0) return FS_EINVAL;
    uint8_t buf[FS_BLOCK_SIZE];
    if (disk_read_block(blk, buf) < 0) return FS_EIO;
    memcpy(buf + off, in, sizeof(fs_inode_t));
    return disk_write_block(blk, buf);
}

static int inode_get_block(fs_inode_t *in, uint32_t lb, uint32_t *out, int alloc) {
    *out = 0;
    uint32_t ptr[FS_BLOCK_SIZE / 4];
    uint8_t buf[FS_BLOCK_SIZE];

    if (lb < 22) {
        if (!in->direct[lb] && alloc) in->direct[lb] = fs_alloc_block();
        *out = in->direct[lb];
        return *out ? FS_OK : FS_ENOSPC;
    }
    lb -= 22;

    if (lb < 1024) {
        if (!in->indirect && alloc) in->indirect = fs_alloc_block();
        if (!in->indirect) return alloc ? FS_ENOSPC : FS_OK;
        if (disk_read_block(in->indirect, buf) < 0) return FS_EIO;
        memcpy(ptr, buf, FS_BLOCK_SIZE);
        if (!ptr[lb] && alloc) {
            ptr[lb] = fs_alloc_block();
            if (!ptr[lb]) return FS_ENOSPC;
            memcpy(buf, ptr, FS_BLOCK_SIZE);
            disk_write_block(in->indirect, buf);
        }
        *out = ptr[lb];
        return *out ? FS_OK : FS_OK;
    }
    lb -= 1024;

    uint32_t i1 = lb / 1024;
    uint32_t i2 = lb % 1024;
    if (i1 >= 1024) return FS_EINVAL;
    if (!in->dindirect && alloc) in->dindirect = fs_alloc_block();
    if (!in->dindirect) return alloc ? FS_ENOSPC : FS_OK;
    if (disk_read_block(in->dindirect, buf) < 0) return FS_EIO;
    memcpy(ptr, buf, FS_BLOCK_SIZE);
    uint32_t l2 = ptr[i1];
    if (!l2 && alloc) {
        l2 = fs_alloc_block();
        if (!l2) return FS_ENOSPC;
        ptr[i1] = l2;
        memcpy(buf, ptr, FS_BLOCK_SIZE);
        disk_write_block(in->dindirect, buf);
    }
    if (!l2) return FS_OK;
    if (disk_read_block(l2, buf) < 0) return FS_EIO;
    memcpy(ptr, buf, FS_BLOCK_SIZE);
    if (!ptr[i2] && alloc) {
        ptr[i2] = fs_alloc_block();
        if (!ptr[i2]) return FS_ENOSPC;
        memcpy(buf, ptr, FS_BLOCK_SIZE);
        disk_write_block(l2, buf);
    }
    *out = ptr[i2];
    return FS_OK;
}

int fs_read_file(uint32_t ino, uint32_t off, void *buf, uint32_t n) {
    fs_inode_t in;
    if (fs_inode_get(ino, &in) < 0) return FS_EIO;
    if ((in.mode & 0xF000) != FS_MODE_FILE && (in.mode & 0xF000) != FS_MODE_DIR) return FS_EINVAL;
    if (off >= in.size) return 0;
    if (n > in.size - off) n = in.size - off;

    uint8_t *dst = buf;
    uint32_t done = 0;
    uint8_t blk[FS_BLOCK_SIZE];
    while (done < n) {
        uint32_t lb = (off + done) / FS_BLOCK_SIZE;
        uint32_t boff = (off + done) % FS_BLOCK_SIZE;
        uint32_t chunk = FS_BLOCK_SIZE - boff;
        if (chunk > n - done) chunk = n - done;
        uint32_t phys = 0;
        int r = inode_get_block(&in, lb, &phys, 0);
        if (r < 0) return r;
        if (!phys) memset(blk, 0, FS_BLOCK_SIZE);
        else if (disk_read_block(phys, blk) < 0) return FS_EIO;
        memcpy(dst + done, blk + boff, chunk);
        done += chunk;
    }
    return (int)done;
}

int fs_write_file(uint32_t ino, uint32_t off, const void *buf, uint32_t n) {
    fs_inode_t in;
    if (fs_inode_get(ino, &in) < 0) return FS_EIO;
    if ((in.mode & 0xF000) != FS_MODE_FILE && (in.mode & 0xF000) != FS_MODE_DIR)
        return FS_EINVAL;

    const uint8_t *src = buf;
    uint32_t done = 0;
    uint8_t blk[FS_BLOCK_SIZE];
    while (done < n) {
        uint32_t lb = (off + done) / FS_BLOCK_SIZE;
        uint32_t boff = (off + done) % FS_BLOCK_SIZE;
        uint32_t chunk = FS_BLOCK_SIZE - boff;
        if (chunk > n - done) chunk = n - done;
        uint32_t phys = 0;
        int r = inode_get_block(&in, lb, &phys, 1);
        if (r < 0) return r;
        if (!phys) return FS_ENOSPC;
        if (chunk == FS_BLOCK_SIZE && boff == 0) {
            memset(blk, 0, FS_BLOCK_SIZE);
        } else if (disk_read_block(phys, blk) < 0) {
            return FS_EIO;
        }
        memcpy(blk + boff, src + done, chunk);
        if (disk_write_block(phys, blk) < 0) return FS_EIO;
        done += chunk;
    }
    uint32_t end = off + n;
    if (end > in.size) in.size = end;
    if (fs_inode_put(ino, &in) < 0) return FS_EIO;
    return (int)done;
}

int fs_truncate(uint32_t ino) {
    fs_inode_t in;
    if (fs_inode_get(ino, &in) < 0) return FS_EIO;

    uint32_t nblocks = (in.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t phys;
    for (uint32_t lb = 0; lb < nblocks; lb++) {
        if (inode_get_block(&in, lb, &phys, 0) == FS_OK && phys)
            fs_free_block(phys);
    }
    if (in.indirect) { fs_free_block(in.indirect); in.indirect = 0; }
    if (in.dindirect) {
        uint8_t buf[FS_BLOCK_SIZE];
        uint32_t ptr[FS_BLOCK_SIZE / 4];
        if (disk_read_block(in.dindirect, buf) == FS_OK) {
            memcpy(ptr, buf, FS_BLOCK_SIZE);
            for (int i = 0; i < 1024; i++)
                if (ptr[i]) fs_free_block(ptr[i]);
        }
        fs_free_block(in.dindirect);
        in.dindirect = 0;
    }
    memset(in.direct, 0, sizeof(in.direct));
    in.size = 0;
    return fs_inode_put(ino, &in);
}

static int dir_find(uint32_t dir, const char *name, uint32_t *out_ino, uint32_t *out_pos) {
    fs_inode_t in;
    if (fs_inode_get(dir, &in) < 0) return FS_EIO;
    if ((in.mode & 0xF000) != FS_MODE_DIR) return FS_ENOTDIR;
    fs_dirent_t de;
    for (uint32_t pos = 0; pos + sizeof(de) <= in.size; pos += sizeof(de)) {
        int r = fs_read_file(dir, pos, &de, sizeof(de));
        if (r != (int)sizeof(de)) return FS_EIO;
        if (de.ino == 0) continue;
        if (de.namelen == strlen(name) && !memcmp(de.name, name, de.namelen)) {
            if (out_ino) *out_ino = de.ino;
            if (out_pos) *out_pos = pos;
            return FS_OK;
        }
    }
    return FS_ENOENT;
}

int fs_lookup(uint32_t dir, const char *name, uint32_t *out) {
    return dir_find(dir, name, out, NULL);
}

static int dir_add(uint32_t dir, const char *name, uint32_t ino, uint8_t type) {
    fs_inode_t in;
    if (fs_inode_get(dir, &in) < 0) return FS_EIO;
    if ((in.mode & 0xF000) != FS_MODE_DIR) return FS_ENOTDIR;
    size_t nlen = strlen(name);
    if (nlen == 0 || nlen > FS_MAX_NAME) return FS_ENAMETOOLONG;

    fs_dirent_t de;
    fs_dirent_t tmp;
    uint32_t pos = in.size;
    for (uint32_t p = 0; p + sizeof(de) <= in.size; p += sizeof(de)) {
        if (fs_read_file(dir, p, &tmp, sizeof(tmp)) != (int)sizeof(tmp)) return FS_EIO;
        if (tmp.ino == 0) { pos = p; break; }
    }
    memset(&de, 0, sizeof(de));
    de.ino = ino;
    de.type = type;
    de.namelen = (uint8_t)nlen;
    memcpy(de.name, name, nlen);
    int r = fs_write_file(dir, pos, &de, sizeof(de));
    return r == (int)sizeof(de) ? FS_OK : r;
}

static int dir_remove(uint32_t dir, const char *name) {
    uint32_t pos;
    int r = dir_find(dir, name, NULL, &pos);
    if (r < 0) return r;
    fs_dirent_t de;
    memset(&de, 0, sizeof(de));
    r = fs_write_file(dir, pos, &de, sizeof(de));
    return r == (int)sizeof(de) ? FS_OK : r;
}

static int alloc_inode(uint16_t mode, uint32_t *out) {
    fs_inode_t in;
    for (uint32_t i = 1; i < sb.inode_count; i++) {
        if (fs_inode_get(i, &in) < 0) return FS_EIO;
        if ((in.mode & 0xF000) == FS_MODE_FREE) {
            memset(&in, 0, sizeof(in));
            in.mode = mode;
            in.links = 1;
            if (fs_inode_put(i, &in) < 0) return FS_EIO;
            *out = i;
            return FS_OK;
        }
    }
    return FS_ENOSPC;
}

static void free_inode(uint32_t ino) {
    fs_inode_t in;
    if (fs_inode_get(ino, &in) == FS_OK) {
        memset(&in, 0, sizeof(in));
        fs_inode_put(ino, &in);
    }
}

int fs_create(uint32_t dir, const char *name, uint16_t mode, uint32_t *out_ino) {
    if (!strcmp(name, ".") || !strcmp(name, "..")) return FS_EINVAL;
    if (strlen(name) > FS_MAX_NAME) return FS_ENAMETOOLONG;
    uint32_t old;
    int r = dir_find(dir, name, &old, NULL);
    if (r == FS_OK) return FS_EEXIST;
    if (r != FS_ENOENT) return r;
    uint32_t ino;
    r = alloc_inode(mode, &ino);
    if (r < 0) return r;
    r = dir_add(dir, name, ino, (mode & 0xF000) == FS_MODE_DIR ? 2 : 1);
    if (r < 0) { free_inode(ino); return r; }
    if (out_ino) *out_ino = ino;
    return FS_OK;
}

int fs_mkdir(uint32_t dir, const char *name, uint32_t *out_ino) {
    if (!strcmp(name, ".") || !strcmp(name, "..")) return FS_EINVAL;
    if (strlen(name) > FS_MAX_NAME) return FS_ENAMETOOLONG;
    uint32_t old;
    int r = dir_find(dir, name, &old, NULL);
    if (r == FS_OK) return FS_EEXIST;
    if (r != FS_ENOENT) return r;

    uint32_t ino;
    r = alloc_inode(FS_MODE_DIR, &ino);
    if (r < 0) return r;
    fs_inode_t in;
    fs_inode_get(ino, &in);
    in.size = 2 * sizeof(fs_dirent_t);
    in.direct[0] = fs_alloc_block();
    if (!in.direct[0]) { free_inode(ino); return FS_ENOSPC; }
    fs_inode_put(ino, &in);

    fs_dirent_t de;
    uint8_t blk[FS_BLOCK_SIZE];
    memset(blk, 0, sizeof(blk));
    memset(&de, 0, sizeof(de));
    de.ino = ino; de.type = 2; de.namelen = 1; de.name[0] = '.';
    memcpy(blk, &de, sizeof(de));
    memset(&de, 0, sizeof(de));
    de.ino = dir; de.type = 2; de.namelen = 2; de.name[0] = '.'; de.name[1] = '.';
    memcpy(blk + sizeof(de), &de, sizeof(de));
    disk_write_block(in.direct[0], blk);

    r = dir_add(dir, name, ino, 2);
    if (r < 0) {
        fs_truncate(ino);
        free_inode(ino);
        return r;
    }
    if (out_ino) *out_ino = ino;
    return FS_OK;
}

int fs_unlink(uint32_t dir, const char *name) {
    uint32_t ino, pos;
    int r = dir_find(dir, name, &ino, &pos);
    if (r < 0) return r;
    if (ino == sb.root_ino) return FS_EINVAL;
    fs_inode_t in;
    if (fs_inode_get(ino, &in) < 0) return FS_EIO;
    if ((in.mode & 0xF000) == FS_MODE_DIR) {
        fs_dirent_t de;
        for (uint32_t p = 2 * sizeof(de); p + sizeof(de) <= in.size; p += sizeof(de)) {
            if (fs_read_file(ino, p, &de, sizeof(de)) != (int)sizeof(de)) return FS_EIO;
            if (de.ino != 0) return FS_ENOTEMPTY;
        }
        fs_truncate(ino);
    } else {
        fs_truncate(ino);
    }
    free_inode(ino);
    return dir_remove(dir, name);
}

int fs_rename(uint32_t olddir, const char *oldname, uint32_t newdir, const char *newname) {
    uint32_t ino;
    int r = dir_find(olddir, oldname, &ino, NULL);
    if (r < 0) return r;
    if (dir_find(newdir, newname, NULL, NULL) == FS_OK) return FS_EEXIST;
    fs_inode_t in;
    if (fs_inode_get(ino, &in) < 0) return FS_EIO;
    r = dir_add(newdir, newname, ino, (in.mode & 0xF000) == FS_MODE_DIR ? 2 : 1);
    if (r < 0) return r;
    dir_remove(olddir, oldname);
    if ((in.mode & 0xF000) == FS_MODE_DIR && olddir != newdir) {
        fs_dirent_t de;
        if (fs_read_file(ino, sizeof(de), &de, sizeof(de)) == (int)sizeof(de)) {
            de.ino = newdir;
            fs_write_file(ino, sizeof(de), &de, sizeof(de));
        }
    }
    return FS_OK;
}

int fs_is_dir(uint32_t ino) {
    fs_inode_t in;
    if (fs_inode_get(ino, &in) < 0) return 0;
    return (in.mode & 0xF000) == FS_MODE_DIR;
}

static int fs_name_in_parent(uint32_t parent, uint32_t ino, char *name, int namesz) {
    uint32_t cookie = 0, found;
    uint8_t type;
    char tmp[FS_MAX_NAME + 2];
    while (fs_dir_iterate(parent, &cookie, tmp, sizeof(tmp), &found, &type) == 1) {
        if (found == ino && strcmp(tmp, ".") && strcmp(tmp, "..")) {
            strncpy(name, tmp, namesz - 1);
            name[namesz - 1] = 0;
            return 0;
        }
    }
    return -1;
}

static int fs_build_path(uint32_t ino, char *buf, int size, int depth) {
    if (depth > 32) return -1;
    if (ino == sb.root_ino) {
        if (size < 2) return -1;
        strcpy(buf, "/");
        return 0;
    }
    uint32_t parent;
    if (dir_find(ino, "..", &parent, NULL) < 0) return -1;
    char name[FS_MAX_NAME + 1];
    if (fs_name_in_parent(parent, ino, name, sizeof(name)) < 0) return -1;
    char ppath[256];
    if (fs_build_path(parent, ppath, sizeof(ppath), depth + 1) < 0) return -1;
    if (!strcmp(ppath, "/")) {
        if ((int)strlen(name) + 2 > size) return -1;
        buf[0] = '/';
        strcpy(buf + 1, name);
    } else {
        if ((int)(strlen(ppath) + strlen(name) + 2) > size) return -1;
        strcpy(buf, ppath);
        strcat(buf, "/");
        strcat(buf, name);
    }
    return 0;
}

int fs_get_path(uint32_t ino, char *buf, int size) {
    if (!mounted || size < 2) return FS_EINVAL;
    return fs_build_path(ino, buf, size, 0);
}

int fs_dir_iterate(uint32_t dir, uint32_t *cookie, char *name, int namesz,
                   uint32_t *ino, uint8_t *type) {
    fs_inode_t in;
    if (fs_inode_get(dir, &in) < 0) return FS_EIO;
    if ((in.mode & 0xF000) != FS_MODE_DIR) return FS_ENOTDIR;
    fs_dirent_t de;
    uint32_t pos = *cookie;
    while (pos + sizeof(de) <= in.size) {
        if (fs_read_file(dir, pos, &de, sizeof(de)) != (int)sizeof(de)) return FS_EIO;
        *cookie = pos + sizeof(de);
        pos += sizeof(de);
        if (de.ino == 0 || de.ino >= sb.inode_count) continue;
        int n = de.namelen;
        if (n >= namesz) n = namesz - 1;
        memcpy(name, de.name, n);
        name[n] = 0;
        if (ino) *ino = de.ino;
        if (type) *type = de.type;
        return 1;
    }
    return 0;
}

static int resolve_component(uint32_t cur, const char *name, uint32_t *out, int must_be_dir) {
    if (!strcmp(name, ".")) { *out = cur; return FS_OK; }
    if (!strcmp(name, "..")) {
        uint32_t up;
        int r = dir_find(cur, "..", &up, NULL);
        if (r < 0) { *out = sb.root_ino; return FS_OK; }
        *out = up;
        return FS_OK;
    }
    uint32_t next;
    int r = dir_find(cur, name, &next, NULL);
    if (r < 0) return FS_ENOENT;
    if (must_be_dir && !fs_is_dir(next)) return FS_ENOTDIR;
    *out = next;
    return FS_OK;
}

int fs_resolve(uint32_t cwd, const char *path, uint32_t *out) {
    if (!mounted) return FS_EIO;
    char tmp[512];
    size_t plen = strlen(path);
    if (plen >= sizeof(tmp)) return FS_ENAMETOOLONG;
    strcpy(tmp, path);

    uint32_t cur = (tmp[0] == '/') ? sb.root_ino : cwd;
    char *p = tmp;
    while (*p == '/') p++;
    while (*p) {
        char *slash = strchr(p, '/');
        int last = 0;
        if (slash) *slash = 0; else last = 1;
        if (*p) {
            int r = resolve_component(cur, p, &cur, !last);
            if (r < 0) return r;
        }
        if (last) break;
        p = slash + 1;
        while (*p == '/') p++;
        last = (*p == 0);
        if (last) break;
    }
    *out = cur;
    return FS_OK;
}

int fs_split_path(const char *path, char *dirbuf, int dirsz, char *namebuf, int namesz) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        if (dirsz < 2 || namesz < (int)strlen(path) + 1) return FS_ENAMETOOLONG;
        strcpy(dirbuf, ".");
        strcpy(namebuf, path);
        return FS_OK;
    }
    int dlen = (int)(slash - path);
    if (dlen == 0) {
        if (dirsz < 2) return FS_ENAMETOOLONG;
        strcpy(dirbuf, "/");
    } else {
        if (dlen >= dirsz) return FS_ENAMETOOLONG;
        memcpy(dirbuf, path, dlen);
        dirbuf[dlen] = 0;
    }
    if ((int)strlen(slash + 1) >= namesz) return FS_ENAMETOOLONG;
    strcpy(namebuf, slash + 1);
    return FS_OK;
}

int fs_format(void) {
    uint32_t total = ata_disk_sectors() / FS_SECTORS_PER_BLOCK;
    if (total < 32) return FS_EIO;
    if (total > 32768) total = 32768;

    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, "spFS", 4);
    sb.version = 1;
    sb.block_size = FS_BLOCK_SIZE;
    sb.total_blocks = total;
    sb.inode_count = FS_MAX_INODES;
    sb.inode_table = 1;
    sb.inode_blocks = (FS_MAX_INODES * sizeof(fs_inode_t) + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    sb.bitmap_start = sb.inode_table + sb.inode_blocks;
    sb.bitmap_blocks = 1;
    sb.data_start = sb.bitmap_start + sb.bitmap_blocks;
    if (sb.data_start < 16) sb.data_start = 16;
    sb.root_ino = 1;

    memset(bitmap, 0, sizeof(bitmap));
    for (uint32_t b = 0; b < sb.data_start; b++) bm_set(b);
    uint32_t rootblk = sb.data_start;
    bm_set(rootblk);
    sb.free_blocks = total - sb.data_start - 1;

    mounted = 1;

    /* zero inode table */
    for (uint32_t i = 0; i < sb.inode_blocks; i++)
        disk_write_block(sb.inode_table + i, zero_block);

    fs_inode_t root;
    memset(&root, 0, sizeof(root));
    root.mode = FS_MODE_DIR;
    root.links = 2;
    root.size = 2 * sizeof(fs_dirent_t);
    root.direct[0] = rootblk;
    fs_inode_put(sb.root_ino, &root);

    uint8_t blk[FS_BLOCK_SIZE];
    memset(blk, 0, sizeof(blk));
    fs_dirent_t de;
    memset(&de, 0, sizeof(de));
    de.ino = sb.root_ino; de.type = 2; de.namelen = 1; de.name[0] = '.';
    memcpy(blk, &de, sizeof(de));
    memset(&de, 0, sizeof(de));
    de.ino = sb.root_ino; de.type = 2; de.namelen = 2; de.name[0] = '.'; de.name[1] = '.';
    memcpy(blk + sizeof(de), &de, sizeof(de));
    disk_write_block(rootblk, blk);

    return fs_sync();
}

int fs_mount(void) {
    memset(zero_block, 0, sizeof(zero_block));
    if (ata_init() < 0) {
        kprintf("[fs] no ATA disk\n");
        return FS_EIO;
    }
    uint8_t buf[FS_BLOCK_SIZE];
    if (ata_read_sectors(0, FS_SECTORS_PER_BLOCK, buf) < 0) {
        kprintf("[fs] block 0 read failed\n");
        return FS_EIO;
    }
    kprintf("[fs] super magic %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
    memcpy(&sb, buf, sizeof(sb));
    if (memcmp(sb.magic, "spFS", 4) || sb.version != 1 ||
        sb.block_size != FS_BLOCK_SIZE || sb.total_blocks > 32768 || sb.data_start < 8) {
        kprintf("[fs] invalid/blank disk, formatting\n");
        memset(&sb, 0, sizeof(sb));
        int fr = fs_format();
        kprintf("[fs] format result %d free=%u total=%u\n", fr, sb.free_blocks, sb.total_blocks);
        return fr;
    }
    if (disk_read_block(sb.bitmap_start, bitmap) < 0) return FS_EIO;
    mounted = 1;
    fs_inode_t root;
    if (fs_inode_get(sb.root_ino, &root) < 0 || (root.mode & 0xF000) != FS_MODE_DIR) {
        kprintf("[fs] bad root inode\n");
        mounted = 0;
        return FS_EIO;
    }
    return FS_OK;
}

uint32_t fs_root(void) { return sb.root_ino; }
uint32_t fs_free_blocks(void) { return sb.free_blocks; }
uint32_t fs_total_blocks(void) { return sb.total_blocks; }
