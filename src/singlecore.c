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
    pthread_cancel(threads[0]);
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

#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif
    
    while(1) {
        addr = (void *)syscall(SYS_mmap, NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        syscall(SYS_munmap, addr, 4096);
        local_times++;
        pthread_testcancel();
    }
    pthread_cleanup_pop(clean_up_handler);
}

int main(int argc, char **argv)
{
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if ((argc > 1) && (atoi(argv[1]) < ncpus))
	ncpus = atoi(argv[1]);
    
    // Set the signal handler for SIGALRM
    struct sigaction alrm;
    alrm.sa_flags = 0;
    alrm.sa_handler = alrm_handler;
    sigaction(SIGALRM, &alrm, NULL);
    pthread_key_create(&tls_key, NULL);

#ifdef DEBUG
    int cpuid = sched_getcpu();
    long threadid = syscall(SYS_gettid);
    printf("[coreID: %4d || threadID: %ld] ===> Main thread\n", cpuid, threadid);
#endif
/*
    pthread_attr_t server_attr, client_attr;
    cpu_set_t server_mask, client_mask;
    CPU_ZERO(&server_mask);
    CPU_SET(cpuid, &server_mask);
    sched_getaffinity(thread_id, sizeof(cpu_set_t), &client_mask);
    CPU_CLR(cpuid, &client_mask);

    const cpu_set_t server_cpus = server_mask, client_cpus = client_mask;
    pthread_attr_setaffinity_np(&server_attr, sizeof(cpu_set_t), &server_cpus);
    pthread_attr_setaffinity_np(&client_attr, sizeof(cpu_set_t), &client_cpus);

    pthread_create(&serverid, &server_attr, server_routine, NULL);
*/
    pthread_create(&threads[0], NULL, mmap_routine, NULL);
    
    alarm(DURATION);

    pthread_join(threads[0], &thread_ret[0]);
    
    pthread_key_delete(tls_key);
    printf("==> CPUs: %ld, Thoughput: %lu (ops per CPU)\n", ncpus, (thrput.times / DURATION));

    return 0;
}
