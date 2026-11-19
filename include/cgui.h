/*
    The CG's interface: the page being edited with the menu bar over it,
    NewTek's imagery and layout scaled by a percentage. Platform
    neutral: the program feeds it Amiga raw keys and mouse events and
    shows the RGBA it draws.
*/

#ifndef CGUI_H
#define CGUI_H

#include "cgcmd.h"

/* what cg_ui_key/cg_ui_mouse ask of the program */
#define CG_UI_REDRAW 1
#define CG_UI_QUIT   2
#define CG_UI_TAKE   4      /* put the current page out (and play it) */

#define CG_QUAL_SHIFT  0x01
#define CG_QUAL_ALT    0x02
#define CG_QUAL_CTRL   0x04
#define CG_QUAL_LAMIGA 0x08
#define CG_QUAL_RAMIGA 0x10

enum
{
    CG_MOUSE_DOWN,
    CG_MOUSE_UP,
    CG_MOUSE_MOVE,
    CG_MOUSE_RDOWN
};

/* Amiga raw key codes the CG reacts to */
#define CG_RAW_BACKSPACE 0x41
#define CG_RAW_TAB       0x42
#define CG_RAW_ENTER     0x43
#define CG_RAW_RETURN    0x44
#define CG_RAW_ESC       0x45
#define CG_RAW_DEL       0x46
#define CG_RAW_PAGEUP    0x48
#define CG_RAW_PAGEDOWN  0x49
#define CG_RAW_KP_MINUS  0x4a
#define CG_RAW_UP        0x4c
#define CG_RAW_DOWN      0x4d
#define CG_RAW_RIGHT     0x4e
#define CG_RAW_LEFT      0x4f
#define CG_RAW_F1        0x50
#define CG_RAW_F10       0x59
#define CG_RAW_KP_PLUS   0x5e
#define CG_RAW_HELP      0x5f
#define CG_RAW_LAMIGA    0x66
#define CG_RAW_RAMIGA    0x67
#define CG_RAW_HOME      0x70
#define CG_RAW_END       0x71

struct cg_ui;

/* percent: the interface's size against the Toaster's 752x480, 100 or more */
struct cg_ui *cg_ui_new(struct cg_editor *e, struct cg_cmd *cmd, struct cg_host *host, int percent);
void cg_ui_free(struct cg_ui *u);
void cg_ui_set_scale(struct cg_ui *u, int percent);
void cg_ui_size(const struct cg_ui *u, int *w, int *h);
void cg_ui_draw(struct cg_ui *u, uint8_t *rgba);

int cg_ui_mouse(struct cg_ui *u, int event, int x, int y, unsigned qual, unsigned ms);
int cg_ui_key(struct cg_ui *u, int raw, int ascii, unsigned qual);

void cg_ui_status(struct cg_ui *u, const char *message);
/* the book or page changed outside the interface (ARexx, loading) */
void cg_ui_changed(struct cg_ui *u);

/* whether leaving is all right: asks when the book has unsaved changes */
int cg_ui_may_quit(struct cg_ui *u);

/* the page as it will be taken, without the editor's marks (F9, REND) */
void cg_ui_preview(struct cg_ui *u);

/* LOAD with no file: the gadget's requester for CG_ASK_* (cgcmd.h) */
int cg_ui_load(struct cg_ui *u, int what);

#endif
