#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


int sys_clone(void) {
  struct proc *curproc = myproc();
  void(*f)(void *, void *);
  void *a1, *a2, *s;
  if(argptr(0, (void*)&f, sizeof(f)) < 0 || argptr(1, (void*)&a1, sizeof(a1)) < 0 
              || argptr(2, (void*)&a2, sizeof(a2)) < 0 || argptr(3, (void*)&s, sizeof(s)) < 0)
    return -1;

  if ((uint) s % PGSIZE != 0) {
    return -1;
  }
  if((uint)s >= curproc->sz || (uint)s+PGSIZE > curproc->sz) {
    return -1;
  }

  return clone(f, a1, a2, s);
}

int sys_join(void) {
  void *s;
  if(argptr(0, (void*)&s, sizeof(s)) < 0)
    return -1;

  return join(s);
}
