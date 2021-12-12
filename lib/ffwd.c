#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "ffwd.h"

struct request _ffwd_requests[MAXCPUS - 1];
struct response _ffwd_responses;
bool _ffwd_running = false;
pthread_t _ffwd_serverid;

static void clean_up_handler(void)
{
    int cpuid = sched_getcpu();
    long threadid = syscall(SYS_gettid);
    printf("[coreID: %4d || threadID: %ld] ===> Server routine exiting ...\n", cpuid, threadid);
}

static void *_ffwd_server_routine(void *args)
{
    bool has_requests = false;
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
        re.flags = _ffwd_responses.flags;
        int cpu;
        for (cpu = 0; cpu < MAXCPUS - 1; ++cpu) {
            if ((_ffwd_requests[cpu].flags & FL_REQ) ^ (_ffwd_responses.flags & FL_RESP(cpu))) {
                has_requests = true;
                switch (_ffwd_requests[cpu].argc)
                {
                case 0:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid);
                    break;
                case 1:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid, _ffwd_requests[cpu].argv[0]);
                    break;
                case 2:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid, _ffwd_requests[cpu].argv[0], _ffwd_requests[cpu].argv[1]);
                    break;
                case 3:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid, _ffwd_requests[cpu].argv[0], _ffwd_requests[cpu].argv[1], _ffwd_requests[cpu].argv[2]);
                    break;
                case 4:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid, _ffwd_requests[cpu].argv[0], _ffwd_requests[cpu].argv[1], _ffwd_requests[cpu].argv[2], _ffwd_requests[cpu].argv[3]);
                    break;
                case 5:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid, _ffwd_requests[cpu].argv[0], _ffwd_requests[cpu].argv[1], _ffwd_requests[cpu].argv[2], _ffwd_requests[cpu].argv[3], _ffwd_requests[cpu].argv[4]);
                    break;
                case 6:
                    re.ret[cpu] = syscall(_ffwd_requests[cpu].syscallid, _ffwd_requests[cpu].argv[0], _ffwd_requests[cpu].argv[1], _ffwd_requests[cpu].argv[2], _ffwd_requests[cpu].argv[3], _ffwd_requests[cpu].argv[4], _ffwd_requests[cpu].argv[5]);
                    break;
                
                default:
                    break;
                }
                RESP_REVERSE(re.flags, cpu);
            }
        }
        if (has_requests) {
            // Now, it's just at this moment the syscall responses are sent to the clients
            _ffwd_responses = re;

            has_requests = false;
        }
#ifdef DEBUG
    printf("[coreID: %4d || threadID: %ld] ===> A round finished.\n", cpuid, threadid);
#endif
    }
    pthread_cleanup_pop(clean_up_handler);
}

int _ffwd_launch()
{
    int ret = 0;
    if (_ffwd_running == false) {
        ret = pthread_create(&_ffwd_serverid, NULL, _ffwd_server_routine, NULL);
        _ffwd_running = (ret < 0) ? false : true;
    }
    return ret;
}

void _ffwd_shutdown()
{
    void *ret;
    if (_ffwd_running == true) {
        pthread_cancel(_ffwd_serverid);
        pthread_join(_ffwd_serverid, &ret);
        _ffwd_running = false;
    }
}

long _ffwd_dsyscall(long syscallno, int argc, ...)
{
    struct request req = {
        .syscallid = syscallno,
        .argc = argc
    };
    long ret;

    va_list args;
    va_start(args, argc);
    int arg_counter;
    for (arg_counter = 0; arg_counter < argc; ++arg_counter)
        req.argv[arg_counter] = va_arg(args, long);
    va_end(args);
    
    int cpuid = sched_getcpu();
    while ((_ffwd_requests[cpuid].flags & FL_REQ) ^ RESP_GET(_ffwd_responses.flags, cpuid));
    req.flags = _ffwd_requests[cpuid].flags;
    REQ_REVERSE(req.flags);

    // Now, it's just at this moment the syscall request is sent to the server
    _ffwd_requests[cpuid] = req;

    // Maybe context switch between REQ & RESP, so there is a bug !!!
#ifdef DEBUG
    printf("[coreID: %04d || threadID: %ld] ===> In dsyscall(): REQ sent.\n", cpuid, syscall(SYS_gettid));
#endif
    while (!((_ffwd_requests[cpuid].flags & FL_REQ) ^ RESP_GET(_ffwd_responses.flags, cpuid)));
#ifdef DEBUG
    printf("[coreID: %04d || threadID: %ld] ===> In dsyscall(): RESP received.\n", cpuid, syscall(SYS_gettid));
#endif
    ret = _ffwd_responses.ret[cpuid];

    return ret;
}
