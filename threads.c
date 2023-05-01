#include "threads.h"
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ucontext.h>
#include <sys/ucontext.h>

/* You can support more threads. At least support this many. */
#define MAX_THREADS 128

/* Your stack should be this many bytes in size */
#define THREAD_STACK_SIZE 32767

/* Number of microseconds between scheduling events */
#define SCHEDULER_INTERVAL_USECS (50 * 1000)

/* Extracted from private libc headers. These are not part of the public
 * interface for jmp_buf.
 */
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7
#define REG_RAX 15
/* thread_status identifies the current state of a thread. You can add, rename,
 * or delete these values. This is only a suggestion. */
enum thread_status
{
	TS_EXITED,
	TS_RUNNING,
	TS_READY,
	TS_STANDBY
};

/* The thread control block stores information about a thread. You will
 * need one of this per thread.
 */
struct thread_control_block {
	pthread_t thread_id;
	void* stack;
	enum thread_status state;
	jmp_buf regs;
};

struct thread_control_block thread_list[MAX_THREADS];
pthread_t TID = 0;
struct sigaction signal_handler;

static void schedule()
{
	int ljump = 0;

	if(thread_list[TID].state == TS_RUNNING)
	{
		thread_list[TID].state = TS_READY;
	}
	else if (thread_list[TID].state == TS_EXITED || thread_list[TID].state == TS_STANDBY || thread_list[TID].state == TS_READY)
	{
			
	}
	
	pthread_t tid = TID;

	while(1)
	{
		if(tid == MAX_THREADS - 1)
		{
			tid = 0;
		}
		else
		{
			tid++;
		}
		if(thread_list[tid].state == TS_READY)
		{
			break;
		}
	}

	if(thread_list[TID].state != TS_EXITED)
	{
		ljump = setjmp(thread_list[TID].regs);
	}
	
	if(ljump == 0)
	{
		TID = tid;
		thread_list[TID].state = TS_RUNNING;
		longjmp(thread_list[TID].regs, 1);
	}
}

static void scheduler_init()
{
	for(int i = 0; i < MAX_THREADS; i++)
	{
		thread_list[i].state = TS_STANDBY;
		thread_list[i].thread_id = i;
	}

	struct itimerval interval;
	interval.it_value.tv_sec = 0;
	interval.it_value.tv_usec = SCHEDULER_INTERVAL_USECS;
	interval.it_interval.tv_sec = 0;
	interval.it_interval.tv_usec = SCHEDULER_INTERVAL_USECS;
	setitimer(ITIMER_REAL, &interval, NULL);
	
	signal_handler = (struct sigaction)
	{
		.sa_handler = &schedule,
		.sa_flags = SA_NODEFER,
	};
	sigemptyset(&signal_handler.sa_mask);
	sigaction(SIGALRM, &signal_handler, NULL);
}

void pthread_save()
{
	union ucontext 
	{
		void * ptrs[32];
		int ints[32];
	};

	ucontext_t context;
	getcontext(&context);

	void * reg = (void*) context.uc_mcontext.gregs[REG_RAX];
	pthread_exit(reg);
}



int pthread_create(
	pthread_t *thread, const pthread_attr_t *attr,
	void *(*start_routine) (void *), void *arg)
{
	static bool is_first_call = true;
	attr = NULL;
	int first_thread = 0;
	if (is_first_call)
	{
		scheduler_init();
		thread_list[0].state = TS_READY;
		first_thread = setjmp(thread_list[0].regs);
		is_first_call = false;
	}
	
	if(first_thread == 0)
	{
		pthread_t thread_tid = 1;
		for(; thread_tid < MAX_THREADS; thread_tid++)
		{
			if(thread_list[thread_tid].state == TS_STANDBY)
			{
				*thread = thread_tid;
				break;
			}
		}
		
		if(thread_tid == MAX_THREADS)
		{
			fprintf(stderr, "MAX_THREADS REACHED, EXITING\n");
			return -1;
		}	

		setjmp(thread_list[thread_tid].regs);

		thread_list[thread_tid].stack = malloc(THREAD_STACK_SIZE);
		void * stack_ptr = thread_list[thread_tid].stack + THREAD_STACK_SIZE;
		stack_ptr = stack_ptr - sizeof(&pthread_save);
		void (*save)(void*) = (void*) &pthread_save;
		stack_ptr = memcpy(stack_ptr, &save, sizeof(save));

		asm volatile (
				"movq %0, %%rax\n\t"
				"movq %%rax, %1"
				:
				: "r" (ptr_mangle((unsigned long int) stack_ptr)), "m" (thread_list[thread_tid].regs[0].__jmpbuf[JB_RSP])
				: "rax", "memory"
			     );
			
		asm volatile (
				"movq %0, %%rax\n\t"
				"movq %%rax, %1;\n\t"
				:
				: "rm" (ptr_mangle((unsigned long int)start_thunk)), "m" (thread_list[thread_tid].regs[0].__jmpbuf[JB_PC])
				: "%rax"
		   	     );

		asm volatile (
				"movq %0, %%rax\n\t"
				"movq %%rax, %p1"
				:
				: "r"((long) arg), "m"(thread_list[thread_tid].regs[0].__jmpbuf[JB_R13])
				: "rax", "memory"
			     );

		asm volatile (
				"movq %0, %%rax\n\t"
				"movq %%rax, %p1"
				: 
				: "r" ((unsigned long int) start_routine), "m" (thread_list[thread_tid].regs[0].__jmpbuf[JB_R12])
				: "%rax"
			     );
	
		thread_list[thread_tid].state = TS_READY;
		thread_list[thread_tid].thread_id = thread_tid;
		schedule();
	}

	else
	{	
		first_thread = 0; 
	}

	return 0;
}

void pthread_exit(void *value_ptr)
{

	pthread_t tid = thread_list[TID].thread_id;
	thread_list[TID].state = TS_EXITED;

	if(tid != TID)
	{
		thread_list[tid].state = TS_READY;
	}

	int remaining_threads = 0;
	for(int i = 0; i < MAX_THREADS; i++)
	{
		if(thread_list[i].state == TS_READY || thread_list[i].state == TS_RUNNING)
		{
			remaining_threads = 1;
		}
		else
		{
			continue;
		}
	}

	if(remaining_threads > 0)
	{
		schedule();
	}	

	for(int i = 0; i < MAX_THREADS; i++)
	{
		if(thread_list[i].state == TS_EXITED)
		{
			free(thread_list[i].stack);
		}
	}

	exit(0);
}

pthread_t pthread_self(void)
{
	return TID;
}

