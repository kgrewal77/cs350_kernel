#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"
#if OPT_A2
#include <limits.h>
#include <machine/trapframe.h>
#endif

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
#if OPT_A2

struct pidTable *create_pidTable(void){
  struct pidTable* pt;
  pt = kmalloc(sizeof(struct pidTable));
  if (pt == NULL) {
    return NULL;
  }
  pt->numprocs = 0;
  
  pt->table = kmalloc(sizeof(struct pidEntry *)*PID_MAX);
  if (pt->table == NULL) {
     kfree(pt);
     return NULL;
  }
  //for (int i = 1;i<=PID_MAX;i++){
  //  pt->table[i] = kmalloc(sizeof(struct pidEntry));
  //}
  
  spinlock_init(&pt->p_spinlock);
  return pt;
  
  
}
int add_pidEntry(struct pidTable *ptable, struct proc *target, struct proc *child, struct proc *parent){
  spinlock_acquire(&ptable->p_spinlock);
  if (ptable->numprocs > PID_MAX){
    return ENPROC;
  }
  struct pidEntry *pe;
  pe = kmalloc(sizeof(struct pidEntry));
  pe->thisProc = target;
  pe->child = child;
  pe->parent = parent;
  //for (int i = 1; i<= PID_MAX;i++){
  //  if (ptable->table[i]){
  pe->pid = ptable->numprocs +1;
  ptable->table[pe->pid] = pe;
  ptable->numprocs += 1;
  spinlock_release(&ptable->p_spinlock);
  return pe->pid;
    //}
  //}
  spinlock_release(&ptable->p_spinlock);
  panic("pid table isnt full but no space for new entry");
  return -1;
}

void remove_pidEntry(struct pidTable *ptable, int pid){
  spinlock_acquire(&ptable->p_spinlock);
  if (ptable->table[pid]){
    return;
  }
  kfree(ptable->table[pid]);
  ptable->numprocs -= 1;
  spinlock_release(&ptable->p_spinlock);
}


int sys_fork(struct trapframe *tf, pid_t *retval) {
  
  struct proc *child = proc_create_runprogram("child");
  if (child == NULL) {
    return ENOMEM;
  }

  struct addrspace *newas;
  int a_result = as_copy(curproc_getas(), &newas);

  if (a_result) {
    kprintf("addrspace copy failed: %s\n", strerror(a_result));
    kfree(newas);
    proc_destroy(child);
    return a_result;
  }


  spinlock_acquire(&child->p_lock);
  child->p_addrspace = newas;
  spinlock_release(&child->p_lock);

  int pid = add_pidEntry(PID_TABLE, child, NULL, curproc);
  if (pid == ENPROC) {
    kprintf("pidTable addition failed: %s\n", strerror(pid));
    proc_destroy(child);
    return pid;
  }
  struct trapframe *trfr;
  trfr = kmalloc(sizeof(struct trapframe));
  trfr->tf_status = tf->tf_status;     /* coprocessor 0 status register */
  trfr->tf_cause = tf->tf_cause;      /* coprocessor 0 cause register */
  trfr->tf_lo = tf->tf_lo;
  trfr->tf_hi = tf->tf_hi;
  trfr->tf_ra = tf->tf_ra;         /* Saved register 31 */
  trfr->tf_at = tf->tf_at;         /* Saved register 1 (AT) */
  trfr->tf_v0 = 0;         /* Saved register 2 (v0) */
  trfr->tf_v1 = tf->tf_v1;         /* etc. */
  trfr->tf_a0 = tf->tf_a0;
  trfr->tf_a1 = tf->tf_a1;
  trfr->tf_a2 = tf->tf_a2;
  trfr->tf_a3 = 0;
  trfr->tf_t0 = tf->tf_t0;
  trfr->tf_t1 = tf->tf_t1;
  trfr->tf_t2 = tf->tf_t2;
  trfr->tf_t3 = tf->tf_t3;
  trfr->tf_t4 = tf->tf_t4;
  trfr->tf_t5 = tf->tf_t5;
  trfr->tf_t6 = tf->tf_t6;
  trfr->tf_t7 = tf->tf_t7;
  trfr->tf_s0 = tf->tf_s0;
  trfr->tf_s1 = tf->tf_s1;
  trfr->tf_s2 = tf->tf_s2;
  trfr->tf_s3 = tf->tf_s3;
  trfr->tf_s4 = tf->tf_s4;
  trfr->tf_s5 = tf->tf_s5;
  trfr->tf_s6 = tf->tf_s6;
  trfr->tf_s7 = tf->tf_s7;
  trfr->tf_t8 = tf->tf_t8;
  trfr->tf_t9 = tf->tf_t9;
  trfr->tf_k0 = tf->tf_k0;         /* dummy (see exception.S comments) */
  trfr->tf_k1 = tf->tf_k1;         /* dummy */
  trfr->tf_gp = tf->tf_gp;
  trfr->tf_sp = tf->tf_sp;
  trfr->tf_s8 = tf->tf_s8;
  trfr->tf_epc = tf->tf_epc;


  int result = thread_fork("forked thread",
                       child,
                       (void *)enter_forked_process,
                       (void *)trfr, 0);
  if (result) {
    kprintf("thread_fork failed: %s\n", strerror(result));
    proc_destroy(child);
    return result;
  }
  *retval = pid;
  return 0;

}
#endif

void sys__exit(int exitcode) {
  
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
  for (int i = 1;i<=PID_TABLE->numprocs;i++){
    if (curproc == PID_TABLE->table[i]->thisProc){
      *retval = PID_TABLE->table[i]->pid;
      return 0;
    }
  }
  panic("get_pid failed to get current proc from PID_TABLE");
  return -1;
#else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
#endif
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
