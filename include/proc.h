#ifndef PROC_H
#define PROC_H

#include <sys/types.h>
#include <time.h>

typedef enum { EJECUTANDO, SUSPENDIDO, TERMINADO } Estado;

typedef struct {
    pid_t  pid;
    char   nombre[32];
    char   usuario[32];
    Estado estado;
    time_t inicio;
} PCB;

void proc_menu(void);
void proc_lanzar(PCB *p, const char *programa);

#endif
