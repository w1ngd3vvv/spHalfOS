#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define __va_size(type) \
    (((sizeof(type) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))

#define va_start(ap, last) \
    ((ap) = (va_list)&(last) + __va_size(last))

#define va_arg(ap, type) \
    (*(type *)(((ap) += __va_size(type)) - __va_size(type)))

#define va_end(ap) ((void)0)

#define va_copy(dest, src) ((dest) = (src))

#endif
