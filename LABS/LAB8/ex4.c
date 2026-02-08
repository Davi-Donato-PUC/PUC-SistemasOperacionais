#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void listar_diretorio(const char *path, int nivel) {
    struct dirent *entrada;
    struct stat info;
    DIR *dir;
    char caminho[1024];

    dir = opendir(path);
    if (!dir) {
        perror("Erro ao abrir diretório");
        return;
    }

    while ((entrada = readdir(dir)) != NULL) {
        // Ignorar "." e ".."
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;

        snprintf(caminho, sizeof(caminho), "%s/%s", path, entrada->d_name);
        stat(caminho, &info);

        for (int i = 0; i < nivel; i++) printf("  "); // Identação

        if (S_ISDIR(info.st_mode)) {
            printf("[DIR]  %s\n", entrada->d_name);
            listar_diretorio(caminho, nivel + 1);
        } else if (S_ISREG(info.st_mode)) {
            printf("[ARQ]  %s (%ld bytes)\n", entrada->d_name, info.st_size);
        }
    }

    closedir(dir);
}

int main() {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));

    printf("Listando diretório: %s\n", cwd);
    listar_diretorio(cwd, 0);

    return 0;
}