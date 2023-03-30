#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int
main(int argc, char *argv[])
{
    /*Write a test program that:
        (a) Forks 3 processes with a low, normal and high priority respectively.
        (b) Each child should perform a simple loop of 1,000,000 iterations,
        sleeping for 1 second every 100,000 iterations.
        (c) Each child process should then print its PID, CFS priority, run
        time, sleep time, and runnable time. 
    */
    int status=0;
    int pid = fork();
    int *stats = malloc(4*(sizeof(int)));
    if (pid == 0) {
        // child
        set_cfs_priority(0);
        int i;
        for (i = 0; i < 1000000; i++) {
            if (i % 100000 == 0) {
                sleep(1);
            }
        }
        get_cfs_stats(getpid(), stats);
        //printf("%d\t\t%d\t\t%d\t\t%d\t\t%d\n",getpid(),stats[0],stats[1], stats[2], stats[3]);            
        exit(0, "");
    } else {
        // parent
        int pid2 = fork();
        if (pid2 == 0) {
            // child
            set_cfs_priority(1);
            int i;
            for (i = 0; i < 1000000; i++) {
                if (i % 100000 == 0) {
                    sleep(1);
                }
            }
        get_cfs_stats(getpid(), stats);
        printf("%d\t\t%d\t\t%d\t\t%d\t\t%d\n",getpid(),stats[0],stats[1], stats[2], stats[3]);            
        exit(0, "");
        } else {
            // parent
            int pid3 = fork();
            if (pid3 == 0) {
                // child
                set_cfs_priority(2);
                int i;
                for (i = 0; i < 1000000; i++) {
                    if (i % 100000 == 0) {
                        sleep(1);
                    }
                }
                get_cfs_stats(getpid(), stats);
                //printf("%d\t\t%d\t\t%d\t\t%d\t\t%d\n",getpid(),stats[0],stats[1], stats[2], stats[3]); 
                exit(0, "");
            } else {
                // parent
                //wait for children
                wait(&status,"");
                wait(&status,"");
                wait(&status,"");
            }
        }
    }
    exit(0,"");
}
