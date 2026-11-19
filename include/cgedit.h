/*
    Editing a ToasterCG book: the current page, line and cursor, the
    default attributes new text gets, and the selection, as the CG's
    RenderData keeps them (CurrentPage, CurrentLine, CursorPosition,
    DefaultAttr, DefaultLine).
*/

#ifndef CGEDIT_H
#define CGEDIT_H

#include "cgbook.h"
#include "cgrender.h"

enum
{
    CG_JUSTIFY_NONE,
    CG_JUSTIFY_LEFT,
    CG_JUSTIFY_CENTER,
    CG_JUSTIFY_RIGHT
};

/* what SetSelectAttrib() changes */
enum cg_attr_field
{
    CG_ATTR_ID,
    CG_ATTR_FACE,
    CG_ATTR_FACE_GRAD,
    CG_ATTR_SHADOW,
    CG_ATTR_OUTLINE,
    CG_ATTR_OUTLINE_GRAD,
    CG_ATTR_SHADOW_TYPE,
    CG_ATTR_SHADOW_LENGTH,
    CG_ATTR_SHADOW_DIRECTION,
    CG_ATTR_SHADOW_PRIORITY,
    CG_ATTR_OUTLINE_TYPE,
    CG_ATTR_FILL,
    CG_ATTR_COLORS      /* ATTR_COLOR: all five colours */
};

#define CG_BOX_ID       (-1)
#define CG_SAFE_LEFT    76
#define CG_SAFE_RIGHT   676
#define CG_SAFE_TOP     48
#define CG_SAFE_BOTTOM  432
#define CG_CRAWL_HEIGHT 300

struct cg_editor
{
    struct cg_book     *book;
    struct cg_renderer *r;
    int                 page;
    struct cg_line     *line;       /* current line, NULL on an empty page */
    int                 cursor;     /* character index, 0..length */
    struct cg_attr      attr;       /* DefaultAttr */
    uint8_t             justify;    /* DefaultLine.JustifyMode */
    int                 dirty;
    const char         *message;    /* last refusal, for the status line */
};

struct cg_editor *cg_edit_new(struct cg_book *book, struct cg_renderer *r);
void cg_edit_free(struct cg_editor *e);
void cg_edit_set_book(struct cg_editor *e, struct cg_book *book);

struct cg_page *cg_edit_page(struct cg_editor *e);

/* the line the cursor moves along: on a crawl its whole text merged into
   tmp (cg_edit_release() it after), else the current line */
const struct cg_line *cg_edit_cursor_line(struct cg_editor *e, struct cg_line *tmp);
void cg_edit_release(struct cg_line *tmp);
int cg_edit_length(struct cg_editor *e);
int cg_line_length(const struct cg_line *l);
const struct cg_attr *cg_char_attr(const struct cg_line *l, int i);

/* pages */
void cg_edit_goto_page(struct cg_editor *e, int page);
int cg_edit_set_page_type(struct cg_editor *e, int type);
void cg_edit_erase_page(struct cg_editor *e);
void cg_edit_erase_book(struct cg_editor *e);
int cg_edit_copy_page(struct cg_editor *e, int to);

/* the cursor */
void cg_edit_set_line(struct cg_editor *e, struct cg_line *l, int cursor);
void cg_edit_left(struct cg_editor *e);
void cg_edit_right(struct cg_editor *e);
void cg_edit_up(struct cg_editor *e);
void cg_edit_down(struct cg_editor *e);
void cg_edit_home(struct cg_editor *e);
void cg_edit_end(struct cg_editor *e);
void cg_edit_return(struct cg_editor *e);
void cg_edit_tab(struct cg_editor *e);
struct cg_line *cg_edit_line_above(struct cg_editor *e, const struct cg_line *l);
struct cg_line *cg_edit_line_below(struct cg_editor *e, const struct cg_line *l);
struct cg_line *cg_edit_topmost(struct cg_editor *e);
struct cg_line *cg_edit_bottommost(struct cg_editor *e);

/* text */
int cg_edit_insert(struct cg_editor *e, uint16_t ascii);
void cg_edit_backspace(struct cg_editor *e);
void cg_edit_delete(struct cg_editor *e);
void cg_edit_erase_line(struct cg_editor *e);
struct cg_line *cg_edit_new_line(struct cg_editor *e, int x, int y);
int cg_edit_load_text(struct cg_editor *e, const char *path);

/* selection */
void cg_edit_select_char(struct cg_editor *e, struct cg_line *l, int i, int on);
void cg_edit_select_line(struct cg_editor *e, struct cg_line *l, int on);
void cg_edit_select_page(struct cg_editor *e, int on);
int cg_edit_line_selected(const struct cg_line *l);
int cg_edit_any_selected(struct cg_editor *e);

/* attributes: the default, and every selected character */
int cg_edit_all_select_same(struct cg_editor *e, enum cg_attr_field f);
void cg_edit_set_attr(struct cg_editor *e, enum cg_attr_field f);
void cg_edit_take_attr(struct cg_editor *e);
void cg_edit_set_justify(struct cg_editor *e, int mode);
void cg_edit_justify_line(struct cg_editor *e, struct cg_line *l);
void cg_edit_fix_line(struct cg_editor *e, struct cg_line *l);

/* boxes and brushes */
int cg_edit_make_box(struct cg_editor *e, int x, int y, int w, int h);
void cg_edit_depth(struct cg_editor *e, int front);
void cg_edit_move_selected(struct cg_editor *e, int dx, int dy);

/* the book's fonts and brushes */
struct cg_data *cg_edit_add_data(struct cg_editor *e, enum cg_data_type type, const char *filename, int height);
struct cg_data *cg_edit_add_draw(struct cg_editor *e, const struct cg_data *font, const char *text, int rot, int shear);
struct cg_data *cg_edit_find_data(struct cg_editor *e, const char *filename, int height);
int cg_edit_data_used(struct cg_editor *e, uint16_t id);
int cg_edit_remove_unused_data(struct cg_editor *e);

#endif
