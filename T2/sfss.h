#ifndef SFP_PROTOCOL_H
#define SFP_PROTOCOL_H

#include <stdint.h>

#define BLOCK_SIZE 16           
#define MAX_PATH 256            
#define SHM_SIZE   2048

// Tipos de Mensagem (Para o switch/case do servidor)
typedef enum {
    SFP_RD_REQ = 1,
    SFP_RD_REP = 2,
    SFP_WR_REQ = 3,
    SFP_WR_REP = 4,
    SFP_DC_REQ = 5,
    SFP_DC_REP = 6,
    SFP_DR_REQ = 7,
    SFP_DR_REP = 8,
    SFP_DL_REQ = 9,
    SFP_DL_REP = 10
} SFP_TYPE;




typedef struct {
    SFP_TYPE type;  
    int owner;
    char file[256];
    int offset;
    int dir;

} RD_REQ;

typedef struct {
    SFP_TYPE type;
    int owner;
    char path[256];       
    int strlen;
    char payload[16]; // sempre 16 bytes
    int offset;

} RD_REP;


typedef struct {
    SFP_TYPE type;
    int owner;
    int dir;
    char file[256];
    int offset;
    char payload[16];
} WR_REQ;

typedef struct {
    SFP_TYPE type;
    int owner;
    char path[256];
    int strlen;
    char payload[16];
    int offset; // se <0 => erro

} WR_REP;



typedef struct {
    SFP_TYPE type;
    int owner;
    char dirname[64];   // novo diretório
    int dir;
 
} DC_REQ;

typedef struct {
    SFP_TYPE type;
    int owner;
    char path[256]; // caminho original
    int strlen;        // se <0 => erro
} DC_REP;


typedef struct {
    SFP_TYPE type;
    int owner;
    char dirname[256];
    int dir;

} DR_REQ;


typedef struct {
    SFP_TYPE type;
    int owner;
    char path[256];
    int strlen; // <0 erro
} DR_REP;


typedef struct {
    SFP_TYPE type;
    int owner;
    int dir;

} DL_REQ;


typedef struct {
    SFP_TYPE type;
    int owner;
    char entries[512]; // nomes separados por vírgula
} DL_REP;



#endif


typedef struct {
    int has_message;     // 1 = tem REP nova do kernel
    char raw[SHM_SIZE - sizeof(int)];
} SHM_CONTAINER;










