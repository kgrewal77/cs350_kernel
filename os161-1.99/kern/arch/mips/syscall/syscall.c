/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include "opt-A2.h"


/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */
void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err;

	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	    case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0,
				 (userptr_t)tf->tf_a1);
		break;
#ifdef UW
	case SYS_write:
	  err = sys_write((int)tf->tf_a0,
			  (userptr_t)tf->tf_a1,
			  (int)tf->tf_a2,
			  (int *)(&retval));
	  break;
	case SYS__exit:
	  sys__exit((int)tf->tf_a0);
	  /* sys__exit does not return, execution should not get here */
	  panic("unexpected return from sys__exit");
	  break;
	case SYS_getpid:
	  err = sys_getpid((pid_t *)&retval);
	  break;
	case SYS_waitpid:
	  err = sys_waitpid((pid_t)tf->tf_a0,
			    (userptr_t)tf->tf_a1,
			    (int)tf->tf_a2,
			    (pid_t *)&retval);
	  break;
#endif // UW

#if OPT_A2
        case SYS_fork:
          err = sys_fork(tf, (pid_t *)&retval);
	  break;
        case SYS_execv:
          err = sys_execv((userptr_t)tf->tf_a0,
                          (userptr_t)tf->tf_a1);
          break;
#endif

 
	default:
	  kprintf("Unknown syscall %d\n", callno);
	  err = ENOSYS;
	  break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked process.
 *
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
#if OPT_A2
void
enter_forked_process(void *trfr, int a)
{
        struct trapframe tf;
        struct trapframe *trf = trfr;
        (void)a;
        (void) trfr;
        tf.tf_status = trf->tf_status;     /* coprocessor 0 status register */
        tf.tf_cause = trf->tf_cause;      /* coprocessor 0 cause register */
        tf.tf_lo = trf->tf_lo;
        tf.tf_hi = trf->tf_hi;
        tf.tf_ra = trf->tf_ra;         /* Saved register 31 */
        tf.tf_at = trf->tf_at;         /* Saved register 1 (AT) */
        tf.tf_v0 = 0;         /* Saved register 2 (v0) */
        tf.tf_v1 = trf->tf_v1;         /* etc. */
        tf.tf_a0 = trf->tf_a0;
        tf.tf_a1 = trf->tf_a1;
        tf.tf_a2 = trf->tf_a2;
        tf.tf_a3 = 0;
        tf.tf_t0 = trf->tf_t0;
        tf.tf_t1 = trf->tf_t1;
        tf.tf_t2 = trf->tf_t2;
        tf.tf_t3 = trf->tf_t3;
        tf.tf_t4 = trf->tf_t4;
        tf.tf_t5 = trf->tf_t5;
        tf.tf_t6 = trf->tf_t6;
        tf.tf_t7 = trf->tf_t7;
        tf.tf_s0 = trf->tf_s0;
        tf.tf_s1 = trf->tf_s1;
        tf.tf_s2 = trf->tf_s2;
        tf.tf_s3 = trf->tf_s3;
        tf.tf_s4 = trf->tf_s4;
        tf.tf_s5 = trf->tf_s5;
        tf.tf_s6 = trf->tf_s6;
        tf.tf_s7 = trf->tf_s7;
        tf.tf_t8 = trf->tf_t8;
        tf.tf_t9 = trf->tf_t9;
        tf.tf_k0 = trf->tf_k0;         /* dummy (see exception.S comments) */
        tf.tf_k1 = trf->tf_k1;         /* dummy */
        tf.tf_gp = trf->tf_gp;
        tf.tf_sp = trf->tf_sp;
        tf.tf_s8 = trf->tf_s8;
        tf.tf_epc = trf->tf_epc+4; 
        kfree(trf);
        mips_usermode(&tf);
}
#else
void
enter_forked_process(struct trapframe *tf)
{
	(void)tf;
}
#endif
