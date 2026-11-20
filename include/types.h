/* spHalfOS - basic integer types (i386) */
#ifndef SP_TYPES_H
#define SP_TYPES_H

typedef unsigned char       uint8_t;
typedef signed char         int8_t;
typedef unsigned short      uint16_t;
typedef signed short        int16_t;
typedef unsigned int        uint32_t;
typedef signed int          int32_t;
typedef unsigned long long  uint64_t;
typedef signed long long    int64_t;

typedef __SIZE_TYPE__       size_t;
typedef __PTRDIFF_TYPE__    ptrdiff_t;
typedef int                 ssize_t;
typedef long                off_t;
typedef unsigned int        ino_t;
typedef unsigned int        mode_t;
typedef unsigned int        time_t;
typedef unsigned int        dev_t;
typedef unsigned int        uid_t;
typedef unsigned int        gid_t;
typedef int                 pid_t;

#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define PACKED __attribute__((packed))
#define NORETURN __attribute__((noreturn))
#define UNUSED(x) ((void)(x))

typedef struct { int quot, rem; } div_t;

#endif
