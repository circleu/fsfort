#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include "fsfort.h"

#define padiv(a, b) ((a + b - 1) / b)
#define padb(sz) (padiv(sz, BSZ) * BSZ)
#define calc_bcnt(dsz) padiv(dsz, BSZ)
#define calc_icnt(bcnt) padiv(bcnt, 4)
#define calc_bbmpsz(bcnt) padb(padiv(bcnt, 8))
#define calc_ibmpsz(icnt) padb(padiv(icnt, 8))
#define calc_itblsz(icnt) padb(icnt * ISZ)
#define realbnum(bnum) (bnum - 1)
#define realinum(inum) (inum - 1)
#define btoaddr(bnum) (realbnum(bnum) * BSZ)
#define getb(bnum) (&target[btoaddr(bnum)])
#define geti(inum) (&itbl[realinum(inum)])

#define exception(msg) {fprintf(stderr, "%s\n", msg);exit(EXIT_FAILURE);}
#define fexception(msg, ...) {fprintf(stderr, msg"\n", __VA_ARGS__);exit(EXIT_FAILURE);}
#define pexception(msg) {perror(msg);exit(EXIT_FAILURE);}
#define bexception(bnum, err) if(bnum==0)fexception("%s: block number cannot be zero", err)
#define iexception(inum, err) if(inum==0)fexception("%s: inode number cannot be zero", err)


extern char* target;
extern long targetsz;
extern sb_t* sb;
extern char* bbmp;
extern char* ibmp;
extern i_t* itbl;
extern char* fstdat;

long pow(long x, long y) {
    long ret = 1;
    for (long i = 0; i < y; i++) {
        ret *= x;
    }
    return ret;
}
char* _strdup(const char* _s) {
    char* s = (char*)calloc(strlen(_s) + 1, sizeof(char));
    strcpy(s, _s);
    
    return s;
}
char* pathcat(const char* p1, const char* p2) {
    char* p = (char*)calloc(strlen(p1) + strlen(p2) + 2, sizeof(char));
    sprintf(p, "%s/%s", p1, p2);

    return p;
}
long realisz(long inum) {
    const char* err = "failed realisz()";
    iexception(inum, err);

    i_t* inode = geti(inum);
    if (inode->depth == 0 && inode->dbcnt == 0) return 0;
    inode->atime = time(NULL);
    char d = inode->depth;

    if (d == 0) {
        char dbcnt = inode->dbcnt;
        
        return dbcnt * DBSZLIM;
    }
    else {
        char ibcnt = BPTRPERI - inode->dbcnt;
        char dbcnt = inode->dbcnt;

        return ibcnt * IDBSZLIM(d) + dbcnt * DBSZLIM;
    }
}
long balloc() {
    const char* err = "failed balloc()";
    long bnum = 0;

    for (long i = 0; i < sb->bbmpsz; i++) {
        for (char j = 0; j < 8; j++) {
            if ((bbmp[i] >> j) & 1) continue;
            bbmp[i] |= (1 << j);
            sb->fbcnt--;
            bnum = i * 8 + j + 1;

            return bnum;
        }
    }
    fexception("%s: out of blocks", err);
}
long bcalloc() {
    const char* err = "failed bcalloc()";
    long bnum = 0;

    for (long i = 0; i < sb->bbmpsz; i++) {
        for (char j = 0; j < 8; j++) {
            if ((bbmp[i] >> j) & 1) continue;
            bbmp[i] |= (1 << j);
            sb->fbcnt--;
            bnum = i * 8 + j + 1;
            memset(getb(bnum), 0, BSZ);

            return bnum;
        }
    }
    fexception("%s: out of blocks", err);
}
void bfree(long bnum) {
    const char* err = "failed bfree()";
    bexception(bnum, err);

    long i = realbnum(bnum) / 8;
    char j = realbnum(bnum) % 8;

    if ((bbmp[i] >> j) & 1) {
        bbmp[i] &= ~(1 << j);
        sb->fbcnt++;
    }
    else fexception("%s: block %ld is not allocated", err, bnum);
}
long icalloc() {
    const char* err = "failed icalloc()";
    long inum = 0;

    for (long i = 0; i < sb->ibmpsz; i++) {
        for (char j = 0; j < 8; j++) {
            if ((ibmp[i] >> j) & 1) continue;
            ibmp[i] |= (1 << j);
            sb->ficnt--;
            inum = i * 8 + j + 1;
            memset(geti(inum), 0, ISZ);

            return inum;
        }
    }
    
    fexception("%s: out of inodes", err);
}
void ifree(long inum) {
    const char* err = "failed ifree()";
    iexception(inum, err);

    long i = realinum(inum) / 8;
    char j = realinum(inum) % 8;

    if ((ibmp[i] >> j) & 1) {
        ibmp[i] &= ~(1 << j);
        sb->ficnt++;
    }
    else fexception("%s: inode %ld is not allocated", err, inum);
}
static void _iballoc(long bnum, char d) {
    const char* err = "failed iballoc()";
    bexception(bnum, err);

    long* b = (long*)getb(bnum);

    if (d == 0) return;
    for (short i = 0; i < PTRSPERB; i++) {
        b[i] = bcalloc();
        _iballoc(b[i], d - 1);
    }
}
void iballoc(long inum, long sz) {
    const char* err = "failed iballoc()";
    iexception(inum, err);
    if (sz == 0) return;

    i_t* inode = geti(inum);
    for (char i = 0; i < BPTRPERI; i++) {
        if (inode->bnums[i] != 0) fexception("%s: block pointers are not zero", err);
    }
    inode->atime = time(NULL);
    inode->mtime = time(NULL);

    if (sz <= DBSZLIM * BPTRPERI) {
        char dbcnt = padiv(sz, DBSZLIM);
        inode->depth = 0;
        inode->dbcnt = dbcnt;

        for (char i = 0; i < dbcnt; i++) {
            inode->bnums[i] = bcalloc();
        }
        return;
    }
    for (char d = 1; d < DEPTHLIM; d++) {
        if (IDBSZLIM(d - 1) * BPTRPERI < sz <= IDBSZLIM(d) * BPTRPERI) {
            char ibcnt = sz / IDBSZLIM(d);
            if (sz % IDBSZLIM(d) > DBSZLIM * (BPTRPERI - ibcnt)) ibcnt++;
            char dbcnt = BPTRPERI - ibcnt;
            inode->depth = d;
            inode->dbcnt = dbcnt;
            
            for (char i = 0; i < ibcnt; i++) {
                inode->bnums[i] = bcalloc();
                _iballoc(inode->bnums[i], d);
            }
            for (char i = ibcnt; i < BPTRPERI; i++) {
                inode->bnums[i] = bcalloc();
            }
            return;
        }
    }
    fexception("%s: size is to large (exceed depth limit)", err);
}
static void _ibfree(long bnum, char d) {
    const char* err = "failed ibfree()";
    bexception(bnum, err);

    long* b = (long*)getb(bnum);

    if (d == 0) return;
    for (short i = 0; i < PTRSPERB; i++) {
        _ibfree(b[i], d - 1);
        bfree(b[i]);
    }
}
void ibfree(long inum) {
    const char* err = "failed ibfree()";
    iexception(inum, err);

    i_t* inode = geti(inum);
    if (inode->depth == 0 && inode->dbcnt == 0) {
        fexception("%s: no block has been allocated to inode %ld", err, inum);
    }
    inode->atime = time(NULL);
    inode->mtime = time(NULL);
    char d = inode->depth;

    if (d == 0) {
        char dbcnt = inode->dbcnt;
        inode->depth = 0;
        inode->dbcnt = 0;

        for (char i = 0; i < dbcnt; i++) {
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
    }
    else {
        char ibcnt = BPTRPERI - inode->dbcnt;
        char dbcnt = inode->dbcnt;
        inode->depth = 0;
        inode->dbcnt = 0;

        for (char i = 0; i < ibcnt; i++) {
            _ibfree(inode->bnums[i], d);
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
        for (char i = ibcnt; i < BPTRPERI; i++) {
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
    }
}
long icreat(long sz, short mode) {
    long inum = icalloc();
    i_t* inode = geti(inum);

    inode->sz = sz;
    inode->ctime = time(NULL);
    inode->atime = time(NULL);
    inode->mtime = time(NULL);
    inode->lcnt = 0;
    inode->mode = mode;
    iballoc(inum, sz);

    return inum;
}
void idel(long inum) {
    const char* err = "failed idel()";
    iexception(inum, err);

    ibfree(inum);
    ifree(inum);
}
static void _iread(long bnum, char d, char* buf, long* ptr) {
    const char* err = "failed iread()";
    bexception(bnum, err);

    long* b = (long*)getb(bnum);

    if (d == 0) {
        memcpy(&buf[*ptr], b, BSZ);
        *ptr += BSZ;

        return;
    }
    for (short i = 0; i < PTRSPERB; i++) {
        _iread(b[i], d - 1, buf, ptr);
    }
}
// buffer size is real size.
void* iread(long inum) {
    const char* err = "failed iread()";
    iexception(inum, err);

    i_t* inode = geti(inum);
    if (inode->depth == 0 && inode->dbcnt == 0) return NULL;
    inode->atime = time(NULL);
    char d = inode->depth;
    char* buf = (char*)mmap(NULL, realisz(inum), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (buf == MAP_FAILED) pexception(err);
    long ptr = 0;

    if (d == 0) {
        char dbcnt = inode->dbcnt;

        for (char i = 0; i < dbcnt; i++) {
            memcpy(&buf[ptr], getb(inode->bnums[i]), BSZ);
            ptr += BSZ;
        }
    }
    else {
        char ibcnt = BPTRPERI - inode->dbcnt;
        char dbcnt = inode->dbcnt;

        for (char i = 0; i < BPTRPERI; i++) {
            _iread(inode->bnums[i], d, buf, &ptr);
        }
        for (char i = ibcnt; i < BPTRPERI; i++) {
            memcpy(&buf[ptr], getb(inode->bnums[i]), BSZ);
            ptr += BSZ;
        }
    }
    return buf;
}
static void _iwrite(long bnum, char d, char* buf, long* ptr) {
    const char* err = "failed iwrite()";
    bexception(bnum, err);

    long* b = (long*)getb(bnum);

    if (d == 0) {
        memcpy(b, &buf[*ptr], BSZ);
        *ptr += BSZ;

        return;
    }
    for (short i = 0; i < PTRSPERB; i++) {
        _iwrite(b[i], d - 1, buf, ptr);
    }
}
void iwrite(long inum, char* buf, long sz) {
    const char* err = "failed iwrite()";
    iexception(inum, err);

    i_t* inode = geti(inum);
    long realsz = realisz(inum);

    if (realsz < sz) {
        if (realsz != 0) ibfree(inum);

        long newinum = icalloc();
        i_t* newinode = geti(newinum);
        *newinode = *inode;
        iballoc(newinum, sz);
        iwrite(newinum, buf, sz);
        *inode = *newinode;
        ifree(newinum);
    }
    else {
        inode->sz = sz;
        inode->atime = time(NULL);
        inode->mtime = time(NULL);

        char* newbuf = (char*)mmap(NULL, realsz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (newbuf == MAP_FAILED) pexception(err);
        memcpy(newbuf, buf, sz);
        char d = inode->depth;
        long ptr = 0;

        if (d == 0) {
            char dbcnt = inode->dbcnt;

            for (char i = 0; i < dbcnt; i++) {
                memcpy(getb(inode->bnums[i]), &newbuf[ptr], BSZ);
                ptr += BSZ;
            }
        }
        else {
            char ibcnt = BPTRPERI - inode->dbcnt;
            char dbcnt = inode->dbcnt;

            for (char i = 0; i < ibcnt; i++) {
                _iwrite(inode->bnums[i], d, newbuf, &ptr);
            }
            for (char i = ibcnt; i < BPTRPERI; i++) {
                memcpy(getb(inode->bnums[i]), &newbuf[ptr], BSZ);
                ptr += BSZ;
            }
        }
        if (munmap(newbuf, realsz) == -1) pexception(err);
    }
}
void idecreat(long inum, const char* name, short mode) {
    const char* err = "failed idecreat()";
    iexception(inum, err);

    i_t* inode = geti(inum);
    if (!S_ISDIR(inode->mode)) fexception("%s: inode %d is not directory", err, inum);
    long realsz = realisz(inum);
    de_t* buf = (de_t*)iread(inum);

    for (long i = 0; i < realsz / DESZ; i++) {
        if (buf[i].inum == 0) {
            buf[i].inum = icreat(0, mode);

            strncpy(buf[i].name, name, MAXPATH);
            iwrite(inum, (char*)buf, realsz);
            geti(buf[i].inum)->lcnt++;

            if (munmap(buf, realsz) == -1) pexception(err);
            return;
        }
    }

    de_t* newbuf = (de_t*)mmap(NULL, realsz + DESZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (newbuf == MAP_FAILED) pexception(err);
    if (buf != NULL) {
        memcpy(newbuf, buf, realsz);
        if (munmap(buf, realsz) == -1) pexception(err);
    }

    iwrite(inum, (char*)newbuf, realsz + DESZ);
    idecreat(inum, name, mode);
    if (munmap(newbuf, realsz + DESZ) == -1) pexception(err);
}
long ifind(const char* path) {
    const char* err = "failed ifind()";
    if (path[0] != '/') fexception("%s: path must be started with root directory(/)", err);

    char* pathcopy = _strdup(path);
    char* tok = strtok(pathcopy, "/");
    de_t* buf = NULL;
    long inum = ROOTINUM;
    long realsz = 0;
    char isok = 0;

    while (tok != NULL) {
        isok = 0;
        realsz = realisz(inum);
        buf = (de_t*)iread(inum);

        for (long i = 0; i < realsz / DESZ; i++) {
            if (!strncmp(buf[i].name, tok, MAXPATH)) {
                inum = buf[i].inum;
                if (munmap(buf, realsz) == -1) pexception(err);
                tok = strtok(NULL, "/");
                isok = 1;
                break;
            }
        }
        if (!isok) {
            free(pathcopy);
            fexception("%s: could not find file or directory '%s'", err, path);
        }
    }
    free(pathcopy);
    return inum;
}
void dopen(const char* path) {
    const char* err = "failed dopen()";

    int fd = open(path, O_RDWR);
    if (fd == -1) pexception(err);

    struct stat stat;
    if (fstat(fd, &stat) == -1) pexception(err);

    if (S_ISREG(stat.st_mode)) {
        targetsz = stat.st_size;
        target = (char*)mmap(NULL, targetsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (target == MAP_FAILED) pexception(err);
        if (close(fd) == -1) pexception(err);
    }
    else fexception("%s: '%s' is not a regular file", err, path);
}
void dsetup() {
    sb = (sb_t*)getb(SBOFF);
    memset(sb, 0, BSZ);
    
    uuid_generate((unsigned char*)sb->uuid);
    memcpy(sb->magic, "FSFORT\0\0", 8);
    

    sb->bcnt = calc_bcnt(targetsz);
    sb->fbcnt = sb->bcnt;
    sb->icnt = calc_icnt(sb->bcnt);
    sb->ficnt = sb->icnt;

    sb->bbmpoff = RESOFF;
    sb->bbmpsz = calc_bbmpsz(sb->bcnt);
    sb->ibmpoff = sb->bbmpoff + sb->bbmpsz / BSZ;
    sb->ibmpsz = calc_ibmpsz(sb->icnt);
    sb->itbloff = sb->ibmpoff + sb->ibmpsz / BSZ;
    sb->itblsz = calc_itblsz(sb->icnt);
    sb->fstdatoff = sb->itbloff + sb->itblsz / BSZ;

    bbmp = getb(sb->bbmpoff);
    ibmp = getb(sb->ibmpoff);
    itbl = (i_t*)getb(sb->itbloff);
    fstdat = getb(sb->fstdatoff);

    memset(getb(sb->bbmpoff), 0, sb->bbmpsz);
    memset(getb(sb->ibmpoff), 0, sb->ibmpsz);

    for (long i = 0; i < SBOFF; i++) {
        balloc();
    }
    for (long i = 0; i < (sb->bbmpsz + sb->ibmpsz + sb->itblsz) / BSZ; i++) {
        balloc();
    }
}
void dget() {
    const char* err = "failed dget()";

    sb = (sb_t*)getb(SBOFF);
    if (memcmp(sb->magic, "FSFORT\0\0", 8)) fexception("%s: magic number not matched", err);

    bbmp = getb(sb->bbmpoff);
    ibmp = getb(sb->ibmpoff);
    itbl = (i_t*)getb(sb->itbloff);
    fstdat = getb(sb->fstdatoff);
}
void dclose() {
    const char* err = "failed dclose()";

    if (munmap(target, targetsz) == -1) pexception(err);
}