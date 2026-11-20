#include <time.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <syscall.h>

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *py, int *pm, int *pd) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    *py = y; *pm = (int)m; *pd = (int)d;
}

static struct tm tm_buf;

struct tm *gmtime_r(const time_t *tp, struct tm *r) {
    if (!tp || !r) return NULL;
    int64_t t = *tp;
    int64_t days = t / 86400;
    int64_t secs = t % 86400;
    if (secs < 0) { secs += 86400; days--; }
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    r->tm_year = y - 1900;
    r->tm_mon = m - 1;
    r->tm_mday = d;
    r->tm_hour = (int)(secs / 3600);
    r->tm_min = (int)((secs % 3600) / 60);
    r->tm_sec = (int)(secs % 60);
    r->tm_wday = (int)((days + 4) % 7 + 7) % 7;
    r->tm_yday = 0;
    r->tm_isdst = 0;
    return r;
}

struct tm *gmtime(const time_t *tp) { return gmtime_r(tp, &tm_buf); }
struct tm *localtime_r(const time_t *tp, struct tm *r) { return gmtime_r(tp, r); }
struct tm *localtime(const time_t *tp) { return gmtime_r(tp, &tm_buf); }

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int64_t days = days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    return (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}

char *asctime(const struct tm *tm) {
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static char buf[32];
    if (!tm) return NULL;
    snprintf(buf, sizeof(buf), "%s %s %2d %02d:%02d:%02d %d\n",
             wd[(tm->tm_wday % 7 + 7) % 7], mo[(tm->tm_mon % 12 + 12) % 12],
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
    return buf;
}

char *ctime(const time_t *tp) {
    struct tm t;
    if (!localtime_r(tp, &t)) return NULL;
    return asctime(&t);
}

double difftime(time_t a, time_t b) { return (double)((long)a - (long)b); }

size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm) {
    size_t pos = 0;
    if (!buf || !max || !fmt || !tm) return 0;
    while (*fmt) {
        char tmp[32];
        int n = 0;
        if (*fmt != '%') { tmp[n++] = *fmt++; }
        else {
            fmt++;
            switch (*fmt++) {
            case 'Y': n = snprintf(tmp, sizeof(tmp), "%d", tm->tm_year + 1900); break;
            case 'm': n = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1); break;
            case 'd': n = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday); break;
            case 'H': n = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour); break;
            case 'M': n = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min); break;
            case 'S': n = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec); break;
            case 's': n = snprintf(tmp, sizeof(tmp), "%d", (int)mktime((struct tm *)tm)); break;
            case '%': tmp[n++] = '%'; break;
            default: tmp[n++] = '?'; break;
            }
        }
        for (int i = 0; i < n; i++) {
            if (pos + 1 < max) buf[pos++] = tmp[i];
        }
    }
    buf[pos] = 0;
    return pos;
}

time_t time(time_t *t) {
    time_t v = (time_t)syscall0(SYS_TIME);
    if (t) *t = v;
    return v;
}

clock_t clock(void) { return (clock_t)syscall0(SYS_TICKS) * (CLOCKS_PER_SEC / 60); }
