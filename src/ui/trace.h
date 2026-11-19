#ifndef TRACE_H
#define TRACE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* the outlines' numbers, the same from every C library: like vsnprintf()
   for %d, %x, %s, %.2f and %.3f (a double rounded exactly, to even on a
   tie) and %g (three places, trailing zeros dropped) */
int trace_vfmt(char *out, size_t cap, const char *fmt, va_list ap);

/* SVG path data (malloc'd, even-odd) for the set pixels of a w x h mask,
   smoothed by a blur of sigma source pixels, simplified to tol */
char *trace_mask(const uint8_t *mask, int w, int h, double sigma, double tol);

/* the same by polygon fitting: straight edges stay straight, steps that
   stand for a curve become one; vertices turning more than alphamax (1 is
   usual) stay corners */
extern double trace_alphamax;
char *trace_poly(const uint8_t *mask, int w, int h);

#endif
