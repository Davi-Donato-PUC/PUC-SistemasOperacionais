// sfss_server_structs.c
// Servidor UDP que recebe structs SFP_*_REQ e responde com structs SFP_*_REP
// Compile: gcc -Wall -Wextra -o sfss_server_structs sfss_server_structs.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "sfss.h"

#define PORTNO 9000
#define BUF_SIZE 2048
#define ROOT "SFSS_root_dir"



ssize_t safe_recvfrom(int sockfd, void *buf, size_t len, struct sockaddr_in *client, socklen_t *addrlen) {
    ssize_t n = recvfrom(sockfd, buf, len, 0, (struct sockaddr *)client, addrlen);
    return n;
}

ssize_t send_struct_reply(int sockfd, const void *buf, size_t len, struct sockaddr_in *client, socklen_t addrlen) {
    ssize_t s = sendto(sockfd, buf, len, 0, (struct sockaddr *)client, addrlen);
    return s;
}

// ---- servidor -------------------------------------------------------------
int main() {
    int sockfd;
    unsigned char buffer[BUF_SIZE];
    struct sockaddr_in server_addr, client_addr;
    socklen_t len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORTNO);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("[SFSS] Servidor UDP ativo na porta %d\n", PORTNO);

    while (1) {
        ssize_t n = safe_recvfrom(sockfd, buffer, sizeof(buffer), &client_addr, &len);
        if (n <= 0) continue;

        // Garantir que há pelo menos o campo SFP_TYPE + owner no buffer
        if ((size_t)n < sizeof(SFP_TYPE)) { fprintf(stderr, "[SFSS] pacote curto recebido (%zd bytes)\n", n); continue; }

        SFP_TYPE type = *((SFP_TYPE *)buffer);

        // ---- RD_REQ -------------------------------------------------------
        if (type == SFP_RD_REQ) {
            if ((size_t)n < sizeof(RD_REQ)) { fprintf(stderr, "[SFSS] RD_REQ tamanho inválido (%zd)\n", n); continue; } // ERRO
            RD_REQ req;
            memcpy(&req, buffer, sizeof(RD_REQ));

            // Determinar base_dir conforme sua lógica
            char base_dir[128];
            if (req.dir == 0) snprintf(base_dir, sizeof(base_dir), ROOT "/A0");
            else snprintf(base_dir, sizeof(base_dir), ROOT "/A%d", req.owner);
            

            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", base_dir, req.file);
            

            RD_REP rep;
            memset(&rep, 0, sizeof(rep));
            rep.type = SFP_RD_REP;
            rep.owner = req.owner;
            strncpy(rep.path, fullpath, sizeof(rep.path)-1);

            int fd = open(fullpath, O_RDONLY);
            if (fd < 0) {
                memset(rep.payload, 0, sizeof(rep.payload));
                rep.strlen = -1;
            } else {
                off_t off = lseek(fd, req.offset, SEEK_SET);
                (void)off;
                ssize_t rr = read(fd, rep.payload, sizeof(rep.payload)); // lê até 16 bytes
                if (rr < 0) {
                    memset(rep.payload, 0, sizeof(rep.payload));
                    rep.strlen = -1;
                    close(fd);
                    }
                    rep.strlen = strlen(fullpath);
                }
            send_struct_reply(sockfd, &rep, sizeof(rep), &client_addr, len);
            printf("[SFSS] RD_REP enviada owner=%d path='%s' \n", rep.owner, rep.path);
        }

        // ---- WR_REQ -------------------------------------------------------
        else if (type == SFP_WR_REQ) {
            if ((size_t)n < sizeof(WR_REQ)) { fprintf(stderr, "[SFSS] WR_REQ tamanho inválido (%zd)\n", n); continue; } // ERRO 

            WR_REQ req;
            memcpy(&req, buffer, sizeof(WR_REQ));

            char base_dir[128];
            if (req.dir == 0) snprintf(base_dir, sizeof(base_dir), ROOT "/A0");
            else snprintf(base_dir, sizeof(base_dir), ROOT "/A%d", req.owner);

            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", base_dir, req.file);

            WR_REP rep;
            memset(&rep, 0, sizeof(rep));
            rep.type = SFP_WR_REP;
            rep.owner = req.owner;
            strncpy(rep.path, fullpath, sizeof(rep.path)-1);
            rep.strlen = strlen(fullpath);

            int fd = open(fullpath, O_CREAT | O_RDWR, 0666);
            if (fd < 0) { 
                memset(rep.payload, 0, sizeof(rep.payload));
                rep.strlen = -1;                
                } 
            else {
                off_t size = lseek(fd, 0, SEEK_END);
                if (req.offset > size) {
                    // preencher com espaços
                    lseek(fd, size, SEEK_SET);
                    int gap = req.offset - (int)size;
                    char spaces[256];
                    memset(spaces, ' ', sizeof(spaces));
                    while (gap > 0) {
                        int chunk = gap > 256 ? 256 : gap;
                        write(fd, spaces, chunk);
                        gap -= chunk;
                        }
                }
                lseek(fd, req.offset, SEEK_SET);
                write(fd, req.payload, strlen(req.payload));
                close(fd);
            }


            send_struct_reply(sockfd, &rep, sizeof(rep), &client_addr, len);
            printf("[SFSS] WR_REP enviada owner=%d path='%s' offset=%d\n", rep.owner, rep.path, rep.offset);
        }

        // ---- DC_REQ (criar diretório) -------------------------------------
        else if (type == SFP_DC_REQ) {
            if ((size_t)n < sizeof(DC_REQ)) { fprintf(stderr, "[SFSS] DC_REQ tamanho inválido (%zd)\n", n); continue; } // ERRO

            DC_REQ req;
            memcpy(&req, buffer, sizeof(DC_REQ));

            char base_dir[128];
            if (req.dir == 0) snprintf(base_dir, sizeof(base_dir), ROOT "/A0");
            else snprintf(base_dir, sizeof(base_dir), ROOT "/A%d", req.owner);
            
            DC_REP rep;
            char newdir[512];
            int written = snprintf(newdir, sizeof(newdir), "%s/%s", base_dir, req.dirname);
            memset(&rep, 0, sizeof(rep));
            rep.type = SFP_DC_REP;
            rep.owner = req.owner;
            //strncpy(rep.path, base_dir, sizeof(rep.path)-1);

            if (written < 0 || written >= (int)sizeof(newdir)) { rep.strlen = -1; } 
            else {
                if ( mkdir(newdir, 0777) < 0 && errno != EEXIST) { rep.strlen = -1; } 
                else { strncpy(rep.path, newdir, sizeof(rep.path)-1); rep.strlen = (int)strlen(rep.path); }
                }

            send_struct_reply(sockfd, &rep, sizeof(rep), &client_addr, len);
            printf("[SFSS] DC_REP enviada owner=%d newpath='%s' strlen=%d\n", rep.owner, rep.path, rep.strlen);
        }

        // ---- DR_REQ (remover arquivo/diretório) ---------------------------
        else if (type == SFP_DR_REQ) {
            if ((size_t)n < sizeof(DR_REQ)) { fprintf(stderr, "[SFSS] DR_REQ tamanho inválido (%zd)\n", n); continue; }

            DR_REQ req;
            memcpy(&req, buffer, sizeof(DR_REQ));

            char base_dir[128];
            if (req.dir == 0) snprintf(base_dir, sizeof(base_dir), ROOT "/A0");
            else snprintf(base_dir, sizeof(base_dir), ROOT "/A%d", req.owner);

            char tgt[512];
            int written = snprintf(tgt, sizeof(tgt), "%s/%s", base_dir, req.dirname);
            DR_REP rep;
            memset(&rep, 0, sizeof(rep));
            rep.type = SFP_DR_REP;
            rep.owner = req.owner;
            strncpy(rep.path, req.dirname, sizeof(req.dirname)-1);

            if (written < 0 || written >= (int)sizeof(tgt)) { rep.strlen = -1;  } 
            else {
                if ( rmdir(tgt) == 0) {  rep.strlen = (int)strlen(rep.path); }
                else { strncpy(rep.path, tgt, sizeof(rep.path)-1); rep.strlen = -1;}
                }

            send_struct_reply(sockfd, &rep, sizeof(rep), &client_addr, len);
            printf("[SFSS] DR_REP enviada owner=%d path='%s' strlen=%d\n", rep.owner, rep.path, rep.strlen);
        }

        // ---- DL_REQ (listar diretório) ------------------------------------
        else if (type == SFP_DL_REQ) {
            if ((size_t)n < sizeof(DL_REQ)) { fprintf(stderr, "[SFSS] DL_REQ tamanho inválido (%zd)\n", n); continue; }
            DL_REQ req;
            memcpy(&req, buffer, sizeof(DL_REQ));

            char base_dir[128];
            if (req.dir == 0) snprintf(base_dir, sizeof(base_dir), ROOT "/A0");
            else snprintf(base_dir, sizeof(base_dir), ROOT "/A%d", req.owner);
           
            DL_REP rep;
            memset(&rep, 0, sizeof(rep));
            rep.type = SFP_DL_REP;
            rep.owner = req.owner;
            rep.entries[0] = '\0';

            DIR *d = opendir(base_dir);
            if (!d) { strncpy(rep.entries, "ERR", sizeof(rep.entries)-1); } 
            else {
                struct dirent *e;
                int first = 1;
                while ((e = readdir(d)) != NULL) {
                    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                    if (!first) strncat(rep.entries, ",", sizeof(rep.entries)-strlen(rep.entries)-1);
                    strncat(rep.entries, e->d_name, sizeof(rep.entries)-strlen(rep.entries)-1);
                    printf("%s\n", e->d_name);
                    first = 0;
                }
                closedir(d);
            }

            send_struct_reply(sockfd, &rep, sizeof(rep), &client_addr, len);
            printf("[SFSS] DL_REP enviada owner=%d path='%s' entries='%s'\n", rep.owner, base_dir, rep.entries);
        }

        else { fprintf(stderr, "[SFSS] Tipo desconhecido recebido: %d\n", (int)type); continue; } 
    }



    close(sockfd);
    return 0;
}
