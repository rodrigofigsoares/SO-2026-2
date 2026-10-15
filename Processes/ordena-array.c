/*
Escreva o programa “ordena-array.c” o qual possui uma variável do tipo array contendo dez números desordenados. O processo main deve criar um processo filho usando fork. Em seguida o main deve ordenar o array utilizando o método bubbleSort, enquanto o filho deve utilizar quickSort. Ambos processos deverão mostrar o vetor ordenado por final.
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "lib/sortlib.h"

int main(int argc, char **argv)
{
    int array[10] = {5, 2, 9, 1, 5, 6, 3, 7, 8, 4};

    pid_t pid;
    pid = fork();
    if(pid < 0)
    {
        fprintf(stderr, "Fork failed\n");
        return 1;
    }
    else if(pid == 0) // child
    {
        quickSort(array, 0, 9);
        printf("Array ordenado pelo filho: ");
        for(int i = 0; i < 10; i++)
        {
            printf("%d ", array[i]);
        }
        printf("\n");
        _exit(0);
    }
    else // parent
    {
        wait(NULL);
        bubbleSort(array, 10);
        printf("Array ordenado pelo pai: ");
        for(int i = 0; i < 10; i++)
        {
            printf("%d ", array[i]);
        }
        printf("\n");
    }
}
