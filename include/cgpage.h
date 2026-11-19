/*
    Page files ("croutons"): an IFF ILBM picture of the page, its icon,
    and a CrUD form whose tags carry the page as a one page book.
*/

#ifndef CGPAGE_H
#define CGPAGE_H

#include "cgedit.h"

int cg_page_save(struct cg_editor *e, const char *path, const char **err);

/* a page file, or the first page of a book, onto the current page */
int cg_page_load(struct cg_editor *e, const char *path, const char **err);

#endif
