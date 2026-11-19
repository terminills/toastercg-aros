/*
    cgrx: call one CG_AREXX function from the shell, as an ARexx script
    would (an RXFUNC message with RXFF_RESULT), and print its result.

    cgrx FUNCTION [arg ...]      e.g.  cgrx SET_PAGE 4
*/

#include <exec/ports.h>
#include <rexx/storage.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>

#include <stdio.h>
#include <string.h>

#include "version.h"

static const char version[] __attribute__((used)) = "$VER: cgrx " CG_VERSTRING " (" CG_DATE ")";

int main(int argc, char **argv)
{
    struct MsgPort *reply, *cg;
    struct RexxMsg *rm;
    int i, rc = 20;

    if (argc < 2 || argc > 16)
    {
        printf("usage: cgrx FUNCTION [arg ...]\n");
        return 20;
    }
    if (!(reply = CreateMsgPort()))
        return 20;
    if (!(rm = CreateRexxMsg(reply, NULL, NULL)))
    {
        DeleteMsgPort(reply);
        return 20;
    }
    rm->rm_Action = RXFUNC | RXFF_RESULT | (argc - 2);
    for (i = 1; i < argc; i++)
        rm->rm_Args[i - 1] = (IPTR)CreateArgstring((UBYTE *)argv[i], strlen(argv[i]));
    Forbid();
    if ((cg = FindPort((CONST_STRPTR)"CG_AREXX")))
        PutMsg(cg, &rm->rm_Node);
    Permit();
    if (cg)
    {
        WaitPort(reply);
        GetMsg(reply);
        rc = (int)rm->rm_Result1;
        printf("%s rc %d", argv[1], rc);
        if (!rc && rm->rm_Result2)
        {
            printf(" \"%s\"", (char *)rm->rm_Result2);
            DeleteArgstring((UBYTE *)rm->rm_Result2);
        }
        printf("\n");
    }
    else
        printf("cgrx: no CG_AREXX port\n");
    for (i = 1; i < argc; i++)
        DeleteArgstring((UBYTE *)rm->rm_Args[i - 1]);
    DeleteRexxMsg(rm);
    DeleteMsgPort(reply);
    return rc;
}
