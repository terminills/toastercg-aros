/*
    The CG's ARexx functions (CG_AREXX), as original/src/cgrexx.c
    defines them: a verb (GET_ SET_ PICK MAKE KILL LOAD SAVE EXIT REQ_
    REND REXX MACR) and an object (FONT CHAR BRUS LINE PAGE BOOK PICT
    RECT SHAD BORD BACK TEXT DRAW FILE), e.g. LOADFONT or SET_CHAR.
*/

#ifndef CGCMD_H
#define CGCMD_H

#include <stddef.h>

#include "cgedit.h"

#define CG_RC_OK    0
#define CG_RC_WARN  5
#define CG_RC_ERROR 10

/* ARexx secondary results (rm_Result2) on failure, as cgrexx.c gives them:
   an unknown function lets ARexx try the next function host */
#define CG_ERR_MISSING_FN 1
#define CG_ERR_BADARG     18

/* what LOAD asks for when given no file, as the bar's gadgets do */
enum { CG_ASK_FONT, CG_ASK_BRUSH, CG_ASK_TEXT, CG_ASK_PICT };

#define CG_RESULT_MAX 256
#define CG_MACROS     20
#define CG_MACRO_MAX  128

/* what only the program around the editor can do */
struct cg_host
{
    void *ud;
    /* lines of text; buttons: 0 continue, 1 OK/Cancel; 1 for OK */
    int (*request)(void *ud, const char *const *lines, int n, int okcancel);
    /* file requester; file and dir in and out; returns 0 on cancel */
    int (*file)(void *ud, const char *title, char *file, size_t filelen, char *dir, size_t dirlen, int save);
    int (*string)(void *ud, const char *prompt, char *buf, size_t len);
    int (*number)(void *ud, const char *prompt, long *value);
    void (*status)(void *ud, const char *message);
    int (*macro)(void *ud, const char *name);
    int (*render)(void *ud);
    int (*load_book)(void *ud, const char *path);
    int (*load_background)(void *ud, const char *path);
    /* a requester for what LOAD names (CG_ASK_*); 1 when loaded */
    int (*ask_load)(void *ud, int what);
    /* free memory for Help: graphics (chip) and other (fast), in bytes; on
       AROS pc-x86_64 the first 2 GB are chip memory, the rest fast */
    void (*memory)(void *ud, unsigned long *graphics, unsigned long *other);
    /* the Switcher to the front, the CG staying as it is; 0 when none runs */
    int (*switcher)(void *ud);
};

struct cg_cmd
{
    struct cg_editor *e;
    struct cg_host   *host;
    unsigned          font_list;
    char              macros[CG_MACROS][CG_MACRO_MAX];
    char              startup[CG_MACRO_MAX];
    char              paths[5][CG_MACRO_MAX];
    int               exit;
    int               err;      /* rm_Result2 after a failure */
    char              req_dir[CG_RESULT_MAX]; /* REQ_FILE/REQ_DIR's last drawer */
};

void cg_cmd_init(struct cg_cmd *c, struct cg_editor *e, struct cg_host *host);

/* run one function; argv excludes the name. Returns an ARexx RC and
   fills result (may be empty); on failure c->err is the secondary code. */
int cg_cmd_run(struct cg_cmd *c, const char *name, int argc, const char *const *argv, char *result);

/* run a "NAME arg arg" line, quoted args allowed (for tests and scripts) */
int cg_cmd_line(struct cg_cmd *c, const char *line, char *result);

/* read CG_Support/cg-config: macros, startup macro, paths */
int cg_cmd_read_config(struct cg_cmd *c, const char *path);

/* after a book is loaded: each font or brush it names that cannot be
   loaded is offered for replacement, as NewBook.c's ReplaceData does; a
   brush left unreplaced becomes a box, a font stays named (its text does
   not show). Returns how many were replaced. */
int cg_cmd_check_book(struct cg_cmd *c);

/* a requester's drawer and file as one path: a '/' between them unless
   the drawer ends a device or drawer; a file typed as a whole path (with
   a device, or from the root) is taken as it is */
void cg_join_path(char *out, size_t len, const char *dir, const char *file);

/* add a font or brush to the book and make it current, as LOADFONT does */
struct cg_data *cg_cmd_add_font(struct cg_cmd *c, const char *filename, int height);
struct cg_data *cg_cmd_add_brush(struct cg_cmd *c, const char *filename);

#endif
