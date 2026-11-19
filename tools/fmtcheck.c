/*
    fmtcheck: the tracer's own number formatting (trace_vfmt) against the
    C library's printf, which on the host rounds exactly: halves, values
    either side of them and a run of pseudo-random ones for %.2f and
    %.3f, and for %g the halves it is used for. Prints what differs;
    exit status 1 if anything does.
    usage: fmtcheck
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "trace.h"

static int bad, done;

static void ours(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    trace_vfmt(out, cap, fmt, ap);
    va_end(ap);
}

static void one(const char *fmt, double v)
{
    char a[64], b[64];

    ours(a, sizeof(a), fmt, v);
    snprintf(b, sizeof(b), fmt, v);
    done++;
    if (strcmp(a, b))
    {
        if (bad++ < 20)
            printf("%s of %.17g: %s, the C library %s\n", fmt, v, a, b);
    }
}

int main(void)
{
    uint32_t r = 12345;
    char     a[64], b[64];
    int      i, k;

    for (i = -4000; i <= 4000; i++)
        for (k = -1; k <= 1; k++)
        {
            double v = nextafter(i / 8.0, k * 1e9), w = nextafter(i / 2000.0, k * 1e9);

            one("%.2f", v);
            one("%.3f", v);
            one("%.2f", w);
            one("%.3f", w);
        }
    for (i = 0; i < 2000000; i++)
    {
        double v;

        r = r * 1664525u + 1013904223u;
        v = ((int32_t)r) / 1048576.0;
        r = r * 1664525u + 1013904223u;
        v += r / 4294967296.0 / 1048576.0;
        one("%.2f", v);
        one("%.3f", v);
    }
    one("%.2f", 0.0);
    one("%.2f", -0.0);
    one("%.2f", -0.001);
    one("%.2f", 0.995);
    one("%.2f", 9.996);
    one("%.3f", 1e-20);
    for (i = -1998; i <= 1998; i++)
        one("%g", i / 2.0);
    ours(a, sizeof(a), "<%06x|%d|%d|%s|%x>", 0xabcu, -17, 2048, "text", 0x123456u);
    snprintf(b, sizeof(b), "<%06x|%d|%d|%s|%x>", 0xabcu, -17, 2048, "text", 0x123456u);
    done++;
    if (strcmp(a, b))
    {
        bad++;
        printf("%s, the C library %s\n", a, b);
    }
    ours(a, 8, "%.2f %.2f", 123.456, 7.0);
    done++;
    if (strcmp(a, "123.46 "))
    {
        bad++;
        printf("cut short: \"%s\"\n", a);
    }
    printf("%d of %d differ\n", bad, done);
    return bad != 0;
}
