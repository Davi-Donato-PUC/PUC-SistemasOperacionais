#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#define EVER ;;

void intHandler(int sinal);
void quitHandler(int sinal);

int main (void) {
    for(EVER);
    }

void intHandler(int sinal) {
    printf("Você pressionou Ctrl-C (%d) \n", sinal);
    }

void quitHandler(int sinal) {
    printf("Terminando o processo...\n");
    exit (0);
    }