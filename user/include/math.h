#ifndef _MATH_H
#define _MATH_H
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define M_PI 3.14159265358979323846
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
double fabs(double x);
float fabsf(float x);
double ldexp(double x, int e);
float ldexpf(float x, int e);
double frexp(double x, int *e);
float frexpf(float x, int *e);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double sqrt(double x);
double pow(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double atan2(double y, double x);
float sqrtf(float x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float powf(float x, float y);
float floorf(float x);
float ceilf(float x);
#endif
