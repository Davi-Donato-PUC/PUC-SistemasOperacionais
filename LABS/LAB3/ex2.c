#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

void trataSIGKILL(int sinal) {
    printf("SINAL : %d\n", sinal);
    exit(0);
    }

int main() {
    signal(SIGKILL, trataSIGKILL);
    raise(SIGKILL);
    return 0;
    }

