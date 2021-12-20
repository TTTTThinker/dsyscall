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
#include <sys/epoll.h>
#include <sys/syscall.h>

#define MAXCPUS 32
#define MAXCLIENTS (MAXCPUS - 1)
#define DURATION 1

typedef void *(*routine_t)(void *);
typedef struct throughput {
    unsigned long times;
    pthread_mutex_t lock;
} throughput_t;

pthread_t client_ids[MAXCLIENTS];
pthread_key_t tls_key;
void *client_rets[MAXCLIENTS];

static long ncpus;
throughput_t thrput = {
    .times  = 0,
    .lock   = PTHREAD_MUTEX_INITIALIZER
};

// For the main thread
static void alrm_handler(int sig)
{
#ifdef DEBUG
    printf("SIGALRM handler called from thread: %lu\n", syscall(SYS_gettid));
#endif
    pthread_cancel(client_ids[0]);
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

void *mmap_routine(void *args);
void *gettid_routine(void *args);
void *epoll_wait_routine(void *args);

routine_t client_routine = mmap_routine;

int main(int argc, char **argv)
{
    ncpus = 1;
    
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

    pthread_create(&client_ids[0], NULL, client_routine, NULL);
    
    alarm(DURATION);

    pthread_join(client_ids[0], &client_rets[0]);
    
    pthread_key_delete(tls_key);
    printf("==> CPUs: %ld, Thoughput: %lu (ops per CPU)\n", ncpus, (thrput.times / DURATION));

    return 0;
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

void *gettid_routine(void *args)
{
    unsigned long local_times;
    int cancelstate, canceltype;
    
    pthread_setspecific(tls_key, &local_times);
    pthread_cleanup_push(clean_up_handler, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);

#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif
    
    while(1) {
        syscall(SYS_gettid);
        local_times++;
        pthread_testcancel();
    }
    pthread_cleanup_pop(clean_up_handler);
}

static void epoll_cleanup_handler(void *epfd)
{
    int epollfd = *(int *)epfd;
    close(epollfd);
}

void *epoll_wait_routine(void *args)
{
    unsigned long local_times;
    int cancelstate, canceltype;
    int epollfd, MAXEVENTS = 1;
    struct epoll_event ev, event;
    
    pthread_setspecific(tls_key, &local_times);
    pthread_cleanup_push(clean_up_handler, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);

#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif

    ev.data.fd = 0;
    ev.events = EPOLLIN;
    epollfd = epoll_create(1);
    epoll_ctl(epollfd, EPOLL_CTL_ADD, 0, &ev);
    pthread_cleanup_push(epoll_cleanup_handler, &epollfd);

    while(1) {
        syscall(SYS_epoll_wait, epollfd, &event, MAXEVENTS, 0);
        local_times++;
        pthread_testcancel();
    }

    pthread_cleanup_pop(epoll_cleanup_handler);
    pthread_cleanup_pop(clean_up_handler);
}