/*
    rexxsink: a stand-in REXX port for testing what the CG sends its ARexx
    interpreter. Each message is written to the file (action, arguments)
    and replied to with RC 0, as an interpreter would once the macro ran.

    rexxsink FILE [COUNT]     stops after COUNT messages (default 100) or
                              Ctrl-C
*/

#include <exec/ports.h>
#include <rexx/storage.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    struct MsgPort *port;
    struct RexxMsg *rm;
    FILE *f;
    int left, done = 0;

    if (argc < 2 || !(f = fopen(argv[1], "w")))
    {
        printf("usage: rexxsink FILE [COUNT]\n");
        return 20;
    }
    left = argc > 2 ? atoi(argv[2]) : 100;
    Forbid();
    if (FindPort((CONST_STRPTR)"REXX") || !(port = CreateMsgPort()))
    {
        Permit();
        fprintf(f, "there is a REXX port already\n");
        fclose(f);
        return 20;
    }
    port->mp_Node.ln_Name = "REXX";
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    Permit();
    fprintf(f, "listening\n");
    fflush(f);
    while (!done && left > 0)
    {
        ULONG sigs = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C)
            done = 1;
        while ((rm = (struct RexxMsg *)GetMsg(port)))
        {
            int i, n = rm->rm_Action & RXARGMASK;

            fprintf(f, "action %s args %d:", (rm->rm_Action & RXCODEMASK) == RXCOMM ? "RXCOMM" :
                    (rm->rm_Action & RXCODEMASK) == RXFUNC ? "RXFUNC" : "other", n);
            for (i = 0; i <= n && i < 16; i++)
                fprintf(f, " [%s]", rm->rm_Args[i] ? (char *)rm->rm_Args[i] : "");
            fprintf(f, "\n");
            fflush(f);
            rm->rm_Result1 = 0;
            rm->rm_Result2 = 0;
            ReplyMsg(&rm->rm_Node);
            left--;
        }
    }
    Forbid();
    RemPort(port);
    while ((rm = (struct RexxMsg *)GetMsg(port)))
    {
        rm->rm_Result1 = 20;
        ReplyMsg(&rm->rm_Node);
    }
    Permit();
    DeleteMsgPort(port);
    fprintf(f, "done\n");
    fclose(f);
    return 0;
}
