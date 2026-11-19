/*
    scalecmp: check that a shot of the interface at some scale shows the
    same pens as one at 100 percent, Toaster pixel for Toaster pixel.

    usage: scalecmp shot100.png shot.png percent x0 y0 x1 y1 [mean]

    Compares, inside the Toaster rectangle x0,y0 - x1,y1, each pixel of
    the first with every output pixel it covers in the second
    (cg_edge()). Exits 0 when all match.

    With mean (a number), the second is a drawing from the SVG redraws:
    each Toaster pixel's span of it is averaged and compared with the
    first's pixel; exits 0 when the mean difference per channel is under
    mean. Antialiasing moves a little; an icon misplaced or misscaled
    moves a lot.
*/

#include <math.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgrender.h"

static uint8_t *read_png(const char *path, int *w, int *h)
{
    png_image img;
    uint8_t *px;

    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path))
        return NULL;
    img.format = PNG_FORMAT_RGBA;
    if (!(px = malloc(PNG_IMAGE_SIZE(img))) || !png_image_finish_read(&img, NULL, px, 0, NULL))
    {
        free(px);
        return NULL;
    }
    *w = (int)img.width;
    *h = (int)img.height;
    return px;
}

int main(int argc, char **argv)
{
    int w0, h0, w1, h1, p, x0, y0, x1, y1, x, y, bad = 0;
    uint8_t *a, *b;

    if ((argc != 8 && argc != 9) || !(a = read_png(argv[1], &w0, &h0)) || !(b = read_png(argv[2], &w1, &h1)))
    {
        fprintf(stderr, "usage: scalecmp shot100.png shot.png percent x0 y0 x1 y1 [mean]\n");
        return 20;
    }
    p = atoi(argv[3]);
    x0 = atoi(argv[4]);
    y0 = atoi(argv[5]);
    x1 = atoi(argv[6]);
    y1 = atoi(argv[7]);
    if (w1 != cg_edge(w0, p) || h1 != cg_edge(h0, p))
    {
        printf("%s: %dx%d, not %dx%d\n", argv[2], w1, h1, cg_edge(w0, p), cg_edge(h0, p));
        return 10;
    }
    if (argc == 9)
    {
        double limit = atof(argv[8]), sum = 0;
        long cnt = 0;

        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++)
            {
                const uint8_t *s = a + ((size_t)y * w0 + x) * 4;
                double avg[3] = { 0, 0, 0 };
                int i, j, k, n = 0;

                for (j = cg_edge(y, p); j < cg_edge(y + 1, p); j++)
                    for (i = cg_edge(x, p); i < cg_edge(x + 1, p); i++, n++)
                        for (k = 0; k < 3; k++)
                            avg[k] += b[((size_t)j * w1 + i) * 4 + k];
                for (k = 0; k < 3 && n; k++)
                {
                    sum += fabs(avg[k] / n - s[k]);
                    cnt++;
                }
            }
        printf("%s at %d%%: mean difference per channel %.2f (limit %g)\n", argv[2], p, cnt ? sum / cnt : 0, limit);
        free(a);
        free(b);
        return cnt && sum / cnt < limit ? 0 : 1;
    }
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
        {
            const uint8_t *s = a + ((size_t)y * w0 + x) * 4;
            int i, j;

            for (j = cg_edge(y, p); j < cg_edge(y + 1, p); j++)
                for (i = cg_edge(x, p); i < cg_edge(x + 1, p); i++)
                    if (memcmp(s, b + ((size_t)j * w1 + i) * 4, 3) && bad++ < 5)
                        printf("%s: Toaster %d,%d is %02x%02x%02x, at %d,%d %02x%02x%02x\n", argv[2], x, y, s[0], s[1],
                               s[2], i, j, b[((size_t)j * w1 + i) * 4], b[((size_t)j * w1 + i) * 4 + 1],
                               b[((size_t)j * w1 + i) * 4 + 2]);
        }
    printf("%s at %d%%: %d pixels differ\n", argv[2], p, bad);
    free(a);
    free(b);
    return bad != 0;
}
