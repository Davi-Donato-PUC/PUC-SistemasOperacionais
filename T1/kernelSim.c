#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_APPS 5
#define MAX_QUEUE 10

// --- FIFO paths ---
#define FIFO_INTER "/tmp/fifo_inter"
#define FIFO_APPS  "/tmp/fifo_apps"

// Estados do processo
typedef enum {READY, RUNNING, BLOCKED_D1, BLOCKED_D2, TERMINATED} state_t;

// Estrutura de processo
typedef struct {
    pid_t pid;
    int id;
    int PC;
    state_t state;
    int access_D1;
    int access_D2;
    char blocked_op;
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
circularQueue blocked_D1;
circularQueue blocked_D2;

// Cada fila
int in_ready[MAX_APPS] = {0};
int in_b1[MAX_APPS]   = {0};
int in_b2[MAX_APPS]   = {0};

int current_index = -1;
int processosTerminados = 0;

// Funções auxiliares de fila 
void init_queue_struct(circularQueue *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

void init_queues() {
    init_queue_struct(&ready_queue);
    init_queue_struct(&blocked_D1);
    init_queue_struct(&blocked_D2);
    memset(in_ready, 0, sizeof(in_ready));
    memset(in_b1,   0, sizeof(in_b1));
    memset(in_b2,   0, sizeof(in_b2));
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

void enqueue_b1(int val) {
    if (val < 0 || val >= MAX_APPS) return;
    if (in_b1[val]) return;
    if (blocked_D1.count == MAX_QUEUE) {
        printf("[WARN] Blocked D1 queue cheia! Valor %d descartado.\n", val);
        return;
    }
    blocked_D1.data[blocked_D1.rear] = val;
    blocked_D1.rear = (blocked_D1.rear + 1) % MAX_QUEUE;
    blocked_D1.count++;
    in_b1[val] = 1;
}

int dequeue_b1() {
    if (blocked_D1.count == 0) return -1;
    int val = blocked_D1.data[blocked_D1.front];
    blocked_D1.front = (blocked_D1.front + 1) % MAX_QUEUE;
    blocked_D1.count--;
    if (val >= 0 && val < MAX_APPS) in_b1[val] = 0;
    return val;
}

void enqueue_b2(int val) {
    if (val < 0 || val >= MAX_APPS) return;
    if (in_b2[val]) return;
    if (blocked_D2.count == MAX_QUEUE) {
        printf("[WARN] Blocked D2 queue cheia! Valor %d descartado.\n", val);
        return;
    }
    blocked_D2.data[blocked_D2.rear] = val;
    blocked_D2.rear = (blocked_D2.rear + 1) % MAX_QUEUE;
    blocked_D2.count++;
    in_b2[val] = 1;
}

int dequeue_b2() {
    if (blocked_D2.count == 0) return -1;
    int val = blocked_D2.data[blocked_D2.front];
    blocked_D2.front = (blocked_D2.front + 1) % MAX_QUEUE;
    blocked_D2.count--;
    if (val >= 0 && val < MAX_APPS) in_b2[val] = 0;
    return val;
}

static void safe_kill_pid(pid_t pid, int sig) {
    if (pid > 0) kill(pid, sig);
}

// --- Escalonador ---
void schedule_next() {
    // Parar processo atual, se houver
    if (current_index != -1) {
        if (apps[current_index].state == RUNNING) {
            safe_kill_pid(apps[current_index].pid, SIGSTOP);
            if (apps[current_index].state != TERMINATED &&
                apps[current_index].state != BLOCKED_D1 &&
                apps[current_index].state != BLOCKED_D2) {
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
        if (apps[next].state == READY)
            break;
        next = dequeue_ready();
    }

    if (next == -1) {
        printf("[Kernel] Nenhum processo pronto.\n");
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
    if (device == 1)
        idx = dequeue_b1();
    else
        idx = dequeue_b2();

    if (idx == -1) return;

    if (apps[idx].state != TERMINATED) {
        apps[idx].state = READY;
        enqueue_ready(idx);
        printf("[Kernel] AP%d desbloqueado (D%d)\n", apps[idx].id, device);
    }
}

void handle_syscall(int pid, int device, char op) {
    for (int i = 0; i < MAX_APPS; i++) {
        if (apps[i].pid == pid) {
            safe_kill_pid(pid, SIGSTOP);
            apps[i].blocked_op = op;
            if (device == 1) {
                apps[i].state = BLOCKED_D1;
                apps[i].access_D1++;
                enqueue_b1(i);
            } else {
                apps[i].state = BLOCKED_D2;
                apps[i].access_D2++;
                enqueue_b2(i);
            }
            printf("[Kernel] AP%d bloqueado em D%d (%c)\n", apps[i].id, device, op);

            if (current_index == i) {
                current_index = -1;
                schedule_next();
            }
            break;
        }
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
            in_ready[i] = 0;
            in_b1[i] = 0;
            in_b2[i] = 0;
        }
    }
}

void sigint_handler(int sig) {
    toggle_pause = 1;
}

// --- MAIN ---
int main() {
    init_queues();

    signal(SIGINT, sigint_handler);

    mkfifo(FIFO_INTER, 0666);
    mkfifo(FIFO_APPS, 0666);
    int fd_inter = open(FIFO_INTER, O_RDONLY | O_NONBLOCK);
    int fd_apps  = open(FIFO_APPS,  O_RDONLY | O_NONBLOCK);
    if (fd_inter < 0 || fd_apps < 0) {
        perror("Erro ao abrir FIFOs");
        exit(1);
    }

    // Criação de processos filhos
    for (int i = 0; i < MAX_APPS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_IGN);
            execl("./process", "process", (char *)NULL);
            exit(0);
        }

        apps[i].pid = pid;
        apps[i].id = i + 1;
        apps[i].PC = 0;
        apps[i].state = READY;
        apps[i].access_D1 = apps[i].access_D2 = 0;
        safe_kill_pid(pid, SIGSTOP);
        enqueue_ready(i);
    }

    schedule_next();

    char buffer[128];
    unsigned char sizeBuffer;
    int sizeBufferInt;

    while (1) {
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
                        case BLOCKED_D1: printf("BLOCKED D1 (%c)", apps[i].blocked_op); break;
                        case BLOCKED_D2: printf("BLOCKED D2 (%c)", apps[i].blocked_op); break;
                        case TERMINATED: printf("TERMINATED"); break;
                    }
                    printf(" | D1=%d | D2=%d\n", apps[i].access_D1, apps[i].access_D2);
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

        ssize_t n0 = read(fd_inter, &sizeBuffer, sizeof(unsigned char));
        ssize_t n1 = read(fd_inter, buffer, sizeBuffer);

        if (n1 > 0) {
            buffer[n1 - 1] = '\0';
            if (strstr(buffer, "IRQ:0")) {
                printf("[Kernel] IRQ0 (Timer)\n");
                schedule_next();
            } else if (strstr(buffer, "IRQ:1")) {
                printf("[Kernel] IRQ1 (D1)\n");
                unblock_process(1);
            } else if (strstr(buffer, "IRQ:2")) {
                printf("[Kernel] IRQ2 (D2)\n");
                unblock_process(2);
            }
        }

        ssize_t n2 = read(fd_apps, &sizeBufferInt, sizeof(int));
        ssize_t n3 = read(fd_apps, buffer, sizeBufferInt);

        if (n3 > 0) {
            buffer[n3 - 1] = '\0';
            if (strncmp(buffer, "SYSCALL", 7) == 0) {
                int pid, pc, dev;
                char op;
                sscanf(buffer, "SYSCALL:%d:%d:%d:%c", &pid, &pc, &dev, &op);
                printf("Processo %d enviou syscall %c para o device %d\n", pid, op, dev);
                update_pc(pid, pc);
                handle_syscall(pid, dev, op);
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

        if (processosTerminados == MAX_APPS)
            break;

        usleep(100000);
    }

    close(fd_inter);
    close(fd_apps);
    unlink(FIFO_INTER);
    unlink(FIFO_APPS);

    printf("\n=== KERNEL FINALIZADO ===\n");
    for (int i = 0; i < MAX_APPS; i++) {
        printf("AP%d (PID %d): PC=%d | ", i+1, apps[i].pid, apps[i].PC);
        switch (apps[i].state) {
            case READY: printf("READY"); break;
            case RUNNING: printf("RUNNING"); break;
            case BLOCKED_D1: printf("BLOCKED D1 (%c)", apps[i].blocked_op); break;
            case BLOCKED_D2: printf("BLOCKED D2 (%c)", apps[i].blocked_op); break;
            case TERMINATED: printf("TERMINATED"); break;
        }
        printf(" | D1=%d | D2=%d\n", apps[i].access_D1, apps[i].access_D2);
    }

    return 0;
}
