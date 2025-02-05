/* CSE 536: User-Level Threading Library */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/ulthread.h"

/* Standard definitions */
#include <stdbool.h>
#include <stddef.h> 

struct scheduler_thread main_thread;
struct uthread *current_thread;
struct uthread *last_yielded_thread;
int scheduled_index_thread = -1;

int thread_count = 1; 
/* Get thread ID */
int get_current_tid(void) {
    return current_thread->tid;
}

/* Thread initialization */
void ulthread_init(int schedalgo) {
    for(int i=0 ; i<MAXULTHREADS; i++)
    {
	main_thread.uthreads[i].tid = -1;
	main_thread.uthreads[i].priority = -1;
	main_thread.uthreads[i].state = FREE;
	main_thread.uthreads[i].start_func = -1;
	main_thread.uthreads[i].stack_pointer = -1;
	main_thread.uthreads[i].time = -1;
    }
    main_thread.tid = 0;
    main_thread.thread_count = 0;
    main_thread.schedalgo = schedalgo;
}

/* Thread creation */
bool ulthread_create(uint64 start, uint64 stack, uint64 args[], int priority) {
    int index = -1;
    for(int i =0; i<MAXULTHREADS; i++)
    {
	if(main_thread.uthreads[i].state == FREE)
        {
	   index = i;
	   main_thread.uthreads[i].tid = thread_count;
	   main_thread.uthreads[i].priority = priority;
	   main_thread.uthreads[i].state = RUNNABLE;
	   main_thread.uthreads[i].start_func = start;
	   main_thread.uthreads[i].stack_pointer = stack;
	   main_thread.uthreads[i].time = ctime();

	   break;
	}
    }
    memset(&main_thread.uthreads[index].context, 0, sizeof(main_thread.uthreads[index].context));
    
    main_thread.uthreads[index].context.ra = start;
    main_thread.uthreads[index].context.sp = stack;

    main_thread.uthreads[index].context.a0 = args[0];
    main_thread.uthreads[index].context.a1 = args[1];
    main_thread.uthreads[index].context.a2 = args[2];
    main_thread.uthreads[index].context.a3 = args[3];
    main_thread.uthreads[index].context.a4 = args[4];
    main_thread.uthreads[index].context.a5 = args[5];

    thread_count += 1;
    
    /* Please add thread-id instead of '0' here. */
    printf("[*] ultcreate(tid: %d, ra: %p, sp: %p)\n", main_thread.uthreads[index].tid, start, stack);
    return false;
}

/* Thread scheduler */
void ulthread_schedule(void) {
    while(thread_count > 1)
    {
	if(current_thread == NULL)
	{
	  for(int i=0; i<MAXULTHREADS; i++)
	  {
	     if(main_thread.uthreads[i].state == RUNNABLE)
	     {
		current_thread = &main_thread.uthreads[i];
		//scheduled_index_thread = i;
		break;
	     }
	  }
	}
	if(main_thread.schedalgo == FCFS)
      	{
	  for(int i =0; i<MAXULTHREADS; i++)
	  {
	     if (main_thread.uthreads[i].state != RUNNABLE)
		continue;
	     
	     
	     else if(current_thread->time > main_thread.uthreads[i].time)
	     {
		current_thread = &main_thread.uthreads[i];
		//scheduled_index_thread = i;
	     }
	  }
	}
	else if(main_thread.schedalgo == ROUNDROBIN)
	{
	  for(int i=0; i<MAXULTHREADS; i++)
	  {
	     if(main_thread.uthreads[i].state != RUNNABLE)
		continue;
	     if(main_thread.uthreads[i].time < current_thread->time)
	     {
		current_thread = &main_thread.uthreads[i];
		//scheduled_index_thread = i+1;
	     }
	    
	  }
	}
	else if(main_thread.schedalgo == PRIORITY)
	{
	  for(int i=0; i<MAXULTHREADS; i++)
	  {
	     if(main_thread.uthreads[i].state != RUNNABLE)
		continue;
		
	     if(current_thread->tid == last_yielded_thread->tid && main_thread.uthreads[i].tid != last_yielded_thread->tid)
	     {
		current_thread = &main_thread.uthreads[i];
		continue;
	     }
	     if(main_thread.uthreads[i].tid != last_yielded_thread->tid && main_thread.uthreads[i].priority >= current_thread->priority)
	     {
	        current_thread = &main_thread.uthreads[i];
		//scheduled_index_thread = i;
	     }
	  }	
	 
	}

 	printf("[*] ultschedule (next tid: %d)\n", current_thread->tid);
 	ulthread_context_switch(&main_thread.context, &current_thread->context);
	
     
    } 
   // printf("Globtime Working: %d\n",ctime());  
    /* Add this statement to denote which thread-id is being scheduled next */
  	
    // Switch between thread contexts
}

/* Yield CPU time to some other thread. */
void ulthread_yield(void) {
    if(main_thread.schedalgo != FCFS)
	current_thread->time = ctime();
    
    last_yielded_thread = current_thread;
    /* Please add thread-id instead of '0' here. */
    printf("[*] ultyield(tid: %d)\n", current_thread->tid);
    ulthread_context_switch(&current_thread->context, &main_thread.context);
}

/* Destroy thread */
void ulthread_destroy(void) {
    last_yielded_thread = current_thread;
    printf("[*] ultdestroy(tid: %d)\n", current_thread->tid); 
    for(int i=0; i<MAXULTHREADS; i++)
    {
	if(main_thread.uthreads[i].tid == current_thread->tid)
	{
	   main_thread.uthreads[i].tid = -1;
           main_thread.uthreads[i].priority = -1;
           main_thread.uthreads[i].state = FREE;
           main_thread.uthreads[i].start_func = -1;
           main_thread.uthreads[i].stack_pointer = -1;
	   main_thread.uthreads[i].time = -1;
	   break;
	}
    }
    thread_count -= 1;

    ulthread_context_switch(&current_thread->context, &main_thread.context);
}
