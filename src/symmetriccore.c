#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define MAXTHREADS 128
#define DURATION 10
static int ncpus;

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

void *mmap_routine(void *args)
{
    unsigned long local_time;
    void *addr;
    // long tid = syscall(SYS_gettid);
    pthread_setspecific(tls_key, &local_time);

    while(1) {
        addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        munmap(addr, 4096);
        local_time++;
        // printf("[tid: %ld | Address: %p] Area mmaped\n", tid, addr);
    }
}

// For the main thread
void alrm_handler(int sig)
{
    for (int i = 0; i < ncpus; ++i)
        pthread_kill(threads[i], SIGCHLD);
}

// For other threads except main thread
void chld_handler(int sig)
{
    pthread_mutex_lock(&thrput.lock);
    thrput.times += *(unsigned long *)pthread_getspecific(tls_key);
    pthread_mutex_unlock(&thrput.lock);
    pthread_exit(0);
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
    for (int i = 0; i < ncpus; ++i)
        pthread_create(&threads[i], NULL, mmap_routine, NULL);
    
    alarm(DURATION);

    for (int i = 0; i < ncpus; ++i)
        pthread_join(threads[i], &thread_ret[i]);
    
    pthread_key_delete(tls_key);
    printf("==> CPUs: %d, Thoughput: %lu (ops per CPU)\n", ncpus, (thrput.times / DURATION / (ncpus - 1)));

    return 0;
}
