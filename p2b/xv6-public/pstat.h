#ifndef _PSTAT_H_
#define _PSTAT_H_

#include "param.h"

struct pstat {
  int inuse[NPROC];   // whether this slot of the process table is in use (1 or 0)
  int tickets[NPROC]; // priority of this process (1 or 0)
  int pid[NPROC];     // the PID of each process 
  int ticks[NPROC];   // the number of ticks representing how long the process has run
};

#endif // _PSTAT_H_