#if !defined(__GNUC__) || !defined(__linux__)
#error Unsupported platform or compiler!
#endif /* __GNUC__ */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
 
#if defined(WIDEINT) 
typedef __int128    word_t;
#define WORDSIZEOF  16
#else
typedef long        word_t;
#define WORDSIZEOF  8
#endif /* WIDEINT */
 
#define LINESIZE        64
#define PAGESIZE        8192
 
#define CPYPAGECNT      PAGESIZE / LINESIZE / 2
#define CPYLINEOFFSET   LINESIZE / WORDSIZEOF
#define CPYLINESTEP     CPYLINEOFFSET * 2
 
#define cat_(x, y) x##y
#define cat(x, y) cat_(x, y)
#define lncpy0(d, s, i) d[i] = s[i];
#define lncpy1(d, s) lncpy0(d, s, __COUNTER__)
#define lncpy2(d, s) lncpy1(d, s) lncpy1(d, s)
#define lncpy4(d, s) lncpy2(d, s) lncpy2(d, s)
#define lncpy8(d, s) lncpy4(d, s) lncpy4(d, s)
#define lncpy16(d, s) lncpy8(d, s) lncpy8(d, s)
#define lncopy_(f, d, s) f(d, s)
#define lncopy(dst, src) lncopy_(cat(lncpy, WORDSIZEOF), dst, src)
 
#define errhandler(msg) ({\
    perror("ERROR: " msg); \
    rc = 1; goto cleanup; })
 
#define unlikely(x) \
    __builtin_expect(!!(x), 0)
 
#define reader(fd, buf) ({ \
    __label__ again; \
    ssize_t rb; \
    again: rb = read(fd, buf, sizeof(buf)); \
    if(unlikely(rb == -1)) { \
        if(errno == EINTR) goto again; \
        errhandler("read()"); } rb; })
 
#define forcopy(dst, blksize, estbytes) \
    for(const char *end = dst + (estbytes & ~(blksize - 1)); \
        dst < end; dst += blksize, estbytes -= blksize)
 
static inline void
bytecopy(void * restrict dst, const void * restrict src, size_t len)
{
    if(len == 0) return;
    char *_dst = dst;
    const char *_src = src;
    size_t cnt = (len + 7) / 8;
    switch(len % 8) {
        case 0: do { *_dst++ = *_src++;
        case 7: *_dst++ = *_src++;
        case 6: *_dst++ = *_src++;
        case 5: *_dst++ = *_src++;
        case 4: *_dst++ = *_src++;
        case 3: *_dst++ = *_src++;
        case 2: *_dst++ = *_src++;
        case 1: *_dst++ = *_src++;
        } while(--cnt > 0);
    }
}
 
static inline void
linecopy(void * restrict dst, const void * restrict src)
{
    word_t *_dst = dst;
    const word_t *_src = src;
    lncopy(_dst, _src);
}
 
static inline void
pagecopy(void * restrict dst, const void * restrict src)
{
    word_t *_dst = dst;
    const word_t *_src = src;
    for(int i = 0; i < CPYPAGECNT; ++i, _dst += CPYLINESTEP, _src += CPYLINESTEP) {
        __builtin_prefetch(_src + CPYLINESTEP, 1, 0);
        __builtin_prefetch(_dst + CPYLINESTEP, 0, 0);
        linecopy(_dst, _src);
        linecopy(_dst + CPYLINEOFFSET, _src + CPYLINEOFFSET);
    }
}
 
 
int main(int argc, char *argv[])
{
    struct stat fs;
    ssize_t est, rb;
    int fd, rc = 0, fdo;
    char *dst = NULL, *dstit;
    off64_t offset = PAGESIZE;
    char __attribute__((aligned(PAGESIZE))) iobuf[PAGESIZE];
    
    fd = open(argv[1], O_RDONLY);
    if(fd == -1) {
        errhandler("open()");
    }
 
    fdo = creat(argv[2], 0666);
    if(fdo == -1) {
        errhandler("creat()");
    }
 
    if(fstat(fd, &fs) == -1) {
        errhandler("fstat()");
    }
 
    est = fs.st_size;
    dst = dstit = memalign(PAGESIZE, est);
    if(dst == NULL) {
        errhandler("memalign()");
    }
 
    forcopy(dstit, PAGESIZE, est) {
        readahead(fd, offset, PAGESIZE);
        reader(fd, iobuf);
        pagecopy(dstit, iobuf);
        offset += PAGESIZE;
    }
 
    if(est) {
        rb = reader(fd, iobuf);
        forcopy(dstit, LINESIZE, est) {
            linecopy(dstit, iobuf);
        }
        rb -= est;
        bytecopy(dstit, iobuf + rb - est, est);
    }
 
    if(write(fdo, dst, dstit - dst) == -1) {
        errhandler("write()");
    }
    
cleanup:
    close(fd);
    close(fdo);
    free(dst);
 
    return rc;
}
