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
#define DURATION 3

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
void alrm_handler(int sig)
{
    printf("Alarm handler from thread: %lu\n", syscall(SYS_gettid));

    for (int i = 0; i < ncpus - 2; ++i)
        pthread_kill(threads[i], SIGCHLD);
}

// For other threads except main thread
void chld_handler(int sig)
{
    printf("SIGCHLD handler from thread: %lu\n", syscall(SYS_gettid));

    pthread_mutex_lock(&thrput.lock);
    thrput.times += *(unsigned long *)pthread_getspecific(tls_key);
    pthread_mutex_unlock(&thrput.lock);
    printf("aaa from thread: %lu\n", syscall(SYS_gettid));
    pthread_exit(0);
}

void *mmap_routine(void *args)
{
    unsigned long local_time;
    void *addr;
    // long tid = syscall(SYS_gettid);
    pthread_setspecific(tls_key, &local_time);

    // for (int i = 0; i < 10000; ++i) {
    while(1) {
        addr = (void *)dsyscall(SYS_mmap, 6, NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        dsyscall(SYS_munmap, 2, addr, 4096);
        local_time++;
        // printf("[tid: %ld | Address: %p] Area mmaped\n", tid, addr);
    }
    // printf("aaa: %lu\n", pthread_self());
    // chld_handler(SIGCHLD);
}

int main(int argc, char **argv)
{
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    
    // Set the signal handler for SIGALRM && SIGCHLD
    struct sigaction alrm, chld;
    alrm.sa_flags = 0;
    alrm.sa_handler = alrm_handler;
    sigaction(SIGALRM, &alrm, NULL);
    chld.sa_flags = 0;
    chld.sa_handler = chld_handler;
    sigaction(SIGCHLD, &chld, NULL);
    pthread_key_create(&tls_key, NULL);

    pthread_t serverid;
    void *serverret;
    pthread_attr_t server_attr, client_attr;
    cpu_set_t server_mask, client_mask;

    int cpuid = sched_getcpu();
    long thread_id = syscall(SYS_gettid);
    printf("[coreID: %04x || threadID: %ld] ===> Main thread\n", cpuid, thread_id);

    CPU_ZERO(&server_mask);
    CPU_SET(cpuid, &server_mask);
    sched_getaffinity(thread_id, sizeof(cpu_set_t), &client_mask);
    CPU_CLR(cpuid, &client_mask);

    // const cpu_set_t server_cpus = server_mask, client_cpus = client_mask;
    // pthread_attr_setaffinity_np(&server_attr, sizeof(cpu_set_t), &server_cpus);
    // pthread_attr_setaffinity_np(&client_attr, sizeof(cpu_set_t), &client_cpus);

    pthread_create(&serverid, NULL, server_routine, NULL);
    sleep(1);
    // pthread_create(&serverid, &server_attr, server_routine, NULL);
    for (int i = 0; i < ncpus - 2; ++i)
        pthread_create(&threads[i], NULL, mmap_routine, NULL);
    
    alarm(DURATION);

    for (int i = 0; i < ncpus - 2; ++i)
        pthread_join(threads[i], &thread_ret[i]);
    // pthread_join(serverid, &serverret);

    pthread_key_delete(tls_key);
    printf("==> Threads: %ld, Thoughput: %lu (ops)\n", ncpus - 1, thrput.times / DURATION);

    return 0;
}
