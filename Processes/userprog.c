/*
Escreva o programa “userprog.c” que permita ao usuário ingressar um comando com seus parâmetros e que execute este comando. Faça uso das chamadas fork e execlp.
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
    pid_t pid;

    pid = fork();
    if(pid < 0)
    {
        fprintf(stderr, "Fork failed\n");
        return 1;
    }
    else if(pid == 0) // child
    {
        execlp(argv[1], argv[1], NULL);
        _exit(0);
    }
    else // parent
    {
        wait(NULL);
        printf("Child process finished\n");
    }

    return 0;
}