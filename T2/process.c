#include     <time.h>
#include    <stdio.h>
#include    <fcntl.h>
#include    <errno.h>
#include   <stdlib.h>
#include   <string.h>
#include   <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "sfss.h"
#define FIFO_APPS "/tmp/fifo_apps"

#define MAX 5


char nomesArqs[20];

char * pathnames[] = {"dir1", "dir2", "dir3", "dir4"};
char * filenames[] = {"Davi.txt", "Isabel.txt", "fileA.txt", "fileB.txt"};
char * payloads[]  = {"ABC123456", "DEF321654", "GHI999111", "JKL000999"};



int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <shm_name>\n", argv[0]);
        return 1;
        }
    const char *shm_name = argv[1];
    int fd0 = shm_open(shm_name, O_RDWR, 0);
    if (fd0 < 0) {
        perror("shm_open filho");
        return 1;
        }
    void *ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd0, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap filho");
        close(fd0);
        return 1;
        }

    int *p = (int *)ptr; 
    int appId = *p;


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

    // parâmetros de chance
    const double SYSCALL_PROB = 0.10; // Probabilidade de chamar syscall
    const double A0_PROB = 0.10;      // Probabilidade de atuar em /A0



    while (PC < MAX) {


        
        // envia heartbeat / PC atual
        char buf[256];
        int len = snprintf(buf, sizeof(buf), "PC:%d:%d\n", pid, PC);
        write(fd, &len, sizeof(int));
        write(fd, buf, len);
        
        // decide gerar syscall
        double r = (double)rand() / RAND_MAX;
        if (r < SYSCALL_PROB) {
            int device = (rand() % 2) ? 1 : 2; // alterna D1/D2
            char op;
            int o = rand() % 5;
            
            // Syscall
            char syscall;
            o = rand() % 5;
            if      (o == 0) syscall = 'W'; // Write  (FILE)
            else if (o == 1) syscall = 'R'; // Read   (FILE)
            else if (o == 2) syscall = 'C'; // Create (DIR)
            else if (o == 3) syscall = 'D'; // Delete (DIR)
            else if (o == 4) syscall = 'L'; // List   (DIR)
            // printf("[APP] SYSCALl %c\n", syscall);
            
            // Offset
            int offset;
            o = rand() % 7;    
            offset = o * 16;

            // Path 
            char * path;
            o = rand() % 4;    
            path = pathnames[o];

            // File 
            char * file;
            o = rand() % 4;    
            file = filenames[o];
 
            // Payload
            char * payload;
            o = rand() % 4;    
            payload = payloads[o];

            // Owner 
            int owner;
            double a = (double)rand() / RAND_MAX;
            if (a < A0_PROB) owner = 0 ;
            else owner = 1;

            
            // envia SYSCALL para o kernel
            len = snprintf(buf, sizeof(buf), "SYSCALL:%d:%d:%d:%c:%d:%s:%s:%s\n", pid, PC, owner, syscall, offset, path, file, payload)  ;

            write(fd, &len, sizeof(int)); // Tamanho
            write(fd, buf, len);
            
    
            
            usleep(100000); // curto delay para dar chance do kernel tratar
        }   
        
        SHM_CONTAINER *c = (SHM_CONTAINER*)ptr;

        if (c->has_message == 1) {   // kernel colocou uma REP nova

            // Interpretar o tipo
            SFP_TYPE *t = (SFP_TYPE*)c->raw;

            switch (*t) {

                case SFP_RD_REP: {
                    RD_REP *rep = (RD_REP*)c->raw;
                    printf("[APP%d] RD_REP recebida: \n  path=%s \n  strlen:%d \n  payload='%s'\n", rep->owner, rep->path, rep->strlen, rep->payload);
                } break;
                case SFP_WR_REP: {
                    WR_REP *rep = (WR_REP*)c->raw;
                    printf("[APP%d] WR_REP recebida: \n  path=%s \n  strlen:%d \n  payload='%s'\n", rep->owner, rep->path, rep->strlen, rep->payload);
                } break;
                case SFP_DC_REP: {
                    DC_REP *rep = (DC_REP*)c->raw;
                    printf("[APP%d] DC_REP recebida: \n  path=%s \n  strlen:%d '\n", rep->owner, rep->path, rep->strlen);

                } break;
                case SFP_DR_REP: {
                    DR_REP *rep = (DR_REP*)c->raw;
                    printf("[APP%d] DR_REP recebida: \n  path=%s \n  strlen:%d \n", rep->owner, rep->path, rep->strlen);

                } break;
                case SFP_DL_REP: {
                    DL_REP *rep = (DL_REP*)c->raw;
                    printf("[APP%d] DL_REP recebido: \n  dirs=%s\n", rep->owner, rep->entries);
                } break;

            }

            // Marcar como lido
            c->has_message = 0;
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

