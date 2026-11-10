#ifndef PROC_H
#define PROC_H

#include <sys/types.h>

void proc_lanzar_menu(void);
void proc_listar(void);
void proc_reanudar_id(pid_t pid);
void proc_terminar_id(pid_t pid);

#endif
