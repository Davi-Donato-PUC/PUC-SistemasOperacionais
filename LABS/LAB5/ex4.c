#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MSG_SIZE 64

int main() {
    int fd[2];
    pid_t pid1, pid2, pid_writer;

    if (pipe(fd) == -1) {
        perror("pipe");
        exit(1);
    }

    // Processo escritor
    if ((pid_writer = fork()) == 0) {
        close(fd[0]); // Fecha leitura no escritor
        int counter = 0;
        while (1) {
            char buffer[MSG_SIZE];
            snprintf(buffer, sizeof(buffer), "Mensagem %d\n", counter++);
            write(fd[1], buffer, strlen(buffer) + 1);
            printf("[Escritor] escreveu: %s", buffer);
            fflush(stdout);
            sleep(1); // Escritor mais rápido
        }
        close(fd[1]);
        exit(0);
    }

    // Processo leitor 1
    if ((pid1 = fork()) == 0) {
        close(fd[1]); // Fecha escrita no leitor
        char buffer[MSG_SIZE];
        while (1) {
            int n = read(fd[0], buffer, sizeof(buffer));
            if (n > 0) {
                printf("    [Leitor 1] leu: %s", buffer);
                fflush(stdout);
            }
            sleep(2); // Leitor mais lento
        }
        close(fd[0]);
        exit(0);
    }

    // Processo leitor 2
    if ((pid2 = fork()) == 0) {
        close(fd[1]); // Fecha escrita no leitor
        char buffer[MSG_SIZE];
        while (1) {
            int n = read(fd[0], buffer, sizeof(buffer));
            if (n > 0) {
                printf("        [Leitor 2] leu: %s", buffer);
                fflush(stdout);
            }
            sleep(2); // Leitor mais lento
        }
        close(fd[0]);
        exit(0);
    }

    // Processo pai espera os filhos (nunca termina de fato)
    close(fd[0]);
    close(fd[1]);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    return 0;
}
