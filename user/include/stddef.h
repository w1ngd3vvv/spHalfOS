#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned int size_t;
typedef int ptrdiff_t;
typedef int wchar_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(t,m) __builtin_offsetof(t,m)
#endif
