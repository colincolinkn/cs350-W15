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
#include <mips/trapframe.h>
#include <synch.h>
#include <array.h>
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

#if OPT_A2
int sys_fork(struct trapframe* tf, pid_t *retval) {
   struct proc *new_proc;
   new_proc = proc_create_runprogram(curproc->p_name);
   if (new_proc == NULL) {
     panic("cannot create new process");
   }
   new_proc->p_pid = curproc->pid;
   int err;
   err = as_copy(curproc->p_addrspace, &new_proc->p_addrspace);
   if (err) {
    
      return err;
   }
   
   if (curproc->children == NULL) {
     panic("invalid children array!");
   }
   err = array_add(curproc->children, (int *)new_proc->pid, NULL);
   if (err) {

      return err;
   }   

   struct trapframe *tf_cp = NULL;

   tf_cp = kmalloc(sizeof(struct trapframe));
   if (tf_cp == NULL) {
      panic("no enough mem");
   }
   memcpy(tf_cp, tf, sizeof(struct trapframe));
   
   err = thread_fork("child", new_proc, enter_forked_process, tf_cp, 0);
   if (err) {

      return err;
   }
   *retval = new_proc->pid;
   return 0;
}
#else
#endif /* OPT_A2 */

void sys__exit(int exitcode) {
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
  #if OPT_A2
  if (p->p_pid < 0) {// no parent
    p_table[p->pid].proc = NULL;
  }  
  p_table[p->pid].exit_code = _MKWAIT_EXIT(exitcode);
  V(p_table[p->pid].proc_sem);
  
  #endif /* OPT_A2 */ 
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
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A2
  *retval = curproc->pid;
  #else
  *retval = 1;
  #endif /* OPT_A2 */
  return(0);
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
  #if OPT_A2
  if ((unsigned)pid > PID_MAX) {
    return ESRCH;
  }
  if ((int)array_num(curproc->children) == 0) {// no parent
    return ESRCH;
  }
  int existence = 0;
  int i;
  for (i = 0; i < PID_MAX; i++) {
    if (pid == (int)array_get(curproc->children, i)) {
       existence = 1; break;
    }
  }
  if (existence == 0) {
    return ECHILD;
  }
  array_remove(curproc->children, i);
  
  P(p_table[pid].proc_sem);
  if(p_table[pid].exit_code > -1) {
    exitstatus =p_table[pid].exit_code;
    p_table[pid].exit_code = -1;
  } else {
    exitstatus = 0;
  }
  
  #else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  #endif /* OPT_A2 */
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

