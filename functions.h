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
#define btoaddr(bnum) ((bnum - 1) * BSZ)
#define calc_depth(d) ((d - 1) / BPTRPERI)

#define exception(msg) {fprintf(stderr, "%s\n", msg);exit(EXIT_FAILURE);}
#define fexception(msg, ...) {fprintf(stderr, msg"\n", __VA_ARGS__);exit(EXIT_FAILURE);}
#define pexception(msg) {perror(msg);exit(EXIT_FAILURE);}


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
    const char* errmsg = "failed balloc()";

    for (long i = 0; i < sb->bbmpsz; i++) {
        for (char j = 0; j < 8; j++) {
            if (((bbmp[i] >> j) & 1) == 0) {
                bbmp[i] |= (1 << j);
                sb->fbcnt--;
                
                return i * 8 + j + 1;
            }
        }
    }
    fexception("%s: out of blocks", errmsg);
}
void bfree(long bnum) {
    if (bnum == 0) exception("failed bfree(): block number cannot be 0");
    long i = (bnum - 1) / 8;
    char j = (bnum - 1) % 8;

    bbmp[i] &= ~(1 << j);
    sb->fbcnt++;
}
long ialloc() {
    const char* errmsg = "failed ialloc()";

    for (long i = 0; i < BSZ; i++) {
        for (char j = 0; j < 8; j++) {
            if (((ibmp[i] >> j) & 1) == 0) {
                ibmp[i] |= (1 << j);
                sb->ficnt--;

                return i * 8 + j + 1;
            }
        }
    }
    fexception("%s: out of inodes", errmsg);
}
void ifree(long inum) {
    if (inum == 0) exception("failed ifree(): inode number cannot be 0");
    long i = (inum - 1) / 8;
    char j = (inum - 1) % 8;

    ibmp[i] &= ~(1 << j);
    sb->ficnt++;
}
static void _iballoc(long bnum, char d) {
    const char* errmsg = "failed iballoc()";
    if (bnum == 0) fexception("%s: block number cannot be 0", errmsg);
    long* b = (long*)&target[btoaddr(bnum)];

    if (d > 0) {
        for (int i = 0; i < PTRSPERB; i++) {
            b[i] = balloc();
            _iballoc(b[i], d - 1);
        }
    }
    else return;
}
void iballoc(long inum, long sz) {
    const char* errmsg = "failed iballoc()";
    if (inum == 0) fexception("%s: inode number cannot be 0", errmsg);

    i_t* inode = &itbl[inum - 1];

    inode->sz = sz;
    inode->atime = time(NULL);
    inode->mtime = inode->atime;

    // for direct blocks
    if (sz <= DBSZLIM * BPTRPERI) {
        char bcnt = padiv(sz, BSZ);
        
        inode->depth = bcnt;
        for (char i = 0; i < bcnt; i++) {
            inode->bnums[i] = balloc();
        }
        return;
    }
    // for indirect blocks
    for (char i = 1; i < DEPTHLIM; i++) {
        if (IDBSZLIM(i - 1) * BPTRPERI < sz <= IDBSZLIM(i) * BPTRPERI) {
            char ibcnt = sz / IDBSZLIM(i);
            char dbcnt;

            if (sz % IDBSZLIM(i) > DBSZLIM * (BPTRPERI - ibcnt)) {
                dbcnt = 0;
                ibcnt++;
            }
            else {
                dbcnt = BPTRPERI - ibcnt;
            }
            inode->depth = BPTRPERI * i + ibcnt;
            for (char j = 0; j < ibcnt; j++) {
                inode->bnums[j] = balloc();
                _iballoc(inode->bnums[j], i);
            }
            for (char j = ibcnt; j < BPTRPERI; j++) {
                inode->bnums[j] = balloc();
            }
            return;
        }
    }
    fexception("%s: size is to large (exceed depth 5)", errmsg);
}
static void _ibfree(long bnum, char d) {
    const char* errmsg = "failed ibfree()";
    if (bnum == 0) fexception("%s: block number cannot be 0", errmsg);
    long* b = (long*)&target[btoaddr(bnum)];

    if (d > 0) {
        for (int i = 0; i < PTRSPERB; i++) {
            _ibfree(b[i], d - 1);
            bfree(bnum);
        }
    }
    else {
        bfree(bnum);
    }
}
void ibfree(long inum) {
    const char* errmsg = "failed ibfree()";
    if (inum == 0) fexception("%s: inode number cannot be 0", errmsg);

    i_t* inode = &itbl[inum - 1];

    inode->sz = 0;
    inode->atime = time(NULL);
    inode->mtime = inode->atime;

    if (inode->depth == 0) return NULL;
    char depth = calc_depth(inode->depth);

    if (depth == 0) {
        char bcnt = inode->depth;

        for (char i = 0; i < bcnt; i++) {
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
    }
    else {
        char ibcnt = inode->depth - depth * BPTRPERI;
        char dbcnt = BPTRPERI - ibcnt;

        for (char i = 0; i < ibcnt; i++) {
            _ibfree(inode->bnums[i], depth);
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
        for (char i = ibcnt; i < BPTRPERI; i++) {
            bfree(inode->bnums[i]);
            inode->bnums[i] = 0;
        }
    }
}
long igetrealsz(long inum) {
    const char* errmsg = "failed igetrealsz()";
    if (inum == 0) fexception("%s: inode number cannot be 0", errmsg);

    i_t* inode = &itbl[inum - 1];
    inode->atime = time(NULL);

    if (inode->depth == 0) return 0;
    char depth = calc_depth(depth);
    
    if (depth == 0) {
        char bcnt = inode->depth;
        return DBSZLIM * bcnt;
    }
    else {
        char ibcnt = inode->depth - depth * BPTRPERI;
        char dbcnt = BPTRPERI - ibcnt;
        return IDBSZLIM(depth) * ibcnt + DBSZLIM * dbcnt;
    }
}
static void _iread(long bnum, char d, void* ret, long* ptr) {
    const char* errmsg = "failed iread()";
    if (bnum == 0) fexception("%s: block number cannot be 0", errmsg);
    long* b = (long*)&target[btoaddr(bnum)];

    if (d > 0) {
        for (int i = 0; i < PTRSPERB; i++) {
            _iread(b[i], d - 1, ret, ptr);
        }
    }
    else {
        memcpy(&ret[*ptr], (void*)b, BSZ);
        *ptr += BSZ;
    }
}
// CALL munmap() AFTER USE!!!!
void* iread(long inum) {
    const char* errmsg = "failed iread()";
    if (inum == 0) fexception("%s: inode number cannot be 0", errmsg);

    i_t* inode = &itbl[inum - 1];
    inode->atime = time(NULL);
    
    if (inode->depth == 0) return NULL;
    void* ret = mmap(NULL, igetrealsz(inum), PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    char depth = calc_depth(inode->depth);
    long ptr = 0;

    if (depth == 0) {
        char bcnt = inode->depth;

        for (char i = 0; i < bcnt; i++) {
            memcpy(&ret[ptr], &target[btoaddr(inode->bnums[i])], BSZ);
            ptr += BSZ;
        }
    }
    else {
        char ibcnt = inode->depth - depth * BPTRPERI;
        char dbcnt = BPTRPERI - ibcnt;

        for (char i = 0; i < ibcnt; i++) {
            _iread(inode->bnums[i], depth, ret, &ptr);
        }
        for (char i = ibcnt; i < BPTRPERI; i++) {
            memcpy(&ret[ptr], &target[btoaddr(inode->bnums[i])], BSZ);
            ptr += BSZ;
        }
    }
    return ret;
}
static void _iwrite(long bnum, char d, void* buf, long* ptr) {
    const char* errmsg = "failed iwrite()";
    if (bnum == 0) fexception("%s: block number cannot be 0", errmsg);
    long* b = (long*)&target[btoaddr(bnum)];

    if (d > 0) {
        for (int i = 0; i < PTRSPERB; i++) {
            _write(b[i], d - 1, buf, ptr);
        }
    }
    else {
        memcpy((void*)b, &buf[*ptr], BSZ);
        *ptr += BSZ;
    }
}
void iwrite(long inum, void* buf, long sz) {
    const char* errmsg = "failed iwrite()";
    if (inum == 0) fexception("%s: inode number cannot be 0", errmsg);

    i_t* inode = &itbl[inum - 1];
    inode->atime = time(NULL);
    inode->mtime = inode->atime;
    
    if (igetrealsz(inum) >= sz) {
        char depth = calc_depth(inode->depth);
        long ptr = 0;

        if (depth == 0) {
            char bcnt = inode->depth;

            for (char i = 0; i < bcnt; i++) {
                memcpy(&target[btoaddr(inode->bnums[i])], &buf[ptr], BSZ);
                ptr += BSZ;
            }
        }
        else {
            char ibcnt = inode->depth - depth * BPTRPERI;
            char dbcnt = BPTRPERI - ibcnt;

            for (char i = 0; i < ibcnt; i++) {
                _iread(inode->bnums[i], depth, buf, &ptr);
            }
            for (char i = ibcnt; i < BPTRPERI; i++) {
                memcpy(&target[btoaddr(inode->bnums[i])], &buf[ptr], BSZ);
                ptr += BSZ;
            }
        }
    }
    else {
        long newinum = ialloc();
        i_t* newinode = &itbl[newinum];

        *newinode = *inode;
        iballoc(newinum, sz);
        iwrite(newinum, buf, sz);
        *inode = *newinode;
        iremove(newinum);
    }
}

// this opens disk image.
// for compatibility, we don't consider block devices here.
void dopen(const char* fname) {
    const char* errmsg = "failed init()";

    // open file
    int fd = open(fname, O_RDWR);
    if (fd == -1) pexception(errmsg);

    // get file info
    struct stat buf;
    if (fstat(fd, &buf) == -1) pexception(errmsg);
    
    // we consider only regular files
    if (S_ISREG(buf.st_mode)) {
        targetsz = buf.st_size;
        target = mmap(NULL, targetsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (target == MAP_FAILED) pexception(errmsg);
        if (close(fd) == -1) pexception(errmsg);
    }
    else fexception("%s: '%s' is not a regular file", errmsg, fname);
}
// this sets up superblock, block bitmap, inode bitmap, and inode table.
// I reckon this is used only for mkfsfort.
void dsetup() {
    memset(&target[btoaddr(SBOFF)], 0, BSZ);
    sb = (sb_t*)&target[btoaddr(SBOFF)];

    uuid_generate(sb->uuid);
    strcpy(sb->magic, "FSFORT\0");

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

    memset(&target[btoaddr(SBOFF)], 0, BSZ);
    memset(&target[btoaddr(sb->bbmpoff)], 0, sb->bbmpsz / BSZ);
    memset(&target[btoaddr(sb->ibmpoff)], 0, sb->ibmpsz / BSZ);

    // reserve blocks for superblock
    for (long i = 0; i < SBOFF; i++) {
        balloc();
    }
    // reserve blocks for bitmaps and tables
    for (long i = 0; i < (sb->bbmpsz + sb->ibmpsz + sb->itblsz) / BSZ; i++) {
        balloc();
    }
}
// this gets info of block bitmap, inode bitmap, and inode table from superblock.
void dget() {
    const char* errmsg = "failed dget()";

    sb = (sb_t*)&target[btoaddr(SBOFF)];
    if (memcmp(sb->magic, "FSFORT\0", 8))
        fexception("%s: magic number not matched", errmsg);
    
    bbmp = &target[btoaddr(sb->bbmpoff)];
    ibmp = &target[btoaddr(sb->ibmpoff)];
    itbl = (i_t*)&target[btoaddr(sb->itbloff)];
    fstdat = &target[btoaddr(sb->fstdatoff)];
}
void dend() {
    const char* errmsg = "failed dend()";

    if (msync(target, targetsz, MS_SYNC) == -1)
        pexception(errmsg);
    if (munmap(target, targetsz) == -1)
        pexception(errmsg);
}