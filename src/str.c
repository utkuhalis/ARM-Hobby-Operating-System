#include "str.h"

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : 0;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int v, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)v;
    return dst;
}

static void emit_uint(put_fn put, uint64_t v, unsigned base, int upper, int width, char pad) {
    char buf[32];
    int len = 0;
    if (v == 0) {
        buf[len++] = '0';
    } else {
        while (v) {
            unsigned digit = v % base;
            buf[len++] = (digit < 10) ? ('0' + digit)
                                      : ((upper ? 'A' : 'a') + digit - 10);
            v /= base;
        }
    }
    while (len < width) buf[len++] = pad;
    while (len--) put(buf[len]);
}

static void emit_int(put_fn put, int64_t v, int width, char pad) {
    if (v < 0) {
        put('-');
        v = -v;
        if (width > 0) width--;
    }
    emit_uint(put, (uint64_t)v, 10, 0, width, pad);
}

void vprintf_(put_fn put, const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') {
            put(*fmt++);
            continue;
        }

        fmt++;
        char pad = ' ';
        int width = 0;
        int longflag = 0;
        int left = 0;

        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0' && !left) {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {
            longflag = 1;
            fmt++;
            if (*fmt == 'l') fmt++;
        }

        switch (*fmt) {
        case 'c':
            put((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t l = strlen(s);
            int padding = width - (int)l;
            if (left) {
                while (*s) put(*s++);
                while (padding-- > 0) put(' ');
            } else {
                while (padding-- > 0) put(' ');
                while (*s) put(*s++);
            }
            break;
        }
        case 'd':
        case 'i':
            if (longflag) {
                emit_int(put, va_arg(ap, int64_t), width, pad);
            } else {
                emit_int(put, va_arg(ap, int), width, pad);
            }
            break;
        case 'u':
            if (longflag) {
                emit_uint(put, va_arg(ap, uint64_t), 10, 0, width, pad);
            } else {
                emit_uint(put, va_arg(ap, unsigned int), 10, 0, width, pad);
            }
            break;
        case 'x':
        case 'X':
            if (longflag) {
                emit_uint(put, va_arg(ap, uint64_t), 16, *fmt == 'X', width, pad);
            } else {
                emit_uint(put, va_arg(ap, unsigned int), 16, *fmt == 'X', width, pad);
            }
            break;
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
            put('0');
            put('x');
            emit_uint(put, v, 16, 0, 16, '0');
            break;
        }
        case '%':
            put('%');
            break;
        default:
            put('%');
            put(*fmt);
            break;
        }
        if (*fmt) fmt++;
    }
}
