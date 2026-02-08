#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define TAM 300



int main() {
    int fd[2];
    pipe(fd);

    int fdEntrada = open("entrada.txt", O_RDONLY);
    int fdSaida = open("saida.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);

    dup2(fdEntrada, fd[0]);
    dup2(fdSaida, fd[1]);


    char buffer[TAM];

    int n = read(fd[0], buffer, TAM);
    write(fd[1], buffer, n);

    close(fd[1]);
    close(fdEntrada);
    
    close(fd[0]);
    close(fdSaida);
    return 0;
    }













