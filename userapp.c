#include <stdio.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "userapp.h"

#define PROC_FILENAME "//proc/mp2/status"

unsigned long long factorial(int number)
{
    unsigned long long retval=1;
    unsigned long long i;

    for(i = 1; i <= number; i++)
    {
        retval = retval * i;
    }

    return retval;
}

int main(int argc, char* argv[])
{
    char cmd[120];
    pid_t mypid;
    int j,k;
    char line[120];
    FILE *file;
    unsigned long int pid, period, computation;
    struct timeval last_tv, current_tv;

    mypid= syscall(__NR_gettid);
    sprintf(cmd, "echo 'R, %lu, 500, 10'>" PROC_FILENAME, mypid);
    system(cmd);

    file = fopen(PROC_FILENAME, "r");
    while(fgets(line, sizeof(line), file) != NULL)
    {
        sscanf(line, "%lu: %lu %lu\n", &pid, &period, &computation);
        if(pid == mypid)
            break;
    }
    fclose(file);

    if(pid != mypid)
    {
        printf("Could not schedule our task\n");
        return -1;
    }

    sprintf(cmd, "echo 'Y, %lu'>" PROC_FILENAME, mypid);
    gettimeofday(&last_tv, NULL);
    system(cmd);
    gettimeofday(&current_tv, NULL);

    for(k = 0; k < 2; k++)
        for(j = 0; j < 10; j++)
        {
            printf("% 4ld sec % 5ld us | fact(%u): %llu\n",
                    current_tv.tv_sec - last_tv.tv_sec,
                    current_tv.tv_usec - last_tv.tv_usec,
                    j, factorial(j));

            sprintf(cmd, "echo 'Y, %lu'>" PROC_FILENAME, mypid);
            gettimeofday(&last_tv, NULL);
            system(cmd);
            gettimeofday(&current_tv, NULL);
        }

    sprintf(cmd, "echo 'D, %lu'>" PROC_FILENAME, mypid);
    system(cmd);

    return 0;
}
