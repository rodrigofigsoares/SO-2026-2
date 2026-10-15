#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    printf("systemmd = %d\n", getpid());
    pid_t pid_logind = fork();
    if(pid_logind < 0)
    {
        fprintf(stderr, "Fork Failed\n");
        return 1;
    }
    else if(pid_logind == 0)
    {
        printf("\tlogind = %d\n", getpid());
        pid_t pid_bash = fork();
        if(pid_bash < 0)
        {
            fprintf(stderr, "Fork Failed\n");
            _exit(1);
        }
        else if(pid_bash == 0)
        {
            printf("\t\tbash = %d\n", getpid());
            pid_t pid_ps = fork();
            if(pid_ps < 0)
            {
                fprintf(stderr, "Fork Failed\n");
                _exit(1);
            }
            else if(pid_ps == 0)
            {
                printf("\t\t\tps = %d\n", getpid());
                _exit(0);
            }
            else
            {
                wait(NULL);
            }
        
            pid_t pid_vim = fork();
            if(pid_vim < 0)
            {
                fprintf(stderr, "Fork Failed\n");
                _exit(1);
            }
            else if(pid_vim == 0)
            {
                printf("\t\t\tvim = %d\n", getpid());
                _exit(0);
            }
            else
            {
                wait(NULL);
            }
            
            _exit(0);
        }
        else
        {
            wait(NULL);
        }

        _exit(0);
    }
    else
    {
        wait(NULL);
    }

    pid_t pid_python = fork();
    if(pid_python < 0)
    {
        fprintf(stderr, "Fork Failed\n");
        _exit(1);
    }
    else if(pid_python == 0)
    {
        printf("\tpython = %d\n", getpid());
        _exit(0);
    }
    else
    {
        wait(NULL);
    }

    pid_t pid_sshd = fork();
    if(pid_sshd < 0)
    {
        fprintf(stderr, "Fork Failed\n");
        _exit(1);
    }
    else if(pid_sshd == 0)
    {
        printf("\tsshd = %d\n", getpid());
        pid_t pid_sshd_child = fork();
        if(pid_sshd_child < 0)
        {
            fprintf(stderr, "Fork Failed\n");
            _exit(1);
        }
        else if(pid_sshd_child == 0)
        {
            printf("\t\tsshd child = %d\n", getpid());
            pid_t pid_tcsh = fork();
            if(pid_tcsh < 0)
            {
                fprintf(stderr, "Fork Failed\n");
                _exit(1);
            }
            else if(pid_tcsh == 0)
            {
                printf("\t\t\ttcsh = %d\n", getpid());
                _exit(0);
            }
            else
            {
                wait(NULL);
            }
            
            _exit(0);
        }
        else
        {
            wait(NULL);
        }
    }
    else
    {
        wait(NULL);
    }

    sleep(10);
    return 0;
}