#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#define TAM 20

int main() {

    int fd[2];
    pipe(fd);

    pid_t pid = fork();

    if (pid > 0) { // PAI
        char buffer[TAM] ;
        close(fd[1]);
        read(fd[0], buffer, TAM);
        printf("Mensagem : %s\n", buffer);
        close(fd[0]);
        }

    else if (pid == 0) { // FILHO
        char mensagem[TAM] = "One Piece";
        close(fd[0]);
        write(fd[1], mensagem, TAM);
        close(fd[1]);
        }



    return 0;
    }









