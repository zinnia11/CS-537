#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  // getcount() starts at 0
  printf(1, "%d\n", getcount());

  // trace the first argument
  int fd;
  trace(argv[1]);
  fd = open(argv[1], 0);
  close(fd);
  fd = open(argv[1], 0);
  close(fd);
  fd = open(argv[1], 0);
  close(fd);
  fd = open(argv[2], 0);
  close(fd);
  // should print out 3 because file was opened 3 times
  printf(1, "%d\n", getcount());

  // trace the second argument
  trace(argv[2]);
  fd = open(argv[1], 0);
  close(fd);
  // should print out 0 because file was not opened
  printf(1, "%d\n", getcount());

  exit();
}
