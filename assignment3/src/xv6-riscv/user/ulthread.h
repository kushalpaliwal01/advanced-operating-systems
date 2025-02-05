#ifndef __UTHREAD_H__
#define __UTHREAD_H__

#include <stdbool.h>

#define MAXULTHREADS 100

enum ulthread_state {
  FREE,
  RUNNABLE,
  YIELD,
};

enum ulthread_scheduling_algorithm {
  ROUNDROBIN,   
  PRIORITY,     
  FCFS,         // first-come-first serve
};

struct context{
  uint64 ra;
  uint64 sp;
  
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;

};

struct uthread{
  int tid;					// Thread ID   
  int priority;					// Priority of the thread
  uint64 time;
  uint64 start_func;				// Function Start Address		
  uint64 stack_pointer;				// Thread's Stack base address
  enum ulthread_state state;			// Free, Runnable or Yield
  struct context context;

};

struct scheduler_thread{
  int tid;
  int thread_count;
  struct uthread uthreads[MAXULTHREADS];
  struct context context;
  enum ulthread_scheduling_algorithm schedalgo;
};
#endif
