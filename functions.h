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
#define calc_depth(d) ((d - 1) / BPTRPERI)
#define realbnum(bnum) (bnum - 1)
#define realinum(inum) (inum - 1)
#define btoaddr(bnum) (realbnum(bnum) * BSZ)

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
            memset(&target[btoaddr(bnum)], 0, BSZ);

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
    else return;
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
            memset(&itbl[realinum(inum)], 0, ISZ);

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
    else return;
}
static void _iballoc(long bnum, char d) {
    const char* err = "failed iballoc()";
    bexception(bnum, err);

    long* b = (long*)&target[btoaddr(bnum)];

    if (d == 0) return;
    for (short i = 0; i < PTRSPERB; i++) {
        b[i] = bcalloc();
        _iballoc(b[i], d - 1);
    }
}
void iballoc(long inum, long sz) {
    const char* err = "failed iballoc()";
    iexception(inum, err);

    i_t* inode = &itbl[realinum(inum)];
    inode->sz = sz;
    inode->atime = time(NULL);
    inode->mtime = time(NULL);

    if (sz == 0) return;
    else if (sz <= DBSZLIM * BPTRPERI) {
        char bcnt = padiv(sz, DBSZLIM);
        inode->depth = bcnt;
        memset(inode->bnums, 0, sizeof(long) * BPTRPERI);

        for (char i = 0; i < bcnt; i++) {
            inode->bnums[i] = bcalloc();
        }
        return;
    }
    for (char i = 1; i < DEPTHLIM; i++) {
        if (IDBSZLIM(i - 1) * BPTRPERI < sz <= IDBSZLIM(i) * BPTRPERI) {
            char ibcnt = sz / IDBSZLIM(i);
            char dbcnt;

            if (sz % IDBSZLIM(i) > DBSZLIM * (BPTRPERI - ibcnt)) {
                ibcnt++;
            }

            dbcnt = BPTRPERI - ibcnt;
            inode->depth = i * BPTRPERI + ibcnt;
            memset(inode->bnums, 0, sizeof(long) * BPTRPERI);

            for (char j = 0; j < ibcnt; j++) {
                inode->bnums[i] = bcalloc();
                _iballoc(inode->bnums[j], i);
            }
            for (char j = ibcnt; j < BPTRPERI; j++) {
                inode->bnums[j] = bcalloc();
            }
            return;
        }
    }
    fexception("%s: size is to large (exceed depth limit)", err);
}
static void _ibfree(long bnum, char d) {
    const char* err = "failed ibfree()";
    bexception(bnum, err);

    long* b = (long*)&target[btoaddr(bnum)];

    if (d == 0) return;
    for (short i = 0; i < PTRSPERB; i++) {
        _ibfree(b[i], d - 1);
        bfree(b[i]);
    }
}
void ibfree(long inum) {
    const char* err = "failed ibfree()";
    iexception(inum, err);

    i_t* inode = &itbl[realinum(inum)];
    inode->sz = 0;
    inode->atime = time(NULL);
    inode->mtime = time(NULL);

    if (inode->depth == 0) return;
    char d = calc_depth(inode->depth);

    if (d == 0) {
        char bcnt = inode->depth;

        for (char i = 0; i < bcnt; i++) {
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
        return;
    }
    else {
        char ibcnt = inode->depth - d * BPTRPERI;
        char dbcnt = BPTRPERI - ibcnt;

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
long realisz(long inum) {
    const char* err = "failed realisz()";
    iexception(inum, err);

    i_t* inode = &itbl[realinum(inum)];
    inode->atime = time(NULL);

    if (inode->depth == 0) return 0;
    char d = calc_depth(inode->depth);

    if (d == 0) {
        char bcnt = inode->depth;

        return bcnt * DBSZLIM;
    }
    else {
        char ibcnt = inode->depth - d * BPTRPERI;
        char dbcnt = BPTRPERI - ibcnt;

        return ibcnt * IDBSZLIM(d) + dbcnt * DBSZLIM;
    }
}
long icreat(long sz, short mode) {
    long inum = icalloc();
    i_t* inode = &itbl[realinum(inum)];
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
static void _iread(long bnum, char d, char* ret, long* ptr) {
    const char* err = "failed iread()";
    bexception(bnum, err);

    long* b = (long*)&target[btoaddr(bnum)];

    if (d == 0) {
        memcpy(&ret[*ptr], b, BSZ);
        *ptr += BSZ;

        return;
    }
    for (short i = 0; i < PTRSPERB; i++) {
        _iread(b[i], d - 1, ret, ptr);
    }
}
void* iread(long inum) {
    const char* err = "failed iread()";
    iexception(inum, err);

    i_t* inode = &itbl[realinum(inum)];
    inode->atime = time(NULL);

    if (inode->depth == 0) return NULL;
    char d = calc_depth(inode->depth);
    char* ret = (char*)mmap(NULL, realisz(inum), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (ret == MAP_FAILED) pexception(err);
    long ptr = 0;

    if (d == 0) {
        char bcnt = inode->depth;

        for (char i = 0; i < bcnt; i++) {
            memcpy(&ret[ptr], &target[btoaddr(inode->bnums[i])], BSZ);
            ptr += BSZ;
        }
    }
    else {
        char ibcnt = inode->depth - d * BPTRPERI;
        char dbcnt = BPTRPERI - ibcnt;

        for (char i = 0; i < ibcnt; i++) {
            _iread(inode->bnums[i], d, ret, &ptr);
        }
        for (char i = ibcnt; i < BPTRPERI; i++) {
            memcpy(&ret[ptr], &target[btoaddr(inode->bnums[i])], BSZ);
            ptr += BSZ;
        }
    }
    return ret;
}
static void _iwrite(long bnum, char d, char* buf, long* ptr) {
    const char* err = "failed iwrite()";
    bexception(bnum, err);

    long* b = (long*)&target[btoaddr(bnum)];

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

    i_t* inode = &itbl[realinum(inum)];
    long realsz = realisz(inum);
    inode->atime = time(NULL);
    inode->mtime = time(NULL);

    if (realsz >= sz) {
        char* newbuf = (char*)mmap(NULL, realsz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (newbuf == MAP_FAILED) pexception(err);
        memcpy(newbuf, buf, sz);
        char d = calc_depth(inode->depth);
        long ptr = 0;

        if (d == 0) {
            char bcnt = inode->depth;

            for (char i = 0; i < bcnt; i++) {
                memcpy(&target[btoaddr(inode->bnums[i])], &newbuf[ptr], BSZ);
                ptr += BSZ;
            }
        }
        else {
            char ibcnt = inode->depth - d * BPTRPERI;
            char dbcnt = BPTRPERI - ibcnt;

            for (char i = 0; i < ibcnt; i++) {
                _iwrite(inode->bnums[i], d, newbuf, &ptr);
            }
            for (char i = 0; i < BPTRPERI; i++) {
                memcpy(&target[btoaddr(inode->bnums[i])], &newbuf[ptr], BSZ);
                ptr += BSZ;
            }
        }
        if (munmap(newbuf, realsz) == -1) pexception(err);
    }
    else {
        long newinum = icalloc();
        i_t* newinode = &itbl[realinum(newinum)];
        *newinode = *inode;
        iballoc(newinum, sz);
        iwrite(newinum, buf, sz);
        *inode = *newinode;
        idel(newinum);
    }
}
void idecreat(long inum, const char* name, short mode) {
    const char* err = "failed idecreat()";
    iexception(inum, err);

    i_t* inode = &itbl[realinum(inum)];
    long realsz = realisz(inum);

    if (!S_ISDIR(inode->mode)) fexception("%s: inode %d is not directory", err, inum);

    de_t* buf = (de_t*)iread(inum);

    for (long i = 0; i < realsz / DESZ; i++) {
        if (buf[i].inum == 0) {
            buf[i].inum = icreat(0, mode);
            strncpy(buf[i].name, name, MAXPATH);
            iwrite(inum, (char*)buf, realsz);

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

    long i = realsz / DESZ;
    newbuf[i].inum = icreat(0, mode);
    strncpy(newbuf[i].name, name, MAXPATH);
    iwrite(inum, (char*)newbuf, realsz + DESZ);
    if (munmap(newbuf, realsz + DESZ) == -1) pexception(err);
}
long ifind(char* path) {
    const char* err = "failed ifind()";
    if (path[0] != '/') fexception("%s: path must be started with root directory(/)", err);
    path++;

    char* tok = strtok(path, "/");
    de_t* buf = NULL;
    long inum = ROOTINUM;
    long realsz;
    char isok;

    while (tok != NULL) {
        isok = 0;
        realsz = realisz(inum);
        buf = (de_t*)iread(inum);

        for (long i = 0; i < realsz / DESZ; i++) {
            if (!strcmp(buf[i].name, tok)) {
                inum = buf[i].inum;
                munmap(buf, realsz);
                tok = strtok(NULL, "/");
                isok = 1;
            }
        }
        if (!isok) fexception("%s: could not find file or directory '%s'", err, --path);
    }
    return inum;
}
void dopen(const char* fname) {
    const char* err = "failed dopen()";

    int fd = open(fname, O_RDWR);
    if (fd == -1) pexception(err);

    struct stat buf;
    if (fstat(fd, &buf) == -1) pexception(err);

    if (S_ISREG(buf.st_mode)) {
        targetsz = buf.st_size;
        target = (char*)mmap(NULL, targetsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (target == MAP_FAILED) pexception(err);
        if (close(fd) == -1) pexception(err);
    }
    else fexception("%s: '%s' is not a regular file", err, fname);
}
void dsetup() {
    memset(&target[btoaddr(SBOFF)], 0, BSZ);
    sb = (sb_t*)&target[btoaddr(SBOFF)];
    
    uuid_generate((unsigned char*)sb->uuid);
    memcpy(sb->magic, "FSFORT\0", 8);

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

    bbmp = &target[btoaddr(sb->bbmpoff)];
    ibmp = &target[btoaddr(sb->ibmpoff)];
    itbl = (i_t*)&target[btoaddr(sb->itbloff)];
    fstdat = &target[btoaddr(sb->fstdatoff)];

    memset(&target[btoaddr(sb->bbmpoff)], 0, sb->bbmpsz);
    memset(&target[btoaddr(sb->ibmpoff)], 0, sb->ibmpsz);

    for (long i = 0; i < SBOFF; i++) {
        balloc();
    }
    for (long i = 0; i < (sb->bbmpsz + sb->ibmpsz + sb->itblsz) / BSZ; i++) {
        balloc();
    }
}
void dget() {
    const char* err = "failed dget()";

    sb = (sb_t*)&target[btoaddr(SBOFF)];
    if (memcmp(sb->magic, "FSFORT\0", 8)) fexception("%s: magic number not matched", err);

    bbmp = &target[btoaddr(sb->bbmpoff)];
    ibmp = &target[btoaddr(sb->ibmpoff)];
    itbl = (i_t*)&target[btoaddr(sb->itbloff)];
    fstdat = &target[btoaddr(sb->fstdatoff)];
}
void dend() {
    const char* err = "failed dend()";

    if (munmap(target, targetsz) == -1) pexception(err);
}