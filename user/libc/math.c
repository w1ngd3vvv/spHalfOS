#include <math.h>
#include <stdint.h>

double fabs(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    v.u &= 0x7FFFFFFFFFFFFFFFULL;
    return v.d;
}
float fabsf(float x) { return (float)fabs((double)x); }

double ldexp(double x, int e) {
    double r;
    __asm__ volatile ("fscale" : "=t"(r) : "0"(x), "u"((double)e) : "st(1)");
    return r;
}
float ldexpf(float x, int e) { return (float)ldexp((double)x, e); }

double frexp(double x, int *e) {
    union { double d; uint64_t u; } v;
    v.d = x;
    int exp = (int)((v.u >> 52) & 0x7FF);
    if (exp == 0) {
        if (x == 0.0) { if (e) *e = 0; return 0.0; }
        x = frexp(x * 9007199254740992.0, e);
        if (e) *e -= 53;
        return x;
    }
    if (exp == 0x7FF) { if (e) *e = 0; return x; }
    if (e) *e = exp - 1022;
    v.u = (v.u & 0x800FFFFFFFFFFFFFULL) | (1022ULL << 52);
    return v.d;
}
float frexpf(float x, int *e) { return (float)frexp((double)x, e); }

double floor(double x) {
    if (x >= 9.2e18 || x <= -9.2e18) return x;
    long long i = (long long)x;
    if ((double)i > x) i--;
    return (double)i;
}
double ceil(double x) {
    if (x >= 9.2e18 || x <= -9.2e18) return x;
    long long i = (long long)x;
    if ((double)i < x) i++;
    return (double)i;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double r;
    __asm__ volatile (
        "1:\n\t"
        "fprem\n\t"
        "fnstsw %%ax\n\t"
        "testb $4, %%ah\n\t"
        "jnz 1b\n\t"
        : "=t"(r) : "0"(x), "u"(y) : "ax", "st(1)");
    return r;
}

double sqrt(double x) {
    double r;
    __asm__ volatile ("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

static double exp2_c(double t) {
    double n = floor(t);
    double f = t - n;
    double p;
    __asm__ volatile ("f2xm1" : "=t"(p) : "0"(f));
    p += 1.0;
    return ldexp(p, (int)n);
}

static double log2_c(double x) {
    double r;
    if (x <= 0.0) return -1e300;
    __asm__ volatile ("fyl2x" : "=t"(r) : "0"(x), "u"(1.0) : "st(1)");
    return r;
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x <= 0.0) {
        if (x == 0.0) return 0.0;
        return 0.0;
    }
    return exp2_c(y * log2_c(x));
}

double sin(double x) {
    double r;
    __asm__ volatile ("fsin" : "=t"(r) : "0"(x));
    return r;
}
double cos(double x) {
    double r;
    __asm__ volatile ("fcos" : "=t"(r) : "0"(x));
    return r;
}
double tan(double x) {
    double r;
    __asm__ volatile ("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}
double atan2(double y, double x) {
    double r;
    __asm__ volatile ("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

float sqrtf(float x) { return (float)sqrt((double)x); }
float sinf(float x)  { return (float)sin((double)x); }
float cosf(float x)  { return (float)cos((double)x); }
float tanf(float x)  { return (float)tan((double)x); }
float powf(float x, float y) { return (float)pow((double)x, (double)y); }
float floorf(float x) { return (float)floor((double)x); }
float ceilf(float x)  { return (float)ceil((double)x); }
