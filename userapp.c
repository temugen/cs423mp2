#include <stdio.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include "userapp.h"

unsigned long long factorial(int number)
{
  unsigned long long retval=1;
  unsigned long long i;

  for (i=1; i <= number; i++)
  {
    retval=retval *i;
  }

  return retval;
}

int main(int argc, char* argv[])
{
  char cmd[120];
  pid_t mypid;
  int j,k;

  mypid= syscall(__NR_gettid);
  sprintf(cmd, "echo 'R, %u, 2000, 10'>//proc/mp2/status", mypid);
  system(cmd);

  struct timeval tv;

  for (k=0;k<10000; k++)
    for(j=0; j<30; j++)
    {
        gettimeofday(&tv, NULL);
        printf("%llu %llu\n", tv.tv_sec, tv.tv_usec);
        printf("Factorial %u: %llu\n",j, factorial(j));
        sprintf(cmd, "echo 'Y, %u'>//proc/mp2/status", mypid);
        system(cmd);
    }
}
