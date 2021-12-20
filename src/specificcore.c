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

#include "ffwd.h"

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
    int client_cnt;
    for (client_cnt = 0; client_cnt < ncpus - 2; ++client_cnt)
        pthread_cancel(client_ids[client_cnt]);
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
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if((argc > 1) && (atoi(argv[1]) < ncpus))
	    ncpus = atoi(argv[1]);
 
    // Set the signal handler for SIGALRM
    struct sigaction alrm;
    alrm.sa_flags = 0;
    alrm.sa_handler = alrm_handler;
    sigaction(SIGALRM, &alrm, NULL);
    pthread_key_create(&tls_key, NULL);

    // Create a server core/thread
    int server_cpu = _ffwd_launch();

    // Set client CPU mask to avoid running on the same core with _ffwd_server
    long online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cpumask;
    int processor_id;
    pthread_attr_t client_attr;

    CPU_ZERO(&cpumask);
    for (processor_id = 0; processor_id < online_cpus; ++processor_id)
        CPU_SET(processor_id, &cpumask);
    CPU_CLR(server_cpu, &cpumask);
    CPU_CLR((server_cpu + 10) % 20, &cpumask);  // This is CPU-specific against the interference of SMT
    pthread_attr_setaffinity_np(&client_attr, sizeof(cpumask), &cpumask);

    // Create several client threads
    int client_cnt;
    for (client_cnt = 0; client_cnt < ncpus - 2; ++client_cnt)
        pthread_create(&client_ids[client_cnt], &client_attr, client_routine, NULL);
    
    alarm(DURATION);

    for (client_cnt = 0; client_cnt < ncpus - 2; ++client_cnt)
        pthread_join(client_ids[client_cnt], &client_rets[client_cnt]);
    
    _ffwd_shutdown();

    pthread_key_delete(tls_key);
    printf("==> CPUs: %ld, Thoughput: %lu (ops per CPU)\n\n", ncpus, (thrput.times / DURATION / ncpus));

    return 0;
}
static void set_client_affinity()
{
    int current_cpu = sched_getcpu();
    long online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cpumask;

    CPU_ZERO(&cpumask);
    sched_getaffinity(0, sizeof(cpumask), &cpumask);

    int processor_id;
    for (processor_id = 0; processor_id < online_cpus; ++processor_id) {
        if (processor_id != current_cpu)
            CPU_CLR(processor_id, &cpumask);
    }

    sched_setaffinity(0, sizeof(cpumask), &cpumask);
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

    set_client_affinity();

#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif
    
    while(1) {
        addr = (void *)_ffwd_dsyscall(SYS_mmap, 6, NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        _ffwd_dsyscall(SYS_munmap, 2, addr, 4096);
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

    set_client_affinity();

#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif
    
    while(1) {
        _ffwd_dsyscall(SYS_gettid, 0);
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

    set_client_affinity();

#ifdef DEBUG
    printf("Enter client thread: %lu, core: %d\n", syscall(SYS_gettid), sched_getcpu());
#endif

    ev.data.fd = 0;
    ev.events = EPOLLIN;
    epollfd = epoll_create(1);
    epoll_ctl(epollfd, EPOLL_CTL_ADD, 0, &ev);
    pthread_cleanup_push(epoll_cleanup_handler, &epollfd);

    while(1) {
        _ffwd_dsyscall(SYS_epoll_wait, 4, epollfd, &event, MAXEVENTS, 0);
        local_times++;
        pthread_testcancel();
    }

    pthread_cleanup_pop(epoll_cleanup_handler);
    pthread_cleanup_pop(clean_up_handler);
}