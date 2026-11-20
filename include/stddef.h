#ifndef SP_STDDEF_H
#define SP_STDDEF_H
#include "types.h"
#ifndef NULL
#define NULL ((void*)0)
#endif
#define offsetof(t,m) __builtin_offsetof(t,m)
#endif
