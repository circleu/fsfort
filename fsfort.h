#ifndef _FSFORT_H
#define _FSFORT_H

#define BSZ     4096
#define ISZ     128
#define DESZ    256

#define SBOFF   2
#define RESOFF  3

#define PTRSPERB    (BSZ / sizeof(long))
#define ISPERB      (BSZ / ISZ)
#define DESPERB     (BSZ / DESZ)
#define BPTRPERI      11

#define DBSZLIM         BSZ
#define IDBSZLIM(d)     (pow(PTRSPERB, d) * BSZ)
#define DEPTHLIM        6
#define MAXPATH         248

#define ROOTINUM 1


typedef struct {
    char    name[16];
    char    uuid[16];
    char    magic[8];
    long    bcnt;
    long    icnt;
    long    fbcnt;
    long    ficnt;
    long    bbmpoff;
    long    ibmpoff;
    long    itbloff;
    long    fstdatoff;
    long    bbmpsz;
    long    ibmpsz;
    long    itblsz;
} sb_t;
typedef struct {
    long    sz;
    long    ctime;
    long    atime;
    long    mtime;
    int     lcnt;
    short   mode;
    char    depth;
    char    dbcnt;
    long    bnums[11];
} i_t;
typedef struct {
    long inum;
    char name[248];
} de_t;


#endif