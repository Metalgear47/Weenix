#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h> 
#include <sys/types.h>

int main(int argc, char *argv[], char *envp[]) {
  open("/dev/tty0", O_RDONLY, 0);
  open("/dev/tty0", O_WRONLY, 0);
  printf("pid %d: Entering forktest\n", getpid());
  int pid = fork();
  printf("pid %d: Fork returned %d\n", getpid(), pid);
  printf("pid %d: About to enter waitpid\n", getpid());
  int rc = waitpid(-1, 0, 0);
  printf("pid %d: Waitpid returned %d\n", getpid(), rc);
  printf("pid %d: Exiting\n", getpid());
  return 0;
}
