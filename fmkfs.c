#include <stdio.h>
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
"usage: fmkfs <target>"
;

int main(int argc, char** argv) {
    if (argc != 2) {
        exception(usage);
    }
    dopen(argv[1]);
    dsetup();

    if (icreat(0, __S_IFDIR) != 1) {
        exception("root is not located in inode 1");
    }

    dend();
    return 0;
}