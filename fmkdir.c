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
"usage: fmkdir <target> <path> <name>\n"
"       path must be started with root directory(/) and not ended with /"
;

int main(int argc, char** argv) {
    if (argc != 4) {
        exception(usage);
    }
    dopen(argv[1]);
    dget();

    long inum = ifind(argv[2]);
    idecreat(inum, argv[3], __S_IFDIR);

    dend();
    return 0;
}