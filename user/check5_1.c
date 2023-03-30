#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    //create a child process, and make the parent print 1 and the child print 0 10000 times'''
    int pid = fork();
    if(pid == 0){
        int pid_2;
        pid_2 = fork();
        if(pid_2 == 0){
            for(int i=0; i<10000;i++){
                printf("2");
            }
        }
        else{
            set_ps_priority(1);
            for(int i=0; i<10000;i++){
                printf("3");
            }
        }
        exit(0,"");
    } 
    else{
        set_ps_priority(10);
        for(int i=0; i<10000;i++){
            printf("1");
        }
    }
    exit(0,"");
}
