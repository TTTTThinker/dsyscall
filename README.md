# dsyscall
A library for dedicate core to specific syscalls

In this project, we compare the throughput of syscalls in three conditions:
  + singlecore, which means there is only one core to do a specific syscall
  + symmetriccore, which means there are several cores to do a specific syscall concurrently
  + specificcore, which means dedicate one server core to do a specific syscall(like kernel core in corey[OSDI'08])

For specificcore, we use a message passing way like ffwd[SOSP'17] to reduce the cache coherence traffic.
  
To build this project, you can:  
> make (or, make docker)  
To run this project, you can:  
> make single/symmetric/specific [NCPUS=]  

NOTE: It can only support systems have no more than 32 logical CPUS!
