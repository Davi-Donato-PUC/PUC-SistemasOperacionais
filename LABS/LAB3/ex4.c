

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

void child_loop(int id) {
    long cont = 0;
    while (1) {
        printf("Filho %d (pid=%d) contador=%ld\n", id, getpid(), cont++);
        fflush(stdout);
        sleep(1); 
    }
}

int main(void) {
    pid_t p1, p2;
    int status;

    p1 = fork();
    if (p1 < 0) {
        perror("fork p1");
        exit(1);
    }
    if (p1 == 0) {
        child_loop(1);
        _exit(0);
    }

    p2 = fork();
    if (p2 < 0) {
        perror("fork p2");
        kill(p1, SIGTERM);
        waitpid(p1, NULL, 0);
        exit(1);
    }
    if (p2 == 0) {
        child_loop(2);
        _exit(0);
    }

    printf("Pai: filhos criados p1=%d p2=%d\n", p1, p2);

    if (kill(p1, SIGSTOP) == -1) {
        fprintf(stderr, "Parando f1 deu errado");
    }
    if (kill(p2, SIGSTOP) == -1) {
        fprintf(stderr, "Parando f2 deu errado");

    }

    usleep(200000); /* 200 ms */

    const int TROCAS = 10;
    pid_t ativo  = p1;  
    pid_t parado = p2;

    for (int i = 0; i < TROCAS; ++i) {
        printf("Pai: troca %d - resumindo %d, parando %d\n", i+1, (int)ativo, (int)parado);

        if (kill(ativo, SIGCONT) == -1) { fprintf(stderr, "ERRO AQUI"); }

        sleep(1);

        if (kill(ativo, SIGSTOP) == -1) { fprintf(stderr, "ERRO AQUI 2"); }

        pid_t tmp = ativo;
        ativo = parado;
        parado = tmp;

        
        usleep(200000); /* 200 ms */
    }

    printf("Pai: %d trocas concluídas — matando filhos...\n", TROCAS);

    // MATA GERAL NO FIM
    if (kill(p1, SIGKILL) == -1) { if (errno != ESRCH) fprintf(stderr, "Erro SIGTERM p1: %s\n", strerror(errno)); }
    if (kill(p2, SIGKILL) == -1) { if (errno != ESRCH) fprintf(stderr, "Erro SIGTERM p2: %s\n", strerror(errno)); }

    // Espera pelos filhos para evitar zumbis
    if (waitpid(p1, &status, 0) == -1) { if (errno != ECHILD) perror("waitpid p1"); } 
    else { printf("Pai: filho p1 (%d) terminou com status %d\n", (int)p1, WEXITSTATUS(status)); }

    if (waitpid(p2, &status, 0) == -1) { if (errno != ECHILD) perror("waitpid p2"); } 
    else { printf("Pai: filho p2 (%d) terminou com status %d\n", (int)p2, WEXITSTATUS(status)); }

    printf("Pai: fim.\n");
    return 0;
}

















