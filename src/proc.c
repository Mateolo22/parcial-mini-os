/* Reemplazar src/proc.c */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include "kernel.h"
#include "seguridad.h"
#include "proc.h"

#define MAX_PROC 20

typedef enum { EJECUTANDO, SUSPENDIDO, TERMINADO } Estado;

typedef struct {
    pid_t  pid;
    char   nombre[32];
    char   usuario[32];
    Estado estado;
    time_t inicio;
} PCB;

static PCB tabla_proc[MAX_PROC];
static int total_proc = 0;

static const char *estado_str(Estado e) {
    if (e == EJECUTANDO) return "EJECUTANDO";
    if (e == SUSPENDIDO) return "SUSPENDIDO";
    return "TERMINADO";
}

static void esperar_hijo(PCB *p) {
    int st;
    waitpid(p->pid, &st, WUNTRACED);
    p->estado = WIFSTOPPED(st) ? SUSPENDIDO : TERMINADO;
}

void proc_lanzar(const char *programa) {
    if (total_proc >= MAX_PROC) { printf("Tabla de procesos llena.\n"); return; }
    
    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT,  SIG_IGN);

    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGTSTP, SIG_DFL);
        signal(SIGINT,  SIG_DFL);
        execlp(programa, programa, (char *)NULL);
        perror("exec");
        _exit(127);
    }

    PCB *p = &tabla_proc[total_proc++];
    p->pid = pid;
    strncpy(p->nombre, programa, sizeof p->nombre - 1);
    strncpy(p->usuario, seg_usuario_actual(), sizeof p->usuario - 1);
    p->estado = EJECUTANDO;
    p->inicio = time(NULL);

    esperar_hijo(p);
    signal(SIGINT, SIG_DFL);
}

void proc_listar(void) {
    printf("\n%-6s %-15s %-12s %-12s %-20s\n", "PID", "PROGRAMA", "DUEÑO", "ESTADO", "HORA INICIO");
    printf("-------------------------------------------------------------------\n");
    for (int i = 0; i < total_proc; i++) {
        char tbuf[32];
        struct tm *tmv = localtime(&tabla_proc[i].inicio);
        strftime(tbuf, sizeof tbuf, "%H:%M:%S %Y-%m-%d", tmv);
        printf("%-6d %-15s %-12s %-12s %-20s\n",
               tabla_proc[i].pid, tabla_proc[i].nombre, tabla_proc[i].usuario,
               estado_str(tabla_proc[i].estado), tbuf);
    }
}

void proc_reanudar_id(pid_t pid) {
    for (int i = 0; i < total_proc; i++) {
        if (tabla_proc[i].pid == pid && tabla_proc[i].estado == SUSPENDIDO) {
            if (strcmp(seg_rol_actual(), "admin") != 0 && strcmp(tabla_proc[i].usuario, seg_usuario_actual()) != 0) {
                printf("  ACCESO DENEGADO: No es el propietario del proceso.\n");
                return;
            }
            signal(SIGTSTP, SIG_IGN);
            signal(SIGINT,  SIG_IGN);
            kill(pid, SIGCONT);
            tabla_proc[i].estado = EJECUTANDO;
            esperar_hijo(&tabla_proc[i]);
            signal(SIGINT, SIG_DFL);
            printf("Proceso reanudado y finalizado/suspendido.\n");
            return;
        }
    }
    printf("Proceso suspendido no encontrado.\n");
}

void proc_terminar_id(pid_t pid) {
    for (int i = 0; i < total_proc; i++) {
        if (tabla_proc[i].pid == pid && tabla_proc[i].estado != TERMINADO) {
            if (strcmp(seg_rol_actual(), "admin") != 0 && strcmp(tabla_proc[i].usuario, seg_usuario_actual()) != 0) {
                printf("  ACCESO DENEGADO: No es el propietario del proceso.\n");
                return;
            }
            kill(pid, SIGTERM);
            kill(pid, SIGCONT);
            int st;
            waitpid(pid, &st, 0);
            tabla_proc[i].estado = TERMINADO;
            printf("Proceso %d terminado sin zombis.\n", pid);
            return;
        }
    }
    printf("Proceso no encontrado.\n");
}

void proc_lanzar_menu(void) {
    printf("\n--- GESTIÓN DE PROCESOS ---\n");
    printf("1. Lanzar Tetris (BSDGames)\n2. Listar procesos\n3. Reanudar proceso suspendido\n4. Terminar proceso\nOpción: ");
    int op = leer_entero("");
    if (op == 1) proc_lanzar("tetris-bsd");
    else if (op == 2) proc_listar();
    else if (op == 3) {
        int pid = leer_entero("PID a reanudar: ");
        if (pid > 0) proc_reanudar_id((pid_t)pid);
    } else if (op == 4) {
        int pid = leer_entero("PID a terminar: ");
        if (pid > 0) proc_terminar_id((pid_t)pid);
    }
}
