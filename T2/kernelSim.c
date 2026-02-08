#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "sfss.h"


#define MAX_APPS 5
#define MAX_QUEUE 10

// --- FIFO paths ---
#define FIFO_INTER "/tmp/fifo_inter"
#define FIFO_APPS  "/tmp/fifo_apps"

#define SHM_PREFIX "/shm_ap"   /* nome deve começar com '/' */


// Estados do processo
typedef enum {READY, RUNNING, BLOCKED_FILE, BLOCKED_DIR, TERMINATED} state_t;

// Estrutura de processo
typedef struct {
    pid_t pid;
    int id;
    int PC;
    state_t state;
    int fileReqs;
    int dirReqs;
    char blocked_op;
    char shm_name[64];
    void *shm_ptr;
    int shm_fd;

    // campo para armazenar reply pendente vindo do SFSS
    int has_pending_reply;       // 0 = none, 1 = file reply, 2 = dir reply
    unsigned char pending_reply[SHM_SIZE];
} processInfos;

// Estrutura da fila circular
typedef struct {
    int data[MAX_QUEUE];
    int front;
    int rear;
    int count;
} circularQueue;

// Variáveis globais
volatile sig_atomic_t paused = 0;
volatile sig_atomic_t toggle_pause = 0;

processInfos apps[MAX_APPS];
circularQueue ready_queue;
circularQueue fileRequestQueue;
circularQueue dirRequestQueue;

// Cada fila
int in_ready[MAX_APPS]  = {0};
int in_fqueue[MAX_APPS] = {0};
int in_dqueue[MAX_APPS] = {0};

int current_index = -1;
int processosTerminados = 0;

// UDP
int udp_sock;
struct sockaddr_in sfss_addr;

// Funções auxiliares de fila 
void init_queue_struct(circularQueue *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

void init_queues() {
    init_queue_struct(&ready_queue);
    init_queue_struct(&fileRequestQueue);
    init_queue_struct(&dirRequestQueue);
    memset(in_ready, 0, sizeof(in_ready));
    memset(in_fqueue,   0, sizeof(in_fqueue));
    memset(in_dqueue,   0, sizeof(in_dqueue));
}

/* enqueue / dequeue específicos para cada fila  */

void enqueue_ready(int val) {
    if (val < 0 || val >= MAX_APPS) return;
    if (in_ready[val]) return;
    if (ready_queue.count == MAX_QUEUE) {
        printf("[WARN] Ready queue cheia! Valor %d descartado.\n", val);
        return;
    }
    ready_queue.data[ready_queue.rear] = val;
    ready_queue.rear = (ready_queue.rear + 1) % MAX_QUEUE;
    ready_queue.count++;
    in_ready[val] = 1;
}

int dequeue_ready() {
    if (ready_queue.count == 0) return -1;
    int val = ready_queue.data[ready_queue.front];
    ready_queue.front = (ready_queue.front + 1) % MAX_QUEUE;
    ready_queue.count--;
    if (val >= 0 && val < MAX_APPS) in_ready[val] = 0;
    return val;
}

static void safe_kill_pid(pid_t pid, int sig) {
    if (pid > 0) kill(pid, sig);
}

void enqueue_file(int val) {
    if (val < 0 || val >= MAX_APPS) return;
    if (in_fqueue[val]) return;
    if (fileRequestQueue.count == MAX_QUEUE) {
        printf("[WARN] Blocked D1 queue cheia! Valor %d descartado.\n", val);
        return;
    }
    fileRequestQueue.data[fileRequestQueue.rear] = val;
    fileRequestQueue.rear = (fileRequestQueue.rear + 1) % MAX_QUEUE;
    fileRequestQueue.count++;
    in_fqueue[val] = 1;
}

int dequeue_file() {
    if (fileRequestQueue.count == 0) return -1;
    int val = fileRequestQueue.data[fileRequestQueue.front];
    fileRequestQueue.front = (fileRequestQueue.front + 1) % MAX_QUEUE;
    fileRequestQueue.count--;
    if (val >= 0 && val < MAX_APPS) in_fqueue[val] = 0;
    return val;
}

void enqueue_dir(int val) {
    if (val < 0 || val >= MAX_APPS) return;
    if (in_dqueue[val]) return;
    if (dirRequestQueue.count == MAX_QUEUE) {
        printf("[WARN] Blocked D2 queue cheia! Valor %d descartado.\n", val);
        return;
    }
    dirRequestQueue.data[dirRequestQueue.rear] = val;
    dirRequestQueue.rear = (dirRequestQueue.rear + 1) % MAX_QUEUE;
    dirRequestQueue.count++;
    in_dqueue[val] = 1;
}

int dequeue_dir() {
    if (dirRequestQueue.count == 0) return -1;
    int val = dirRequestQueue.data[dirRequestQueue.front];
    dirRequestQueue.front = (dirRequestQueue.front + 1) % MAX_QUEUE;
    dirRequestQueue.count--;
    if (val >= 0 && val < MAX_APPS) in_dqueue[val] = 0;
    return val;
}


// --- ESCALONADOR ================================================================================ >>>

void schedule_next() {
    // Parar processo atual, se houver
    if (current_index != -1) {
        if (apps[current_index].state == RUNNING) {
            safe_kill_pid(apps[current_index].pid, SIGSTOP);
            if (apps[current_index].state != TERMINATED &&
                apps[current_index].state != BLOCKED_FILE &&
                apps[current_index].state != BLOCKED_DIR) {
                apps[current_index].state = READY;
                enqueue_ready(current_index);
                }
            }
        current_index = -1;
        }

    // Escolher próximo processo válido
    int next = dequeue_ready();
    while (next != -1) {
        if (next < 0 || next >= MAX_APPS) {
            next = dequeue_ready();
            continue;
            }
        if (apps[next].state == READY) break;
        next = dequeue_ready();
        }

    if (next == -1) {
        //printf("[Kernel] Nenhum processo pronto.\n");
        current_index = -1;
        return;
        }

    apps[next].state = RUNNING;
    safe_kill_pid(apps[next].pid, SIGCONT);
    current_index = next;

    printf("[Kernel] Processo AP%d em execução (PID=%d)\n", apps[next].id, apps[next].pid);
    }

//  Tratadores de eventos 
void unblock_process(int device) {
    int idx = -1;
    if (device == 1) idx = dequeue_file();
    else idx = dequeue_dir();

    if (idx == -1) return;

    // Se existir reply pendente, escreve na shm antes de desbloquear
    if (apps[idx].has_pending_reply) {
        // escreve resposta na shared memory do processo
        char *dest = (char *) apps[idx].shm_ptr;
        if (dest != NULL) {
            // garantimos tamanho

            SHM_CONTAINER *c = apps[idx].shm_ptr;

            memcpy(c->raw, apps[idx].pending_reply, sizeof(apps[idx].pending_reply));
            c->has_message = 1;

            // limpa pending
            apps[idx].has_pending_reply = 0;
            apps[idx].pending_reply[0] = '\0';
        }
    }

    if (apps[idx].state != TERMINATED) {
        apps[idx].state = READY;
        enqueue_ready(idx);
        printf("[Kernel] IRQ%d recebida, AP%d desbloqueado e reply entregue.\n", device, apps[idx].id);
        }
    }


void update_pc(int pid, int pc) {
    for (int i = 0; i < MAX_APPS; i++)
        if (apps[i].pid == pid)
            apps[i].PC = pc;
    }

void mark_terminated(int pid) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].pid == pid) {
            apps[i].state = TERMINATED;
            /* remover quaisquer flags de presença em filas */
            in_ready[i]  = 0;
            in_fqueue[i] = 0;
            in_dqueue[i] = 0;
            }
        }
    }

void sigint_handler(int sig) { toggle_pause = 1; }


// SHARED MEMORY ---------------------------------------------------------------------------------- >>>

char *create_shm_name(int idx) {
    static char name[64];
    snprintf(name, sizeof(name), "%s%d", SHM_PREFIX, idx+1); // "/shm_ap1", "/shm_ap2", ...
    return name;
}

int create_and_init_shm(const char *name, size_t size) {
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        if (errno == EEXIST) {
            /* se já existir, abre sem O_EXCL */
            fd = shm_open(name, O_RDWR, 0);
            if (fd < 0) { perror("shm_open existing failed"); return -1; }
        } else {
            perror("shm_open failed");
            return -1;
        }
    } else {
        /* só ftruncate quando criamos */
        if (ftruncate(fd, size) < 0) {
            perror("ftruncate");
            close(fd);
            shm_unlink(name);
            return -1;
        }
    }
    return fd;
}


// envia string para a shm do processo (usa mmap que foi salvo em apps[i].shm_ptr)
void deliver_reply_to_process_by_index(int appIndex, const char *reply) {
    if (appIndex < 0 || appIndex >= MAX_APPS) return;
    if (!apps[appIndex].shm_ptr) return;
    char *dest = (char *)apps[appIndex].shm_ptr;
    snprintf(dest, SHM_SIZE, "%s", reply);
}


// REQS =========================================================================================== >>>

// Construção simples de mensagens SFP em texto (fácil para testar localmente)
void send_rd_req(int owner, char *file, int offset, int dir) {
    RD_REQ req;
    req.type = SFP_RD_REQ;
    req.owner = owner;
    req.offset = offset;
    req.dir = dir;
    strncpy(req.file, file, sizeof(req.file));
    req.file[sizeof(req.file)-1] = '\0';
    sendto(udp_sock, &req, sizeof(req), 0, (struct sockaddr *)&sfss_addr, sizeof(sfss_addr));
    
    }

void send_wr_req(int owner, char *file, int offset, char *payload, int dir) {
    WR_REQ req;
    req.type = SFP_WR_REQ;
    req.owner = owner;
    req.dir = dir;
    req.offset = offset;
    strncpy(req.file, file, sizeof(req.file));
    req.file[sizeof(req.file)-1] = '\0';
    strncpy(req.payload, payload, sizeof(req.payload));
    req.payload[sizeof(req.payload)-1] = '\0';
    sendto(udp_sock, &req, sizeof(req), 0, (struct sockaddr *)&sfss_addr, sizeof(sfss_addr));
    }

void send_dc_req(int owner, char *path, int dir) {
    DC_REQ req;
    req.type = SFP_DC_REQ;
    req.owner = owner;
    req.dir = dir;
    strncpy(req.dirname, path, sizeof(req.dirname));
    sendto(udp_sock, &req, sizeof(req), 0, (struct sockaddr *)&sfss_addr, sizeof(sfss_addr));
    }

void send_dr_req(int owner, char *path, int dir) {
    DR_REQ req;
    req.type = SFP_DR_REQ;
    req.owner = owner;
    req.dir = dir;
    strncpy(req.dirname, path, sizeof(req.dirname));
    sendto(udp_sock, &req, sizeof(req), 0, (struct sockaddr *)&sfss_addr, sizeof(sfss_addr));
    }

void send_dl_req(int owner, char *path, int dir) {
    DL_REQ req;
    req.type = SFP_DL_REQ;
    req.owner = owner;
    req.dir = dir;
    sendto(udp_sock, &req, sizeof(req), 0, (struct sockaddr *)&sfss_addr, sizeof(sfss_addr));
    }

// ================================================================================================ <<<


void udp_init() {
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { perror("socket"); exit(1); }

    bzero(&sfss_addr, sizeof(sfss_addr));
    sfss_addr.sin_family = AF_INET;
    sfss_addr.sin_port = htons(9000); // mesma porta do SFSS 
    inet_aton("127.0.0.1", &sfss_addr.sin_addr); // local

    int flags = fcntl(udp_sock, F_GETFL, 0);
    fcntl(udp_sock, F_SETFL, flags | O_NONBLOCK);
}

// Recebe replies do SFSS (não-bloqueante) e grava em apps[owner].pending_reply
void try_receive_sfp_reply() {
    unsigned char buf[2048];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    ssize_t n = recvfrom(udp_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
    if (n <= 0) return;

    // Agora que estamos recebendo STRUCTS, buf NÃO É STRING
    SFP_TYPE type = *(SFP_TYPE *)buf;

    int owner = -1;

    switch (type) {

        case SFP_RD_REP: {
            RD_REP *rep = (RD_REP *)buf;
            owner = rep->owner;
            break;
        }

        case SFP_WR_REP: {
            WR_REP *rep = (WR_REP *)buf;
            owner = rep->owner;
            break;
        }

        case SFP_DC_REP: {
            DC_REP *rep = (DC_REP *)buf;
            owner = rep->owner;
            break;
        }

        case SFP_DR_REP: {
            DR_REP *rep = (DR_REP *)buf;
            owner = rep->owner;
            break;
        }

        case SFP_DL_REP: {
            DL_REP *rep = (DL_REP *)buf;
            owner = rep->owner;
            break;
        }

        default:
            printf("[Kernel] Resposta desconhecida (type=%d)\n", type);
            return;
    }

    if (owner < 1 || owner > MAX_APPS) {
        printf("[Kernel] OWNER inválido na resposta: %d\n", owner);
        return;
    }

    // Agora armazenamos a STRUCT recebida diretamente
    memcpy(apps[owner-1].pending_reply, buf, n);
    apps[owner-1].has_pending_reply = 1;

    printf("[Kernel] REP recebida do owner=%d, tipo=%d \n", owner, type);
}

// LIDANDO COM A SYSCALL -------------------------------------------------------------------------- >>>

void handle_syscall(int pid, char syscall, int offset, char * pathname, char * filename, char * payload, int dir) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].pid == pid) {

            safe_kill_pid(pid, SIGSTOP);
            apps[i].blocked_op = syscall;
            int owner = i+1;

            // IDENTIFICAÇÃO DA SYSCALL -----------------------------
            switch (syscall) {

                // ============ READ ============
                case 'R':
                    printf("[Kernel] AP%d Bloqueado em File Request \n", owner);
                    printf("[Kernel] REQ Enviada : RD_REQ (AP%d)\n", owner);
                    apps[i].state = BLOCKED_FILE;
                    apps[i].fileReqs++;
                    enqueue_file(i);
                    send_rd_req(owner, filename, offset, dir);
                    break;

                // ============ WRITE ============
                case 'W':
                    printf("[Kernel] AP%d Bloqueado em File Request \n", owner);
                    printf("[Kernel] REQ Enviada : WR_REQ (AP%d)\n", owner);
                    apps[i].state = BLOCKED_FILE;
                    apps[i].fileReqs++;
                    enqueue_file(i);
                    send_wr_req(owner, filename, offset, payload, dir);
                    break;

                // ============ ADD DIRECTORY ============
                case 'C':  
                    printf("[Kernel] AP%d Bloqueado em Dir Request \n", owner);
                    printf("[Kernel] REQ Enviada : DC_REQ (AP%d)\n",  owner);
                    apps[i].state = BLOCKED_DIR;
                    apps[i].dirReqs++;
                    enqueue_dir(i);
                    send_dc_req(owner, pathname, dir);
                    break;

                // ============ REMOVE DIRECTORY ============
                case 'D':  
                    printf("[Kernel] AP%d Bloqueado em Dir Request \n", owner);
                    printf("[Kernel] REQ Enviada : DR_REQ (AP%d)\n", owner);
                    apps[i].state = BLOCKED_DIR;
                    apps[i].dirReqs++;
                    enqueue_dir(i);
                    send_dr_req(owner, pathname, dir);
                    break;

                // ============ LIST DIR ============
                case 'L':
                    printf("[Kernel] AP%d Bloqueado em Dir Request \n", owner);
                    printf("[Kernel] REQ Enviada : DL_REQ (AP%d)\n", owner);
                    apps[i].state = BLOCKED_DIR;
                    apps[i].dirReqs++;
                    enqueue_dir(i);
                    send_dl_req(owner, filename, dir);
                    break;

                default:
                    printf("[Kernel] Syscall inválida '%c'\n", syscall);
                    return;
            }

            if (current_index == i) { current_index = -1; schedule_next(); }
            break;

        }
    }
}


// --- MAIN ---
int main() {
    init_queues();

    signal(SIGINT, sigint_handler);

    // cria fifos (ignora se já existirem)
    mkfifo(FIFO_INTER, 0666);
    mkfifo(FIFO_APPS,  0666);
    int fd_inter = open(FIFO_INTER, O_RDONLY | O_NONBLOCK);
    int fd_apps  = open(FIFO_APPS,  O_RDONLY | O_NONBLOCK);
    if (fd_inter < 0 || fd_apps < 0) {
        perror("Erro ao abrir FIFOs");
        exit(1);
    }

    // inicializa UDP (cliente)
    udp_init();

    // CRIANDO PROCESSOS -------------------------------------------------------------------------- >>>
    for (int i = 0; i < MAX_APPS; i++) {

        char *shm_name = create_shm_name(i); //  Nome (Apx...)
        int shm_fd = create_and_init_shm(shm_name, SHM_SIZE);
        if (shm_fd < 0) { fprintf(stderr, "Erro criando shm para %s\n", shm_name); exit(1); } // Err

        /* mapear aqui no pai para inicializar conteúdo */
        void *shm_ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shm_ptr == MAP_FAILED) {
            perror("mmap pai");
            close(shm_fd);
            shm_unlink(shm_name);
            exit(1);
        }
        /* inicializa com zero, por exemplo */
        memset(shm_ptr, 0, SHM_SIZE);
        memcpy(shm_ptr, &i, sizeof(int));

        pid_t pid = fork();
        if (pid == 0) {
            /* filho */
            munmap(shm_ptr, SHM_SIZE);
            close(shm_fd);
            /* passa o nome do shm como argumento */
            execl("./process", "process", shm_name, (char *)NULL);
            perror("execl do filho falhou");
            exit(1);
        }

        /* pai continua */
        apps[i].pid = pid;
        apps[i].id = i + 1;
        apps[i].PC = 0;
        apps[i].state = READY;
        apps[i].fileReqs = apps[i].dirReqs = 0;
        strncpy(apps[i].shm_name, shm_name, sizeof(apps[i].shm_name));
        apps[i].shm_fd = shm_fd;
        apps[i].shm_ptr = shm_ptr; // mapeado no pai para ver conteúdo
        apps[i].has_pending_reply = 0;
        apps[i].pending_reply[0] = '\0';
        safe_kill_pid(pid, SIGSTOP);
        enqueue_ready(i);
    }

    // Inicia a execução
    schedule_next();

    char buffer[1024];
    unsigned char sizeBuffer;
    int sizeBufferInt;

    while (1) {

        // recebe replies UDP não-bloqueante
        try_receive_sfp_reply();

         if (toggle_pause) {
            toggle_pause = 0;
            paused = !paused;

            if (current_index != -1)
                safe_kill_pid(apps[current_index].pid, SIGSTOP);

            if (paused) {
                printf("\n=== SIMULADOR PAUSADO ===\n");
                printf("(Pressione Ctrl+C novamente para continuar)\n\n");
                for (int i = 0; i < MAX_APPS; i++) {
                    printf("AP%d (PID %d): PC=%d | ", i+1, apps[i].pid, apps[i].PC);
                    switch (apps[i].state) {
                        case READY: printf("READY"); break;
                        case RUNNING: printf("RUNNING"); break;
                        case BLOCKED_FILE: printf("BLOCKED FILE (%c)", apps[i].blocked_op); break;
                        case BLOCKED_DIR: printf("BLOCKED DIR (%c)", apps[i].blocked_op); break;
                        case TERMINATED: printf("TERMINATED"); break;
                    }
                    printf(" | File Reqs=%d | Dir reqs=%d\n", apps[i].fileReqs, apps[i].dirReqs);
                }
                printf("=========================\n");
            } else {
                printf("\n=== SIMULADOR RETOMADO ===\n\n");
                if (current_index != -1)
                    safe_kill_pid(apps[current_index].pid, SIGCONT);
            }
        }

        if (paused) {
            usleep(100000);
            continue;
        }

        // Lê do intercontroller
        ssize_t sizeInterMsg = read(fd_inter, &sizeBuffer, sizeof(unsigned char));
        ssize_t interMsg = 0;
        if (sizeInterMsg > 0) {
            interMsg = read(fd_inter, buffer, sizeBuffer);
        }

        // INTERCONTROLLER ------------------------------------------------------------------------ >>>
        if (interMsg > 0) {
            buffer[interMsg - 1] = '\0';
            if (strstr(buffer, "IRQ:0")) {
                //printf("[Kernel] IRQ0 (Timer)\n");
                schedule_next();
            } else if (strstr(buffer, "IRQ:1")) {
                //printf("[Kernel] IRQ1 (D1)\n");
                unblock_process(1);
            } else if (strstr(buffer, "IRQ:2")) {
                // printf("[Kernel] IRQ2 (D2)\n");
                unblock_process(2);
            }
        }

        // APPS ----------------------------------------------------------------------------------- >>>
        ssize_t sizeAppMsg = read(fd_apps, &sizeBufferInt, sizeof(int));
        ssize_t appMsg = 0;
        if (sizeAppMsg > 0) {
            appMsg = read(fd_apps, buffer, sizeBufferInt);
        }

        if (appMsg > 0) {
            buffer[appMsg - 1] = '\0';
            if (strncmp(buffer, "SYSCALL", 7) == 0) { // SYSCALL ---------------------------------- >>>
                int pid, pc, offset, dir;
                char syscall;
                char pathname[256];
                char filename[256];
                char payload[64];

                // Formato esperado: SYSCALL:<pid>:<pc>:<syscall>:<offset>:<filename>:<payload>
                sscanf(buffer, "SYSCALL:%d:%d:%d:%c:%d:%255[^:]:%255[^:]:%63s", &pid, &pc, &dir, &syscall, &offset, pathname, filename, payload);
                printf("[Kernel] syscall recebida: (processo %d) (syscall %c) (Dir %d) (offset %d) (pathname: %s) (filename %s) (payload %s)\n", pid, syscall, dir, offset, pathname, filename, payload);
                update_pc(pid, pc);
                handle_syscall(pid, syscall, offset, pathname, filename, payload, dir);

            } else if (strncmp(buffer, "PC", 2) == 0) {
                int pid, pc;
                sscanf(buffer, "PC:%d:%d", &pid, &pc);
                update_pc(pid, pc);

            } else if (strncmp(buffer, "TERMINATED", 10) == 0) {
                processosTerminados++;
                int pid;
                sscanf(buffer, "TERMINATED:%d", &pid);
                mark_terminated(pid);
                printf("[Kernel] Processo %d terminou.\n", pid);
            }
        }

        if (processosTerminados == MAX_APPS) break;
        usleep(100000);
    }

    // FIM ========================================================================================================================================== |||
    close(fd_inter);
    close(fd_apps);
    unlink(FIFO_INTER);
    unlink(FIFO_APPS);

    close(udp_sock);

    printf("\n=== KERNEL FINALIZADO ===\n");
    for (int i = 0; i < MAX_APPS; i++) {
        printf("AP%d (PID %d): PC=%d | ", i+1, apps[i].pid, apps[i].PC);
        switch (apps[i].state) {
            case READY: printf("READY"); break;
            case RUNNING: printf("RUNNING"); break;
            case BLOCKED_FILE: printf("BLOCKED FILE (%c)", apps[i].blocked_op); break;
            case BLOCKED_DIR: printf("BLOCKED DIR (%c)", apps[i].blocked_op); break;
            case TERMINATED: printf("TERMINATED"); break;
        }
        printf(" | File Reqs=%d | Dir Reqs=%d\n", apps[i].fileReqs, apps[i].dirReqs);
    }

    return 0;
}










