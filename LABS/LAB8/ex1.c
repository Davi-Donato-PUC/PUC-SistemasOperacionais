#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

int main() {
    DIR *dir;
    struct dirent *entrada;
    struct stat info;
    char caminho[1024];
    int contador = 0;
    time_t agora = time(NULL);

    // Abre diretório corrente
    dir = opendir(".");
    if (!dir) {
        perror("Erro ao abrir diretório");
        return 1;
    }

    while ((entrada = readdir(dir)) != NULL) {
        // Ignorar "." e ".."
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;

        snprintf(caminho, sizeof(caminho), "./%s", entrada->d_name);

        if (stat(caminho, &info) == 0) {
            contador++;

            // Calcular idade em dias
            double diff = difftime(agora, info.st_mtime) / (60 * 60 * 24);
            int dias = (int) diff;

            printf("%s inode %lu size: %ld age: %d days\n",
                   entrada->d_name,
                   info.st_ino,
                   info.st_size,
                   dias);
        }
    }

    closedir(dir);
    printf("Number of files = %d\n", contador);
    return 0;
}