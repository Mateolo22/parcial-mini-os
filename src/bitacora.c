#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include "kernel.h"
#include "seguridad.h"
#include "bitacora.h"

#define ARCH_BITACORA DIR_DATOS "bitacora.log"

void auditar(const char *accion, const char *objeto, const char *resultado) {
    char fecha[32], linea[256];
    time_t t = time(NULL);
    strftime(fecha, sizeof fecha, "%Y-%m-%d %H:%M:%S", localtime(&t));
    
    int n = snprintf(linea, sizeof linea, "%s | %-10s | %-8s | %-14s | %-10s | %s\n",
                     fecha, seg_usuario_actual(), seg_rol_actual(), accion, objeto, resultado);
    
    int fd = open(ARCH_BITACORA, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    write(fd, linea, n);
    close(fd);
}

void bitacora_mostrar(void) {
    FILE *f = fopen(ARCH_BITACORA, "r");
    if (!f) { printf("Bitácora vacía.\n"); return; }

    char linea[256], top_user[32] = "-";
    int denegados = 0, fallidos = 0, max_denies = 0;

    struct ConteoUser { char user[32]; int count; } conteos[50];
    int n_users = 0;

    printf("\n------- BITÁCORA DEL SISTEMA -------\n");
    while (fgets(linea, sizeof linea, f)) {
        printf("%s", linea);
        if (strstr(linea, "DENEGADO")) {
            denegados++;
            char u[32];
            if (sscanf(linea, "%*s %*s | %31s", u) == 1 && strcmp(u, "-") != 0) {
                int eno = 0;
                for (int i = 0; i < n_users; i++) {
                    if (strcmp(conteos[i].user, u) == 0) {
                        conteos[i].count++;
                        eno = 1;
                        break;
                    }
                }
                if (!eno && n_users < 50) {
                    strncpy(conteos[n_users].user, u, 31);
                    conteos[n_users].count = 1;
                    n_users++;
                }
            }
        }
        if (strstr(linea, "LOGIN FALLIDO")) fallidos++;
    }
    fclose(f);

    for (int i = 0; i < n_users; i++) {
        if (conteos[i].count > max_denies) {
            max_denies = conteos[i].count;
            strncpy(top_user, conteos[i].user, sizeof top_user - 1);
        }
    }

    printf("------------------------------------\n");
    printf("Resumen: %d accesos denegados | %d logins fallidos\n", denegados, fallidos);
    printf("Usuario con más denegaciones: %s (%d veces)\n", top_user, max_denies);
}
