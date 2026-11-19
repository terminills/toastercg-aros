/*
    ToasterCG book files ("ToasterBookFile4"), as written by NewTek's
    SaveBook() in original/src/NewBook.c.

    The released source lacks book.h, so the layouts below are
    reconstructed: the chunk sequence and the sizes come from LoadNewBook()
    and SaveBook(), checked against the books shipped on the Video Toaster
    4.3 CD; field names come from their uses in the released code. Fields
    whose meaning is inferred from the data rather than the code are marked.
*/

#ifndef CGBOOK_H
#define CGBOOK_H

#include <stdint.h>
#include <stdio.h>

#define CG_PAGES_PER_BOOK   1000
#define CG_MAX_LINE_CHARS   256
#define CG_DRAWDATA_SIZE    156

/* DrawData (a PostScript graphic, struct DrawData of the CG): what is
   known of its 156 bytes is the text, NUL-terminated, from byte 72. The
   port keeps a graphic's rotation and shear just after that NUL, inside
   the text's own buffer ("CGrs", rotate and shear as big-endian 16-bit
   degrees), where the CG's string functions do not look: rot and skew
   have fields of their own in the CG, whose places are not known. */
#define CG_DRAW_TEXT        72
#define CG_DRAW_TEXT_MAX    50

/* page types and background kinds (values inferred from the data) */
#define CG_BACKGROUND_SOLID     0
#define CG_BACKGROUND_GRADATION 1
#define CG_BACKGROUND_BUFFER    2

/* shadow types, as the CG's F-key steps them (NONE, then up to CAST):
   a drop shadow is the shape repeated along the direction for its whole
   length, a cast shadow one copy at the length (newscroll.c MakeBMShadow) */
#define CG_SHADOW_NONE 0
#define CG_SHADOW_DROP 1
#define CG_SHADOW_CAST 2

enum cg_data_type
{
    CG_DATA_FONT,
    CG_DATA_BRUSH,
    CG_DATA_DRAW
};

struct cg_rgba
{
    uint8_t r, g, b, a;
};

/* struct Attributes, 32 bytes on disk */
struct cg_attr
{
    struct cg_rgba face;
    struct cg_rgba shadow;
    struct cg_rgba outline;
    struct cg_rgba face_grad;
    struct cg_rgba outline_grad;
    int16_t        id;              /* the FONT/BRSH/DRAW the text uses */
    uint8_t        raw0;            /* unnamed, kept for saving */
    uint8_t        shadow_length;   /* pixels, 2..10 */
    uint8_t        shadow_type;     /* 0 none, 1 drop, 2 cast */
    uint8_t        outline_type;
    uint8_t        shadow_direction;    /* light (ARexx SPOT): 0 north, clockwise to 7 northwest */
    uint8_t        shadow_priority;
    uint8_t        raw1[3];         /* unnamed, kept for saving */
    uint8_t        special_fill;    /* CG_FILL_* */
};

/* special_fill: top to bottom gradations, face_grad and outline_grad
   being the bottom colours (FILL_TBGRAD, OLFILL_GRAD) */
#define CG_FILL_FACE_GRAD    0x01
#define CG_FILL_OUTLINE_GRAD 0x10

struct cg_char
{
    uint16_t        ascii;
    int8_t          kerning;
    struct cg_attr *attr;           /* NULL: same as the character before */
    uint8_t         select;         /* editor state, not saved */
};

/* struct CGLine from TotalHeight on, 18 bytes on disk */
struct cg_line
{
    struct cg_line *next;
    uint16_t        nchars;
    struct cg_char *chars;
    uint16_t        total_height;
    uint16_t        baseline;
    int16_t         xoffset;
    int16_t         yoffset;
    uint8_t         raw_flags;
    uint8_t         justify;        /* 0 none, 2 centred (inferred from the data) */
    uint16_t        face_width;     /* box and brush lines; 0 for text */
    uint16_t        face_height;
    uint8_t         raw[2];         /* justify mode ... (unnamed) */
    uint8_t         render_pri;     /* boxes and brushes: 0 back, higher in front */
    uint8_t         raw3;
};

/* struct CGPage from Type on, 12 bytes on disk */
struct cg_page
{
    int             used;
    uint8_t         type;
    uint8_t         speed;          /* crawl 0..3, scroll 0..8 in steps of 2 */
    int8_t          playback;       /* -1 forever, 0 once, 1 once and freeze (scroll) */
    uint8_t         background;     /* CG_BACKGROUND_* */
    struct cg_rgba  top;
    struct cg_rgba  bottom;
    struct cg_line *lines;
    unsigned        nlines;
};

struct cg_data
{
    struct cg_data   *next;
    enum cg_data_type type;
    uint16_t          id;
    uint16_t          height;
    uint32_t          flags;        /* FONT: PSFontFlags */
    char             *filename;     /* as stored, e.g. Toaster:PSFonts/... */
    uint8_t          *drawdata;     /* DRAW: struct DrawData as stored */
};

struct cg_book
{
    char            version;
    struct cg_data *data;
    struct cg_page  pages[CG_PAGES_PER_BOOK];
};

/* Load a book; returns NULL and fills err (if given) on failure. */
struct cg_book *cg_book_load(const char *path, const char **err);
void cg_book_free(struct cg_book *book);

/* the same from memory; a page file's (crouton's) embedded book is found */
struct cg_book *cg_book_parse(const uint8_t *buf, size_t len, const char **err);

/* Save a book as SaveBook() does; returns 0 and fills err (if given) on failure. */
int cg_book_save(const struct cg_book *book, const char *path, const char **err);

/* the book to an open file; only >= 0 writes just that page and the
   fonts and brushes it uses */
int cg_book_write(const struct cg_book *book, FILE *f, int only);
const struct cg_data *cg_book_find_data(const struct cg_book *book, uint16_t id);

/* page types */
enum
{
    CG_PAGE_EMPTY,
    CG_PAGE_STATIC,     /* key page */
    CG_PAGE_BUFFER,     /* full frame with its background */
    CG_PAGE_SCROLL,
    CG_PAGE_CRAWL
};

/* A crawl's text runs through all its lines in order, CG_CRAWL_CHUNK
   characters to a line (NewCrawl.c GetCrawlLine), in the first line's
   place and attributes. This makes it one line: chars is allocated, the
   attribute pointers are the page's own. */
#define CG_CRAWL_CHUNK 80
int cg_crawl_merge(const struct cg_page *p, struct cg_line *out);
void cg_crawl_free(struct cg_line *merged);

/* A line is a box when its first attribute's ID is -1, a brush or a
   PostScript drawing when the ID names one; any other line is text,
   whatever face size it carries (System4.1 page 10 has text with one). */
int cg_line_is_graphic(const struct cg_book *book, const struct cg_line *l);

#endif
