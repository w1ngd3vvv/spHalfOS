#ifndef _DIRENT_H
#define _DIRENT_H
#include <stdint.h>
struct dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    char     d_name[60];
};
typedef struct __DIR DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
#endif
