#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "proc.h"
#include "kernel.h"
#include "seguridad.h"

#define MAX_PROC 32

static PCB tabla_procesos[MAX_PROC];
static int num_procesos = 0;

static void esperar_hijo(PCB *p) {
    int st;
    waitpid(p->pid, &st, WUNTRACED);
    if (WIFSTOPPED(st)) {
        p->estado = SUSPENDIDO;
        printf("\nProceso [%s] (PID %d) SUSPENDIDO con Ctrl+Z.\n", p->nombre, p->pid);
    } else {
        p->estado = TERMINADO;
        printf("\nProceso [%s] (PID %d) TERMINADO.\n", p->nombre, p->pid);
    }
}

void proc_lanzar(PCB *p, const char *programa) {
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

    p->pid = pid;
    strncpy(p->nombre, programa, sizeof p->nombre - 1);
    strncpy(p->usuario, seg_usuario_actual(), sizeof p->usuario - 1);
    p->estado = EJECUTANDO;
    p->inicio = time(NULL);

    esperar_hijo(p);

    signal(SIGTSTP, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
}

static void listar_procesos(void) {
    printf("\n%-8s %-15s %-15s %-12s\n", "PID", "PROGRAMA", "USUARIO", "ESTADO");
    printf("----------------------------------------------------------\n");
    for (int i = 0; i < num_procesos; i++) {
        const char *est_str = "DESCONOCIDO";
        if (tabla_procesos[i].estado == EJECUTANDO) est_str = "EJECUTANDO";
        else if (tabla_procesos[i].estado == SUSPENDIDO) est_str = "SUSPENDIDO";
        else if (tabla_procesos[i].estado == TERMINADO) est_str = "TERMINADO";

        printf("%-8d %-15s %-15s %-12s\n",
               tabla_procesos[i].pid,
               tabla_procesos[i].nombre,
               tabla_procesos[i].usuario,
               est_str);
    }
    printf("----------------------------------------------------------\n");
}

static void reanudar_proceso(void) {
    int pid = leer_entero(" PID del proceso a reanudar: ");
    for (int i = 0; i < num_procesos; i++) {
        if (tabla_procesos[i].pid == pid && tabla_procesos[i].estado == SUSPENDIDO) {
            signal(SIGTSTP, SIG_IGN);
            signal(SIGINT,  SIG_IGN);

            kill(tabla_procesos[i].pid, SIGCONT);
            tabla_procesos[i].estado = EJECUTANDO;
            printf("Reanudando proceso %d...\n", pid);
            esperar_hijo(&tabla_procesos[i]);

            signal(SIGTSTP, SIG_DFL);
            signal(SIGINT,  SIG_DFL);
            return;
        }
    }
    printf("Proceso suspendido con PID %d no encontrado.\n", pid);
}

static void terminar_proceso(void) {
    int pid = leer_entero(" PID del proceso a terminar: ");
    for (int i = 0; i < num_procesos; i++) {
        if (tabla_procesos[i].pid == pid && tabla_procesos[i].estado != TERMINADO) {
            if (seg_rol_actual() != ROL_ADMIN && strcmp(tabla_procesos[i].usuario, seg_usuario_actual()) != 0) {
                printf("ACCESO DENEGADO: Solo el dueño (%s) o admin pueden terminar este proceso.\n", tabla_procesos[i].usuario);
                return;
            }
            kill(tabla_procesos[i].pid, SIGTERM);
            kill(tabla_procesos[i].pid, SIGCONT);
            int st;
            waitpid(tabla_procesos[i].pid, &st, 0);
            tabla_procesos[i].estado = TERMINADO;
            printf("Proceso %d terminado limpiamente (sin zombi).\n", pid);
            return;
        }
    }
    printf("Proceso activo con PID %d no encontrado.\n", pid);
}

void proc_menu(void) {
    while (1) {
        printf("\n=== GESTION DE PROCESOS Y BSDGAMES ===\n");
        printf("1. Lanzar Tetris (tetris-bsd)\n");
        printf("2. Lanzar Snake (snake)\n");
        printf("3. Listar Tabla de Procesos\n");
        printf("4. Reanudar Proceso Suspendido\n");
        printf("5. Terminar Proceso\n");
        printf("0. Volver al Menú Principal\n");
        printf("=======================================\n");

        int op = leer_entero(" Opción: ");
        if (op == 0) break;

        switch (op) {
            case 1:
            case 2: {
                if (num_procesos >= MAX_PROC) {
                    printf("Tabla de procesos llena.\n");
                    break;
                }
                const char *prog = (op == 1) ? "tetris-bsd" : "snake";
                proc_lanzar(&tabla_procesos[num_procesos], prog);
                num_procesos++;
                break;
            }
            case 3: listar_procesos(); break;
            case 4: reanudar_proceso(); break;
            case 5: terminar_proceso(); break;
            default: printf("Opción inválida.\n"); break;
        }
    }
}
