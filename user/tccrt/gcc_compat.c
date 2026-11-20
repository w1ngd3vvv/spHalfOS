/* 64-bit division helpers missing from TinyCC's libtcc1 on i386.
   GCC-compiled libc objects reference these; TCC-compiled programs may
   link against our libc.a, so provide them in libtcc1.a. */

unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d,
                                unsigned long long *rp)
{
    unsigned long long q = 0, r = 0;
    if (!d) {
        if (rp) *rp = 0;
        return 0;
    }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    if (rp) *rp = r;
    return q;
}

long long __divmoddi4(long long a, long long b, long long *rp)
{
    int negq = (a < 0) ^ (b < 0);
    int negr = a < 0;
    unsigned long long ua = a < 0 ? -(unsigned long long)a : (unsigned long long)a;
    unsigned long long ub = b < 0 ? -(unsigned long long)b : (unsigned long long)b;
    unsigned long long ur = 0;
    unsigned long long uq = __udivmoddi4(ua, ub, &ur);
    if (rp) *rp = negr ? -(long long)ur : (long long)ur;
    return negq ? -(long long)uq : (long long)uq;
}
