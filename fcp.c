#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
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
"usage: fcp <target> <dest> <origin>\n"
"       you can indicate path of filesystem using :: prefix\n"
"       you have to use at least one prefixed path\n"
"       path must not be ended with /\n"
"       prefixed path must be started with root directory(/) and not ended with /"
;

int main(int argc, char** argv) {
    if (argc != 4) {
        exception(usage);
    }
    dopen(argv[1]);
    dget();

    char* dp = argv[2];
    char disfs = 0;
    char* op = argv[3];
    char oisfs = 0;

    if (!strncmp(dp, "::", 2)) {
        disfs = 1;
        dp += 2;
        if (dp[0] != '/') exception("error: path must be started with root directory(/)");
    }
    if (!strncmp(op, "::", 2)) {
        oisfs = 1;
        op += 2;
        if (op[0] != '/') exception("error: path must be started with root directory(/)");
    }

    if (disfs && oisfs) {
        long dinum = ifind(dp);
        long oinum = ifind(op);

        char* opcopy = _strdup(op);
        char* oname = basename(opcopy);
        char* newp = pathcat(dp, oname);

        i_t* oinode = geti(oinum);
        void* obuf = iread(oinum);

        idecreat(dinum, oname, oinode->mode);
        iwrite(ifind(newp), obuf, oinode->sz);

        if (munmap(obuf, realisz(oinum)) == -1) pexception("failed munmap()");
        free(newp);
        free(opcopy);
    }
    else if (!disfs && oisfs) {
        long oinum = ifind(op);

        char* opcopy = _strdup(op);
        char* oname = basename(opcopy);
        char* newp = pathcat(dp, oname);

        i_t* oinode = &itbl[realinum(oinum)];
        void* obuf = iread(oinum);
        
        int fd = open(newp, O_WRONLY | O_CREAT);
        if (fd == -1) pexception("failed open()");

        if (write(fd, obuf, realisz(oinum)) == -1) pexception("failed write()");

        if (close(fd) == -1) pexception("failed close()");
        if (munmap(obuf, realisz(oinum)) == -1) pexception("failed munmap()");
        free(newp);
        free(opcopy);
    }
    else if (disfs && !oisfs) {
        long dinum = ifind(dp);

        char* opcopy = _strdup(op);
        char* oname = basename(opcopy);
        char* newp = pathcat(dp, oname);

        int fd = open(op, O_RDONLY);
        if (fd == -1) pexception("failed open()")
        struct stat stat;
        if (fstat(fd, &stat) == -1) pexception("failed fstat()");

        void* obuf = mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (close(fd) == -1) pexception("failed close()");

        idecreat(dinum, oname, stat.st_size);
        iwrite(ifind(newp), obuf, stat.st_size);

        if (munmap(obuf, stat.st_size) == -1) pexception("failed munmap()");
        free(newp);
        free(opcopy);
    }
    else {
        exception(usage);
    }
    
    dclose();
    return 0;
}