/* Arbitrary-precision signed integers (numeric tower stage 2).
   Little-endian base-2^32 limbs, stored as a heap GC object. */
#include "glowlang.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static BigInt* bigint_alloc(int nlimbs, int neg) {
    if (nlimbs < 1) nlimbs = 1;
    BigInt *b = (BigInt*)gc_alloc(sizeof(BigInt) + (size_t)nlimbs * sizeof(uint32_t), OBJ_BIGINT);
    b->neg = neg ? 1 : 0;
    b->nlimbs = nlimbs;
    for (int i = 0; i < nlimbs; i++) b->limbs[i] = 0;
    return b;
}

static void bigint_normalize(BigInt *b) {
    int i = b->nlimbs - 1;
    while (i > 0 && b->limbs[i] == 0) i--;
    b->nlimbs = i + 1;
    if (b->nlimbs == 1 && b->limbs[0] == 0) b->neg = 0;
}

BigInt* bigint_from_int64(int64_t v) {
    if (v == 0) { BigInt *b = bigint_alloc(1, 0); b->limbs[0] = 0; return b; }
    int neg = v < 0;
    /* The magnitude, computed in unsigned arithmetic so INT64_MIN is safe.
       (It used to take the two's-complement bits as the magnitude, so every
       negative big integer built from an int64 was wrong.) */
    uint64_t u = neg ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    uint32_t limbs[2];
    limbs[0] = (uint32_t)(u & 0xFFFFFFFFu);
    limbs[1] = (uint32_t)(u >> 32);
    int n = limbs[1] ? 2 : 1;
    BigInt *b = bigint_alloc(n, neg);
    for (int i = 0; i < n; i++) b->limbs[i] = limbs[i];
    bigint_normalize(b);
    return b;
}

BigInt* bigint_from_string(const char *s) {
    while (*s == ' ' || *s == '+' || *s == '-') { if (*s == '-') break; s++; }
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    BigInt *b = bigint_alloc(1, neg);
    b->limbs[0] = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') break;
        int d = *s - '0';
        uint64_t carry = (uint64_t)d;
        for (int i = 0; i < b->nlimbs; i++) {
            uint64_t cur = (uint64_t)b->limbs[i] * 10u + carry;
            b->limbs[i] = (uint32_t)(cur & 0xFFFFFFFFu);
            carry = cur >> 32;
        }
        if (carry) {
            int n = b->nlimbs + 1;
            BigInt *nb = bigint_alloc(n, neg);
            for (int i = 0; i < b->nlimbs; i++) nb->limbs[i] = b->limbs[i];
            nb->limbs[b->nlimbs] = (uint32_t)carry;
            b = nb;
        }
    }
    bigint_normalize(b);
    return b;
}

int bigint_cmp(BigInt *a, BigInt *b) {
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    int r = 0;
    int n = a->nlimbs > b->nlimbs ? a->nlimbs : b->nlimbs;
    for (int i = n - 1; i >= 0; i--) {
        uint32_t av = i < a->nlimbs ? a->limbs[i] : 0;
        uint32_t bv = i < b->nlimbs ? b->limbs[i] : 0;
        if (av != bv) { r = av > bv ? 1 : -1; break; }
    }
    return a->neg ? -r : r;
}

static BigInt* bi_uadd(const BigInt *a, const BigInt *b, int neg) {
    int n = (a->nlimbs > b->nlimbs ? a->nlimbs : b->nlimbs) + 1;
    BigInt *r = bigint_alloc(n, neg);
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t av = i < a->nlimbs ? a->limbs[i] : 0;
        uint64_t bv = i < b->nlimbs ? b->limbs[i] : 0;
        uint64_t s = av + bv + carry;
        r->limbs[i] = (uint32_t)(s & 0xFFFFFFFFu);
        carry = s >> 32;
    }
    bigint_normalize(r);
    return r;
}

static BigInt* bi_usub(const BigInt *a, const BigInt *b, int neg) {
    int n = a->nlimbs;
    BigInt *r = bigint_alloc(n, neg);
    int64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        int64_t d = (int64_t)a->limbs[i] - (int64_t)(i < b->nlimbs ? b->limbs[i] : 0) - borrow;
        if (d < 0) { d += 0x100000000LL; borrow = 1; } else borrow = 0;
        r->limbs[i] = (uint32_t)d;
    }
    bigint_normalize(r);
    return r;
}

/* Compare magnitudes |a| and |b|. */
static int bi_ucmp(const BigInt *a, const BigInt *b) {
    int n = a->nlimbs > b->nlimbs ? a->nlimbs : b->nlimbs;
    for (int i = n - 1; i >= 0; i--) {
        uint32_t av = i < a->nlimbs ? a->limbs[i] : 0;
        uint32_t bv = i < b->nlimbs ? b->limbs[i] : 0;
        if (av != bv) return av > bv ? 1 : -1;
    }
    return 0;
}

/* With opposite signs (add) or equal signs (sub) the result is the larger
   magnitude minus the smaller, which needs a magnitude comparison: a signed
   one got e.g. (-5) - (-3) and 5 + (-7) wrong. */
BigInt* bigint_add(BigInt *a, BigInt *b) {
    if (a->neg == b->neg) return bi_uadd(a, b, a->neg);
    if (bi_ucmp(a, b) >= 0) return bi_usub(a, b, a->neg);
    return bi_usub(b, a, b->neg);
}

BigInt* bigint_sub(BigInt *a, BigInt *b) {
    if (a->neg != b->neg) return bi_uadd(a, b, a->neg);
    if (bi_ucmp(a, b) >= 0) return bi_usub(a, b, a->neg);
    return bi_usub(b, a, !a->neg);
}

BigInt* bigint_negate(BigInt *a) {
    BigInt *r = bigint_alloc(a->nlimbs, !a->neg);
    for (int i = 0; i < a->nlimbs; i++) r->limbs[i] = a->limbs[i];
    bigint_normalize(r);
    return r;
}

BigInt* bigint_mul(BigInt *a, BigInt *b) {
    int n = a->nlimbs + b->nlimbs;
    BigInt *r = bigint_alloc(n, a->neg ^ b->neg);
    for (int i = 0; i < a->nlimbs; i++) {
        uint64_t ai = a->limbs[i];
        uint64_t carry = 0;
        for (int j = 0; j < b->nlimbs; j++) {
            uint64_t cur = (uint64_t)r->limbs[i + j] + ai * (uint64_t)b->limbs[j] + carry;
            r->limbs[i + j] = (uint32_t)(cur & 0xFFFFFFFFu);
            carry = cur >> 32;
        }
        uint64_t c = carry;
        int k = i + b->nlimbs;
        while (c) {
            uint64_t cur = (uint64_t)r->limbs[k] + c;
            r->limbs[k] = (uint32_t)(cur & 0xFFFFFFFFu);
            c = cur >> 32;
            k++;
        }
    }
    bigint_normalize(r);
    if (r->nlimbs == 1 && r->limbs[0] == 0) r->neg = 0;
    return r;
}

int bigint_fits_int64(BigInt *b, int64_t *out) {
    if (b->nlimbs > 2) return 0;
    uint64_t u = 0;
    if (b->nlimbs >= 1) u |= (uint64_t)b->limbs[0];
    if (b->nlimbs >= 2) u |= (uint64_t)b->limbs[1] << 32;
    if (b->neg) {
        if (u > 0x8000000000000000ULL) return 0;
        *out = (u == 0x8000000000000000ULL) ? INT64_MIN : -(int64_t)u;
    } else {
        if (u > 0x7FFFFFFFFFFFFFFFULL) return 0;
        *out = (int64_t)u;
    }
    return 1;
}

double bigint_to_double(BigInt *b) {
    int64_t i;
    if (bigint_fits_int64(b, &i)) return (double)i;
    char *s = bigint_to_new_string(b);
    double d = strtod(s, NULL);
    free(s);
    return d;
}

char* bigint_to_new_string(BigInt *b) {
    if (b->nlimbs == 1 && b->limbs[0] == 0) {
        char *s = (char*)malloc(2);
        s[0] = '0'; s[1] = '\0';
        return s;
    }
    /* Worst-case decimal length is nlimbs*10 (2^32 < 10^10). */
    size_t cap = (size_t)b->nlimbs * 10 + 2;
    char *buf = (char*)malloc(cap + 1);
    uint32_t *w = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)b->nlimbs);
    int wl = b->nlimbs;
    for (int i = 0; i < wl; i++) w[i] = b->limbs[i];
    int tlen = 0;
    while (!(wl == 1 && w[0] == 0)) {
        uint64_t rem = 0;
        for (int i = wl - 1; i >= 0; i--) {
            uint64_t cur = (rem << 32) | w[i];
            w[i] = (uint32_t)(cur / 10u);
            rem = cur % 10u;
        }
        buf[tlen++] = (char)('0' + (int)rem);
        while (wl > 1 && w[wl - 1] == 0) wl--;
    }
    free(w);
    for (int i = 0, j = tlen - 1; i < j; i++, j--) {
        char c = buf[i]; buf[i] = buf[j]; buf[j] = c;
    }
    if (b->neg) {
        memmove(buf + 1, buf, (size_t)tlen);
        buf[0] = '-';
        tlen++;
    }
    buf[tlen] = '\0';
    return buf;
}
