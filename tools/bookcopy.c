/*
    bookcopy: load a ToasterCG book and save it again.
*/

#include <stdio.h>

#include "cgbook.h"

int main(int argc, char **argv)
{
    struct cg_book *book;
    const char *err = NULL;

    if (argc != 3)
    {
        fprintf(stderr, "usage: bookcopy in.Book out.Book\n");
        return 2;
    }
    if (!(book = cg_book_load(argv[1], &err)))
    {
        fprintf(stderr, "bookcopy: %s: %s\n", argv[1], err);
        return 1;
    }
    if (!cg_book_save(book, argv[2], &err))
    {
        fprintf(stderr, "bookcopy: %s: %s\n", argv[2], err);
        cg_book_free(book);
        return 1;
    }
    cg_book_free(book);
    return 0;
}
