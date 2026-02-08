

#include      <time.h>
#include     <stdio.h>
#include     <fcntl.h>
#include     <errno.h>
#include    <stdlib.h>
#include    <string.h>
#include    <signal.h>
#include    <unistd.h>
#include  <sys/stat.h>
#include <sys/types.h>

#define FIFO_INTER "/tmp/fifo_inter"




int main(int argc, char **argv) {
    // Probabilidades: P1 = 0.10 (IRQ1), P2 = P1/20 = 0.005 (IRQ2)
    const double P1 = 0.50;
    const double P2 = P1 / 20.0;

    srand(time(NULL) ^ getpid());

    // garante que o FIFO existe (kernel normalmente também cria)
    if (mkfifo(FIFO_INTER, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            return 1;
        }
    }

    // Abrir FIFO para escrita (bloqueia até existir leitor)
    int fd = open(FIFO_INTER, O_WRONLY);
    if (fd < 0) {
        perror("open fifo_inter para escrita");
        return 1;
    }

    printf("[InterController] Iniciado (PID=%d). Escrevendo em %s\n", getpid(), FIFO_INTER);

    while (1) {

   
        usleep(500000);
        // IRQ0 (timer) sempre
        const char *msg = "IRQ:0\n";
        unsigned char tam = strlen(msg);
        write(fd, &tam, sizeof(unsigned char));
        write(fd, msg, strlen(msg));

        // Sorteio para IRQ1
        double r1 = (double)rand() / RAND_MAX;
        if (r1 < P1) {
            const char *msg1 = "IRQ:1\n";
            unsigned char tam = strlen(msg1);
            write(fd, &tam, sizeof(unsigned char));
            write(fd, msg1, strlen(msg1));
            //printf("[InterController] gerou IRQ:1\n");
            }

        // Sorteio para IRQ2
        double r2 = (double)rand() / RAND_MAX;
        if (r2 < P2) {
            const char *msg2 = "IRQ:2\n";
            unsigned char tam = strlen(msg2);
            write(fd, &tam, sizeof(unsigned char));
            write(fd, msg2, strlen(msg2));
            //printf("[InterController] gerou IRQ:2\n");
            }

    }

    close(fd);
    return 0;
}



