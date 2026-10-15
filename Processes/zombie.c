/*
Escreva o programa “zombie.c” que execute o programa mostrado na Tabela 2. O programa cria um filho e deixa ele como zombie, isto é, não aguarda ele terminar. Após um tempo de X segundos o pai executa o comando ps e termina. A saída no terminal apresenta o processo zombie.x no estado <defunct> indicando que zombie.x com PID 1192, filho de PID 1191 virou órfão e, após ser reconhecido como tal pelo SO, virou um “órfão defunto”, isto é, ele foi terminado pelo SO.
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

    if(pid < 0)
    { // error occurred
        fprintf(stderr, "Fork Failed\n");
        return 1;
    }
    else if(pid == 0)
    { // child process
        _exit(0);
    }
    else
    { // parent process
        // parent will wait for the child to complete
        sleep(5);
        execlp("/bin/ps","ps",NULL);
        _exit(0);
    }

    return 0;
}