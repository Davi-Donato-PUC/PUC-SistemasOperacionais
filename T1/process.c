#include     <time.h>
#include    <stdio.h>
#include    <fcntl.h>
#include    <errno.h>
#include   <stdlib.h>
#include   <string.h>
#include   <unistd.h>
#include <sys/stat.h>

#define FIFO_APPS "/tmp/fifo_apps"

#define MAX 5

int main() {

    // Semente do random
    srand(time(NULL) ^ getpid());

    // garante que FIFO existe (kernel normalmente cria)
    if (mkfifo(FIFO_APPS, 0666) == -1) { if (errno != EEXIST) { perror("mkfifo"); return 1; } }

    // abrir FIFO para escrita (bloqueia até existir leitor)
    int fd = open(FIFO_APPS, O_WRONLY);
    if (fd < 0) {
        perror("open fifo_apps para escrita");
        return 1;
        }

    pid_t pid = getpid();
    printf("[App] Inicio AP (PID=%d) MAX=%d\n", pid, MAX);
    fflush(stdout);

    int PC = 0;

    // parâmetros de chance de syscall (ajustáveis)
    const double SYSCALL_PROB = 0.10; // 15% por iteração (ajuste se quiser mais/menos)



    while (PC < MAX) {

        // envia heartbeat / PC atual
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "PC:%d:%d\n", pid, PC);
        write(fd, &len, sizeof(int));
        write(fd, buf, len);

        // decide gerar syscall
        double r = (double)rand() / RAND_MAX;
        if (r < SYSCALL_PROB) {
            int device = (rand() % 2) ? 1 : 2; // alterna D1/D2
            char op;
            int o = rand() % 3;
            if (o == 0) op = 'R';
            else if (o == 1) op = 'W';
            else op = 'X';

            // envia SYSCALL para o kernel
            len = snprintf(buf, sizeof(buf), "SYSCALL:%d:%d:%d:%c\n", pid, PC, device, op);
            write(fd, &len, sizeof(int));
            write(fd, buf, len);

            // Após solicitar syscall, o kernel deve enviar SIGSTOP e colocá-lo na fila de bloqueio.
            usleep(100000); // curto delay para dar chance do kernel tratar
            }   
        

        PC++;
        usleep(500000); // 500ms por iteração 
        
    }

    // envia terminado
    char buf2[30];
    int l2 = snprintf(buf2, sizeof(buf2), "TERMINATED:%d\n", pid);
    write(fd, &l2, sizeof(int));
    write(fd, buf2, l2);

    close(fd);
    printf("[App] AP (PID=%d) terminou (PC=%d)\n", pid, PC);
    return 0;
}

