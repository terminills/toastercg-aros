/*
    The CG's interface imagery as SVG, redrawn by cgtrace.c from the
    original pixels (cgart.h): one entry per gadget image and state, and
    the CGInterface font's characters as outlines. cg_art_load() sets
    them up.
*/

#ifndef CGICONS_H
#define CGICONS_H

struct cg_icon
{
    int         gadget;     /* index in cg_art_gadgets */
    int         select;     /* the select (pressed) image */
    int         w, h;       /* the original image's size */
    const char *svg;
};

extern const struct cg_icon *cg_icons;
extern int                   cg_nicons;

/* per character lo..hi and the default one: SVG path data in the
   character's cell (its width, the font's height), even-odd */
extern char **cg_vfont;

/* redraws the art in cgart.h; note() is told how many of how many images
   are done. 0 when memory runs out */
int  cg_trace(struct cg_icon **icons, int *nicons, char ***vfont, void (*note)(void *, int, int), void *ud);
void cg_trace_free(struct cg_icon *icons, int nicons, char **vfont, int nvfont);

#endif
