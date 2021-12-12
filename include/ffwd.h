#ifndef __FFWD_H
#define __FFWD_H

#define CACHELINE   64
#define MAXCPUS     32

#define FL_REQ                  0x00000001
#define REQ_REVERSE(flags)      (flags ^= FL_REQ)

#define FL_RESP(i)              (0x00000001 << i)
#define RESP_REVERSE(flags, i)  (flags ^= (0x00000001 << i))
#define RESP_GET(flags, i)      ((flags >> i) & 0x00000001)

struct request {
    long syscallid;
    int flags;
    int argc;
    long argv[6];
}__attribute__((aligned(2*CACHELINE)));

struct response {
    int flags;
    long ret[MAXCPUS - 1];
}__attribute__((aligned(2*CACHELINE)));

extern int _ffwd_launch();
extern void _ffwd_shutdown();
extern long _ffwd_dsyscall(long syscallno, int argc, ...);

#endif /* __FFWD_H */