#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


long long somar_tamanhos(const char *path) {
    struct dirent *entrada;
    struct stat info;
    DIR *dir;
    long long total = 0;
    char caminho[1024];

    dir = opendir(path);
    if (!dir) {
        perror("Erro ao abrir diretório");
        return 0;
    }

    while ((entrada = readdir(dir)) != NULL) {
        // Ignorar "." e ".."
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;

        snprintf(caminho, sizeof(caminho), "%s/%s", path, entrada->d_name);

        if (stat(caminho, &info) == 0) {
            if (S_ISDIR(info.st_mode)) {
                // Recursão para subdiretórios
                total += somar_tamanhos(caminho);
            } else if (S_ISREG(info.st_mode)) {
                // Somar tamanho de arquivo
                total += info.st_size;
            }
        }
    }

    closedir(dir);
    return total;
}

int main() {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));

    long long total = somar_tamanhos(cwd);
    printf("Tamanho total dos arquivos em '%s': %lld bytes\n", cwd, total);

    return 0;
}