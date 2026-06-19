#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include "fsfort.h"
#include "functions.h"


char* target;
long targetsz;

sb_t* sb;
char* bbmp;
char* ibmp;
i_t* itbl;
char* fstdat;

const char* usage =
"usage: mkfsfort <target>"
;

int main(int argc, char** argv) {
    if (argc != 2) {
        exception(usage);
    }
    dopen(argv[1]);
    dsetup();

    long rootinum = ialloc();
    i_t* rooti = &itbl[rootinum];
    rooti->sz = 0;
    rooti->ctime = time(NULL);
    rooti->atime = rooti->ctime;
    rooti->mtime = rooti->ctime;
    rooti->lcnt = 1;
    rooti->mode = __S_IFDIR;
    rooti->depth = 1;
    rooti->bnums[0] = balloc();

    dend();
    return 0;
}