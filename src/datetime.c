/* Dates and times for lib/datetime.glow: datetime_native.*
 *
 * Calendar arithmetic is done here (days_from_civil / civil_from_days, from
 * Howard Hinnant's date algorithms) rather than with timegm/strptime, which
 * Windows lacks, so parsing and formatting behave identically everywhere.
 * A time is a Unix timestamp (seconds, with a fraction) plus the UTC offset
 * it is shown in; the local offset comes from the C library.
 */
#include "glowlang.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

static const char *month_names[] = { "January", "February", "March", "April", "May", "June", "July",
                                     "August", "September", "October", "November", "December" };
static const char *day_names[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

static Value dt_raise(const char *msg) {
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

/* Days since 1970-01-01 of a proleptic Gregorian date. */
static int64_t days_from_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

static int is_leap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in_month(int64_t y, int m) {
    static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && is_leap(y) ? 29 : dm[m - 1];
}

/* Seconds east of UTC that local time is at Unix time t. */
static int local_offset_at(double t) {
    time_t tt = (time_t)floor(t);
    struct tm lt;
#ifdef _WIN32
    if (localtime_s(&lt, &tt) != 0) {
        tt = 0;
        if (localtime_s(&lt, &tt) != 0) return 0;
    }
#else
    if (!localtime_r(&tt, &lt)) return 0;
#endif
    int64_t local = days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
                    lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return (int)(local - (int64_t)tt);
}

/* The Unix time of a wall-clock time; offset_set says whether `offset`
   (seconds east of UTC) applies, else local time is assumed. */
static double ts_from_fields(int64_t y, int mo, int d, int h, int mi, int s, double frac,
                             int offset_set, int offset) {
    double naive = (double)(days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s) + frac;
    if (offset_set) return naive - offset;
    /* Local: the offset depends on the instant, so settle it in two steps
       (exact except inside a daylight-saving jump). */
    double t = naive - local_offset_at(naive);
    return naive - local_offset_at(t);
}

typedef struct { int64_t year; int month, day, hour, minute, second, micro, weekday, yearday; } DtFields;

static void fields_of(double t, int offset, DtFields *f) {
    double local = t + offset;
    double whole = floor(local);
    int64_t secs = (int64_t)whole;
    f->micro = (int)floor((local - whole) * 1e6 + 0.5);
    if (f->micro >= 1000000) { f->micro -= 1000000; secs++; }
    int64_t days = secs >= 0 ? secs / 86400 : -((-secs + 86399) / 86400);
    int64_t rem = secs - days * 86400;
    civil_from_days(days, &f->year, &f->month, &f->day);
    f->hour = (int)(rem / 3600);
    f->minute = (int)(rem / 60 % 60);
    f->second = (int)(rem % 60);
    f->weekday = (int)(((days % 7) + 7 + 3) % 7);   /* 1970-01-01 was a Thursday; Monday is 0 */
    f->yearday = (int)(days - days_from_civil(f->year, 1, 1) + 1);
}

static Value ts_value(double t) {
    if (t == floor(t) && fabs(t) < 140737488355327.0) return value_int((int64_t)t);
    return value_float(t);
}

static int num_arg(Value v, double *out) {
    if (GET_TYPE(v) == VAL_INT) { *out = (double)AS_INT(v); return 1; }
    if (GET_TYPE(v) == VAL_FLOAT) { *out = AS_FLOAT(v); return 1; }
    return 0;
}

static int int_arg(Value v, int64_t *out) {
    if (GET_TYPE(v) == VAL_INT) { *out = AS_INT(v); return 1; }
    if (GET_TYPE(v) == VAL_FLOAT && AS_FLOAT(v) == floor(AS_FLOAT(v))) { *out = (int64_t)AS_FLOAT(v); return 1; }
    return 0;
}

/* datetime_native.now(): the current Unix time, with a fraction. */
static Value dn_now(int argc, Value *args) {
    (void)argc; (void)args;
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return value_int((int64_t)time(NULL));
    return value_float((double)ts.tv_sec + ts.tv_nsec / 1e9);
}

/* datetime_native.local_offset(t): seconds east of UTC of local time at t. */
static Value dn_local_offset(int argc, Value *args) {
    double t;
    if (argc < 1 || !num_arg(args[0], &t)) return dt_raise("datetime: local_offset needs a timestamp");
    return value_int(local_offset_at(t));
}

/* datetime_native.fields(t, offset): [year, month, day, hour, minute,
   second, microsecond, weekday (Monday 0), yearday]. */
static Value dn_fields(int argc, Value *args) {
    double t; int64_t off;
    if (argc < 2 || !num_arg(args[0], &t) || !int_arg(args[1], &off)) return dt_raise("datetime: fields needs a timestamp and an offset");
    DtFields f;
    fields_of(t, (int)off, &f);
    List *l = list_new(9);
    list_push(l, value_int(f.year)); list_push(l, value_int(f.month)); list_push(l, value_int(f.day));
    list_push(l, value_int(f.hour)); list_push(l, value_int(f.minute)); list_push(l, value_int(f.second));
    list_push(l, value_int(f.micro)); list_push(l, value_int(f.weekday)); list_push(l, value_int(f.yearday));
    return value_list(l);
}

/* datetime_native.make(year, month, day, hour, minute, second, micro,
   offset or none for local): the Unix time. Fields out of range raise. */
static Value dn_make(int argc, Value *args) {
    int64_t v[7];
    static const char *names[] = { "year", "month", "day", "hour", "minute", "second", "microsecond" };
    if (argc < 8) return dt_raise("datetime: make needs 8 arguments");
    for (int i = 0; i < 7; i++) {
        if (!int_arg(args[i], &v[i])) {
            char m[80]; snprintf(m, sizeof(m), "datetime: the %s must be a whole number", names[i]);
            return dt_raise(m);
        }
    }
    if (v[0] < -9999 || v[0] > 9999) return dt_raise("datetime: year out of range (-9999 to 9999)");
    if (v[1] < 1 || v[1] > 12) return dt_raise("datetime: month must be 1 to 12");
    if (v[2] < 1 || v[2] > days_in_month(v[0], (int)v[1])) {
        char m[80]; snprintf(m, sizeof(m), "datetime: day must be 1 to %d for that month", days_in_month(v[0], (int)v[1]));
        return dt_raise(m);
    }
    if (v[3] < 0 || v[3] > 23) return dt_raise("datetime: hour must be 0 to 23");
    if (v[4] < 0 || v[4] > 59) return dt_raise("datetime: minute must be 0 to 59");
    if (v[5] < 0 || v[5] > 59) return dt_raise("datetime: second must be 0 to 59");
    if (v[6] < 0 || v[6] > 999999) return dt_raise("datetime: microsecond must be 0 to 999999");
    int64_t off = 0;
    int has_off = GET_TYPE(args[7]) != VAL_NONE;
    if (has_off && (!int_arg(args[7], &off) || off < -86400 || off > 86400)) return dt_raise("datetime: the offset must be seconds within a day");
    return ts_value(ts_from_fields(v[0], (int)v[1], (int)v[2], (int)v[3], (int)v[4], (int)v[5], v[6] / 1e6, has_off, (int)off));
}

/* datetime_native.days_in_month(year, month) */
static Value dn_days_in_month(int argc, Value *args) {
    int64_t y, m;
    if (argc < 2 || !int_arg(args[0], &y) || !int_arg(args[1], &m) || m < 1 || m > 12) return dt_raise("datetime: days_in_month needs a year and a month 1 to 12");
    return value_int(days_in_month(y, (int)m));
}

typedef struct { char *p; size_t n, cap; } DtBuf;
static void db_add(DtBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}
static void db_fmt(DtBuf *b, const char *fmt, long long v) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), fmt, v);
    db_add(b, tmp, (size_t)n);
}
static void db_offset(DtBuf *b, int off, int colon) {
    char tmp[16];
    int a = off < 0 ? -off : off;
    int n = snprintf(tmp, sizeof(tmp), colon ? "%c%02d:%02d" : "%c%02d%02d", off < 0 ? '-' : '+', a / 3600, a / 60 % 60);
    db_add(b, tmp, (size_t)n);
}

/* datetime_native.format(t, offset, fmt): strftime-style codes, the same on
   every platform. An unknown code raises instead of printing garbage. */
static Value dn_format(int argc, Value *args) {
    double t; int64_t off;
    if (argc < 3 || !num_arg(args[0], &t) || !int_arg(args[1], &off) || GET_TYPE(args[2]) != VAL_STRING)
        return dt_raise("datetime: format needs a timestamp, an offset and a format string");
    DtFields f;
    fields_of(t, (int)off, &f);
    DtBuf b = { NULL, 0, 0 };
    for (const char *p = AS_STRING(args[2]); *p; p++) {
        if (*p != '%') { db_add(&b, p, 1); continue; }
        p++;
        int hour12 = f.hour % 12 == 0 ? 12 : f.hour % 12;
        switch (*p) {
            case 'Y': db_fmt(&b, f.year < 0 ? "%05lld" : "%04lld", f.year); break;
            case 'y': db_fmt(&b, "%02lld", ((f.year % 100) + 100) % 100); break;
            case 'm': db_fmt(&b, "%02lld", f.month); break;
            case 'd': db_fmt(&b, "%02lld", f.day); break;
            case 'e': db_fmt(&b, "%2lld", f.day); break;
            case 'H': db_fmt(&b, "%02lld", f.hour); break;
            case 'I': db_fmt(&b, "%02lld", hour12); break;
            case 'M': db_fmt(&b, "%02lld", f.minute); break;
            case 'S': db_fmt(&b, "%02lld", f.second); break;
            case 'f': db_fmt(&b, "%06lld", f.micro); break;
            case 'p': db_add(&b, f.hour < 12 ? "AM" : "PM", 2); break;
            case 'B': db_add(&b, month_names[f.month - 1], strlen(month_names[f.month - 1])); break;
            case 'b': db_add(&b, month_names[f.month - 1], 3); break;
            case 'A': db_add(&b, day_names[f.weekday], strlen(day_names[f.weekday])); break;
            case 'a': db_add(&b, day_names[f.weekday], 3); break;
            case 'j': db_fmt(&b, "%03lld", f.yearday); break;
            case 'u': db_fmt(&b, "%lld", f.weekday + 1); break;
            case 'w': db_fmt(&b, "%lld", (f.weekday + 1) % 7); break;
            case 'z': db_offset(&b, (int)off, 0); break;
            case 'Z': if (off == 0) db_add(&b, "UTC", 3); else db_offset(&b, (int)off, 1); break;
            case 'F': db_fmt(&b, "%04lld", f.year); db_fmt(&b, "-%02lld", f.month); db_fmt(&b, "-%02lld", f.day); break;
            case 'T': db_fmt(&b, "%02lld", f.hour); db_fmt(&b, ":%02lld", f.minute); db_fmt(&b, ":%02lld", f.second); break;
            case 's': db_fmt(&b, "%lld", (long long)floor(t)); break;
            case '%': db_add(&b, "%", 1); break;
            default: {
                free(b.p);
                char m[80];
                if (*p) snprintf(m, sizeof(m), "datetime: unknown format code %%%c", *p);
                else snprintf(m, sizeof(m), "datetime: the format ends with a lone %%");
                return dt_raise(m);
            }
        }
    }
    if (b.p) b.p[b.n] = '\0';
    Value out = value_string_copy(b.p ? b.p : "");
    free(b.p);
    return out;
}

/* Reads 1..max digits; returns how many were read. */
static int read_digits(const char **s, int min, int max, int64_t *out) {
    int n = 0;
    int64_t v = 0;
    while (n < max && isdigit((unsigned char)**s)) { v = v * 10 + (**s - '0'); (*s)++; n++; }
    if (n < min) return 0;
    *out = v;
    return n;
}

/* Matches a month or day name (full or its first three letters). */
static int read_name(const char **s, const char **names, int count) {
    for (int i = 0; i < count; i++) {
        size_t full = strlen(names[i]);
        size_t lens[2] = { full, 3 };
        for (int k = 0; k < 2; k++) {
            size_t n = lens[k];
            size_t j = 0;
            while (j < n && (*s)[j] && tolower((unsigned char)(*s)[j]) == tolower((unsigned char)names[i][j])) j++;
            if (j == n) { *s += n; return i; }
        }
    }
    return -1;
}

/* +HH:MM, +HHMM, +HH or Z; returns 0 if s does not start with one. */
static int read_offset(const char **s, int *off) {
    const char *p = *s;
    if (*p == 'Z' || *p == 'z') { *off = 0; *s = p + 1; return 1; }
    if (*p != '+' && *p != '-') return 0;
    int sign = *p == '-' ? -1 : 1;
    p++;
    int64_t h, m = 0;
    if (read_digits(&p, 2, 2, &h) != 2) return 0;
    if (*p == ':') { p++; if (read_digits(&p, 2, 2, &m) != 2) return 0; }
    else if (isdigit((unsigned char)*p)) { if (read_digits(&p, 2, 2, &m) != 2) return 0; }
    if (h > 23 || m > 59) return 0;
    *off = sign * (int)(h * 3600 + m * 60);
    *s = p;
    return 1;
}

typedef struct {
    int64_t year; int month, day, hour, minute, second; double frac;
    int has_off, off, pm, has_pm, clock12, yearday;
} DtParsed;

static Value parse_fail(const char *text, const char *why) {
    char m[240];
    snprintf(m, sizeof(m), "datetime.parse: cannot read \"%.80s\": %s", text, why);
    return dt_raise(m);
}

/* ISO 8601: YYYY-MM-DD, then optionally T or a space and HH:MM[:SS[.fff]],
   then optionally Z or an offset. */
static const char *parse_iso(const char *s, DtParsed *r) {
    int64_t v;
    if (!read_digits(&s, 4, 4, &r->year) || *s++ != '-') return "expected YYYY-MM-DD";
    if (read_digits(&s, 2, 2, &v) != 2 || *s++ != '-') return "expected YYYY-MM-DD";
    r->month = (int)v;
    if (read_digits(&s, 2, 2, &v) != 2) return "expected YYYY-MM-DD";
    r->day = (int)v;
    if (*s == 'T' || *s == 't' || (*s == ' ' && isdigit((unsigned char)s[1]))) {
        s++;
        if (read_digits(&s, 2, 2, &v) != 2 || *s++ != ':') return "expected HH:MM after the date";
        r->hour = (int)v;
        if (read_digits(&s, 2, 2, &v) != 2) return "expected HH:MM after the date";
        r->minute = (int)v;
        if (*s == ':') {
            s++;
            if (read_digits(&s, 2, 2, &v) != 2) return "expected two digits of seconds";
            r->second = (int)v;
            if (*s == '.' || *s == ',') {
                s++;
                double scale = 0.1;
                if (!isdigit((unsigned char)*s)) return "expected digits after the decimal point";
                while (isdigit((unsigned char)*s)) { r->frac += (*s - '0') * scale; scale /= 10; s++; }
            }
        }
        if (*s && read_offset(&s, &r->off)) r->has_off = 1;
    }
    while (isspace((unsigned char)*s)) s++;
    if (*s) return "unexpected text after the date";
    return NULL;
}

/* strptime-style: %Y %y %m %d %e %H %I %M %S %f %p %b %B %a %A %j %z %Z
   %F %T %%; a space matches any run of whitespace; other characters
   match themselves. */
static const char *parse_fmt(const char *s, const char *fmt, DtParsed *r) {
    int64_t v;
    for (const char *f = fmt; *f; f++) {
        if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*s)) s++; continue; }
        if (*f != '%') { if (*s != *f) return "the text does not match the format"; s++; continue; }
        f++;
        switch (*f) {
            case 'Y': { int neg = *s == '-'; if (neg) s++; if (!read_digits(&s, 1, 4, &v)) return "expected a year (%Y)"; r->year = neg ? -v : v; break; }
            case 'y': if (read_digits(&s, 2, 2, &v) != 2) return "expected a two-digit year (%y)"; r->year = v < 69 ? 2000 + v : 1900 + v; break;
            case 'm': if (!read_digits(&s, 1, 2, &v)) return "expected a month (%m)"; r->month = (int)v; break;
            case 'd': case 'e': while (*f == 'e' && *s == ' ') s++; if (!read_digits(&s, 1, 2, &v)) return "expected a day (%d)"; r->day = (int)v; break;
            case 'H': if (!read_digits(&s, 1, 2, &v)) return "expected an hour (%H)"; r->hour = (int)v; break;
            case 'I': if (!read_digits(&s, 1, 2, &v) || v < 1 || v > 12) return "expected an hour 1 to 12 (%I)"; r->hour = (int)v; r->clock12 = 1; break;
            case 'M': if (!read_digits(&s, 1, 2, &v)) return "expected minutes (%M)"; r->minute = (int)v; break;
            case 'S': if (!read_digits(&s, 1, 2, &v)) return "expected seconds (%S)"; r->second = (int)v; break;
            case 'f': { double scale = 0.1; if (!isdigit((unsigned char)*s)) return "expected a fraction of a second (%f)";
                        while (isdigit((unsigned char)*s)) { r->frac += (*s - '0') * scale; scale /= 10; s++; } break; }
            case 'p': {
                char a = (char)tolower((unsigned char)s[0]), b = (char)tolower((unsigned char)s[1]);
                if ((a != 'a' && a != 'p') || b != 'm') return "expected AM or PM (%p)";
                r->pm = a == 'p'; r->has_pm = 1; s += 2; break;
            }
            case 'b': case 'B': { int m = read_name(&s, month_names, 12); if (m < 0) return "expected a month name (%b)"; r->month = m + 1; break; }
            case 'a': case 'A': if (read_name(&s, day_names, 7) < 0) return "expected a day name (%a)"; break;
            case 'j': if (!read_digits(&s, 1, 3, &v) || v < 1 || v > 366) return "expected a day of the year (%j)"; r->yearday = (int)v; break;
            case 'z': case 'Z': if (!read_offset(&s, &r->off)) return "expected Z or an offset like +05:30 (%z)"; r->has_off = 1; break;
            case 'F': if (!read_digits(&s, 4, 4, &r->year)) return "expected YYYY-MM-DD (%F)";
                      if (*s++ != '-' || !read_digits(&s, 1, 2, &v)) return "expected YYYY-MM-DD (%F)"; r->month = (int)v;
                      if (*s++ != '-' || !read_digits(&s, 1, 2, &v)) return "expected YYYY-MM-DD (%F)"; r->day = (int)v; break;
            case 'T': if (!read_digits(&s, 1, 2, &v)) return "expected HH:MM:SS (%T)"; r->hour = (int)v;
                      if (*s++ != ':' || !read_digits(&s, 1, 2, &v)) return "expected HH:MM:SS (%T)"; r->minute = (int)v;
                      if (*s++ != ':' || !read_digits(&s, 1, 2, &v)) return "expected HH:MM:SS (%T)"; r->second = (int)v; break;
            case '%': if (*s++ != '%') return "expected %"; break;
            default: return "unknown code in the format";
        }
    }
    if (*s) return "unconverted text remains at the end";
    return NULL;
}

/* datetime_native.parse(text, format or none): [timestamp, offset or none]
   where a text without a zone is read as local time (offset none). */
static Value dn_parse(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return dt_raise("datetime.parse needs a string");
    const char *text = AS_STRING(args[0]);
    DtParsed r;
    memset(&r, 0, sizeof(r));
    r.year = 1900; r.month = 1; r.day = 1;
    const char *err;
    const char *start = text;
    while (isspace((unsigned char)*start)) start++;
    if (argc > 1 && GET_TYPE(args[1]) == VAL_STRING) err = parse_fmt(start, AS_STRING(args[1]), &r);
    else if (argc > 1 && GET_TYPE(args[1]) != VAL_NONE) return dt_raise("datetime.parse: the format must be a string");
    else err = parse_iso(start, &r);
    if (err) return parse_fail(text, err);
    /* %p turns a 12-hour %I into 24-hour time: 12 AM is 0, 1 PM is 13. */
    if (r.has_pm && r.clock12) {
        if (r.hour == 12) r.hour = r.pm ? 12 : 0;
        else if (r.pm) r.hour += 12;
    }
    if (r.yearday) {
        if (r.yearday > (is_leap(r.year) ? 366 : 365)) return parse_fail(text, "day of the year out of range");
        int64_t days = days_from_civil(r.year, 1, 1) + r.yearday - 1;
        int64_t y; civil_from_days(days, &y, &r.month, &r.day);
    }
    if (r.month < 1 || r.month > 12) return parse_fail(text, "month out of range");
    if (r.day < 1 || r.day > days_in_month(r.year, r.month)) return parse_fail(text, "day out of range for that month");
    if (r.hour > 23) return parse_fail(text, "hour out of range");
    if (r.minute > 59) return parse_fail(text, "minute out of range");
    if (r.second > 59) return parse_fail(text, "second out of range");
    double t = ts_from_fields(r.year, r.month, r.day, r.hour, r.minute, r.second, r.frac, r.has_off, r.off);
    List *out = list_new(2);
    list_push(out, ts_value(t));
    list_push(out, r.has_off ? value_int(r.off) : value_none());
    return value_list(out);
}

void glow_datetime_init(void) {
    Dict *d = dict_new(8);
    dict_set(d, "now", native_func_new("now", dn_now));
    dict_set(d, "local_offset", native_func_new("local_offset", dn_local_offset));
    dict_set(d, "fields", native_func_new("fields", dn_fields));
    dict_set(d, "make", native_func_new("make", dn_make));
    dict_set(d, "format", native_func_new("format", dn_format));
    dict_set(d, "parse", native_func_new("parse", dn_parse));
    dict_set(d, "days_in_month", native_func_new("days_in_month", dn_days_in_month));
    var_set("datetime_native", value_dict(d));
}
