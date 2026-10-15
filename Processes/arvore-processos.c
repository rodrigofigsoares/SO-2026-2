/*
Escreva o programa “arvore-processos.c” que executa o programa mostrado na Tabela 1. O programa cria uma estrutura de árvore de processos do tipo P_A -> P_B -> P_C, onde o processo P_A é o pai de P_B, quem por sua vez é o pai de P_C.
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    pid_t pid;

    // fork a child process
    pid = fork();

    if (pid < 0)
    { // error occurred
        fprintf(stderr, "Fork Failed\n");
        return 1;
    }
    else if (pid == 0) 
    { // child process
        pid = fork(); // fork another child process
        if (pid < 0)
        { // error occurred
            fprintf(stderr, "Fork Failed\n");
            return 1;
        }
        else if (pid == 0)
        { // child process
            printf("\t\tsou P_C com PID %d, PID interno %d, filho de PID %d\n", getpid(), pid, getppid());
            printf("\t\tEu P_C executei: ps\n");
            execlp("/bin/ps", "ps", NULL);
        }
        else
        { // parent process
            printf("\tsou P_B com PID %d, PID interno %d, filho de PID %d\n", getpid(), pid, getppid());
            printf("\tEu P_B criei P_C!\n");
            wait(NULL);
            printf("\tEu P_B aguardei P_C terminar!\n");
            printf("\tEu P_B executei: ps\n");
            execlp("/bin/ps", "ps", NULL);
        }
    }
    else { // parent process
        printf("sou P_A com PID %d, PID interno %d, filho de PID %d\n", getpid(), pid, getppid());
        printf("eu P_A criei P_B!\n");
        wait(NULL);
        printf("Eu P_A aguardei P_B terminar!\n");
        printf("Eu P_A executei: ps\n");
        execlp("/bin/ps", "ps", NULL);
    }

    return 0;
}