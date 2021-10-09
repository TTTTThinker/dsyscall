#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>

#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "ffwd.h"

static void clean_up_handler(void)
{
    printf("[coreID: %4d || threadID: %ld] ===> Server routine exiting ...\n", sched_getcpu(), syscall(SYS_gettid));
}

void *server_routine(void *args)
{
    int modified = 0;
    struct response re;    
    int cpuid = sched_getcpu();
    long threadid = syscall(SYS_gettid);

    cpu_set_t cpumask;
    CPU_ZERO(&cpumask);
    CPU_SET(cpuid, &cpumask);
    sched_setaffinity(threadid, sizeof(cpu_set_t), &cpumask);

    printf("[coreID: %4d || threadID: %ld] ===> Server routine start serving ...\n", cpuid, threadid);

    int cancelstate, canceltype;
    pthread_cleanup_push(clean_up_handler, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);

    while(1) {
        re.flags = responses.flags;
        for (int i = 0; i < MAXCPUS - 1; ++i) {
            if ((requests[i].flags & FL_REQ) ^ (responses.flags & FL_RESP(i))) {
                modified = 1;
                switch (requests[i].argc)
                {
                case 0:
                    re.ret[i] = syscall(requests[i].syscallid);
                    break;
                case 1:
                    re.ret[i] = syscall(requests[i].syscallid, requests[i].argv[0]);
                    break;
                case 2:
                    re.ret[i] = syscall(requests[i].syscallid, requests[i].argv[0], requests[i].argv[1]);
                    break;
                case 3:
                    re.ret[i] = syscall(requests[i].syscallid, requests[i].argv[0], requests[i].argv[1], requests[i].argv[2]);
                    break;
                case 4:
                    re.ret[i] = syscall(requests[i].syscallid, requests[i].argv[0], requests[i].argv[1], requests[i].argv[2], requests[i].argv[3]);
                    break;
                case 5:
                    re.ret[i] = syscall(requests[i].syscallid, requests[i].argv[0], requests[i].argv[1], requests[i].argv[2], requests[i].argv[3], requests[i].argv[4]);
                    break;
                case 6:
                    re.ret[i] = syscall(requests[i].syscallid, requests[i].argv[0], requests[i].argv[1], requests[i].argv[2], requests[i].argv[3], requests[i].argv[4], requests[i].argv[5]);
                    break;
                
                default:
                    break;
                }
                RESP_REVERSE(re.flags, i);
            }
        }
        if (modified) {
            responses = re;
            modified = 0;
        }
#ifdef DEBUG
    printf("[coreID: %4d || threadID: %ld] ===> A round finished.\n", cpuid, threadid);
#endif
    }
    pthread_cleanup_pop(clean_up_handler);
}

long dsyscall(long syscallno, int argc, ...)
{
    struct request req = {
        .syscallid = syscallno,
        .argc = argc
    };
    long ret;

    va_list args;
    va_start(args, argc);
    for (int i = 0; i < argc; ++i)
        req.argv[i] = va_arg(args, long);
    va_end(args);
    
    int cpuid = sched_getcpu();
    while ((requests[cpuid].flags & FL_REQ) ^ RESP_GET(responses.flags, cpuid));
    req.flags = requests[cpuid].flags;
    REQ_REVERSE(req.flags);
    requests[cpuid] = req;
    // Maybe context switch between REQ & RESP, so there is a bug
#ifdef DEBUG
    printf("[coreID: %04d || threadID: %ld] ===> In dsyscall(): REQ sent.\n", cpuid, syscall(SYS_gettid));
#endif
    while (!((requests[cpuid].flags & FL_REQ) ^ RESP_GET(responses.flags, cpuid)));
#ifdef DEBUG
    printf("[coreID: %04d || threadID: %ld] ===> In dsyscall(): RESP received.\n", cpuid, syscall(SYS_gettid));
#endif
    ret = responses.ret[cpuid];

    return ret;
}
