#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "ffwd.h"

static long ncpus;

#define MAXTHREADS 32
#define DURATION 10

pthread_t threads[MAXTHREADS];
void *thread_ret[MAXTHREADS];
pthread_key_t tls_key;

struct throughput {
    unsigned long times;
    pthread_mutex_t lock;
};
struct throughput thrput = {
    .times  = 0,
    .lock   = PTHREAD_MUTEX_INITIALIZER
};

// For the main thread
static void alrm_handler(int sig)
{
#ifdef DEBUG
    printf("SIGALRM handler called from thread: %lu\n", syscall(SYS_gettid));
#endif
    for (int i = 0; i < ncpus - 2; ++i)
        pthread_cancel(threads[i]);
}

// For client thread
static void clean_up_handler(void)
{
#ifdef DEBUG
    printf("Pthread clean_up handler called from thread: %lu\n", syscall(SYS_gettid));
#endif
    pthread_mutex_lock(&thrput.lock);
    thrput.times += *(unsigned long *)pthread_getspecific(tls_key);
    pthread_mutex_unlock(&thrput.lock);
}

void *mmap_routine(void *args)
{
    unsigned long local_times;
    void *addr;
    int cancelstate, canceltype;
    
    pthread_setspecific(tls_key, &local_times);
    pthread_cleanup_push(clean_up_handler, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);
/*
    int cpuid = sched_getcpu();
    int threadid = syscall(SYS_gettid);
    cpu_set_t cpumask;
    CPU_ZERO(&cpumask);
    CPU_SET(cpuid, &cpumask);
    sched_setaffinity(threadid, sizeof(cpu_set_t), &cpumask);
*/
#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif
    
    while(1) {
        addr = (void *)dsyscall(SYS_mmap, 6, NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        dsyscall(SYS_munmap, 2, addr, 4096);
        local_times++;
        pthread_testcancel();
    }
    pthread_cleanup_pop(clean_up_handler);
}

void *gettid_routine(void *args)
{
    unsigned long local_times;
    int cancelstate, canceltype;
    
    pthread_setspecific(tls_key, &local_times);
    pthread_cleanup_push(clean_up_handler, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);
/*
    int cpuid = sched_getcpu();
    int threadid = syscall(SYS_gettid);
    cpu_set_t cpumask;
    CPU_ZERO(&cpumask);
    CPU_SET(cpuid, &cpumask);
    sched_setaffinity(threadid, sizeof(cpu_set_t), &cpumask);
*/
#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif
    
    while(1) {
        dsyscall(SYS_gettid, 0);
        local_times++;
        pthread_testcancel();
    }
    pthread_cleanup_pop(clean_up_handler);
}

int main(int argc, char **argv)
{
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if((argc > 1) && (atoi(argv[1]) < ncpus))
	ncpus = atoi(argv[1]);
 
    // Set the signal handler for SIGALRM
    struct sigaction alrm;
    alrm.sa_flags = 0;
    alrm.sa_handler = alrm_handler;
    sigaction(SIGALRM, &alrm, NULL);
    pthread_key_create(&tls_key, NULL);

    pthread_t serverid;
    void *serverret;

#ifdef DEBUG
    int cpuid = sched_getcpu();
    long threadid = syscall(SYS_gettid);
    printf("[coreID: %4d || threadID: %ld] ===> Main thread\n", cpuid, threadid);
#endif
/*
    int server_cpuid = (sched_getcpu() + 1) % ncpus;
    pthread_attr_t server_attr;
    cpu_set_t server_mask;
    CPU_ZERO(&server_mask);
    CPU_SET((server_cpuid + 1) % ncpus, &server_mask);

    const cpu_set_t server_cpus = server_mask;
    pthread_attr_setaffinity_np(&server_attr, sizeof(cpu_set_t), &server_cpus);
    pthread_create(&serverid, &server_attr, server_routine, NULL);
*/    
    pthread_create(&serverid, NULL, server_routine, NULL);
    for (int i = 0; i < ncpus - 2; ++i)
        pthread_create(&threads[i], NULL, mmap_routine, NULL);
    
    alarm(DURATION);

    for (int i = 0; i < ncpus - 2; ++i)
        pthread_join(threads[i], &thread_ret[i]);
    
    pthread_cancel(serverid);
    pthread_join(serverid, &serverret);

    pthread_key_delete(tls_key);
    printf("==> CPUs: %ld, Thoughput: %lu (ops per CPU)\n", ncpus, (thrput.times / DURATION / (ncpus - 1)));

    return 0;
}
