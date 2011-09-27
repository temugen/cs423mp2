#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "userapp.h"

long long factorial(int number)
{
  long long retval=1;
  long long i;
  
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
  sprintf(cmd, "echo '%u'>//proc/mp1/status", mypid);
  system(cmd);
 
  for (k=0;k<10000; k++)
  for(j=0; j<30; j++)
  {
	printf("Factorial %u: %llu\n",j, factorial(j));
  }
}
