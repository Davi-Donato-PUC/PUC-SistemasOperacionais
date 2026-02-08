#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

int main() {

    int fd[2];
    pipe(fd);
    pid_t pid = fork();

    if (pid == 0) {
        // filho: redireciona stdout -> pipe write
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]); close(fd[1]);
        execlp("ls", "ls", NULL);
        perror("execlp ls"); exit(1);
    } else {
        // pai: redireciona stdin <- pipe read
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]); close(fd[1]);
        execlp("wc", "wc", "-l", NULL);
        perror("execlp wc"); exit(1);
    }


    return 0;
    }