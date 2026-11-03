#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel.h"
#include "seguridad.h"

#define ARCH_MATRIZ DIR_DATOS "matriz.dat"

static unsigned char matriz[NUM_ROLES][NUM_OBJETOS] = {
    /* ADMIN    */ { D_CREATE|D_READ|D_UPDATE|D_DELETE, D_EXEC, D_READ|D_UPDATE, D_READ|D_UPDATE|D_DELETE, D_READ|D_UPDATE, D_EXEC },
    /* OPERADOR */ { D_CREATE|D_READ|D_UPDATE,          D_EXEC, D_CREATE|D_READ|D_UPDATE, D_READ|D_UPDATE|D_DELETE, D_READ,          0      },
    /* INVITADO */ { D_READ,                            D_EXEC, 0,                        D_READ,                   0,               0      },
};

static const char *NOMBRE_ROL[NUM_ROLES] = { "admin", "operador", "invitado" };
static const char *NOMBRE_OBJETO[NUM_OBJETOS] = { "registros", "juegos", "partidas", "procesos", "matriz", "sistema" };

static int rol_actual = -1;

void seg_iniciar_sesion(void) {
    int r;
    do {
        r = leer_entero("\nSeleccione Rol (0=admin, 1=operador, 2=invitado): ");
        if (feof(stdin)) exit(0);
    } while (r < 0 || r >= NUM_ROLES);
    rol_actual = r;
    printf("Sesión iniciada como usuario: %s\n", NOMBRE_ROL[rol_actual]);
}

const char *seg_usuario_actual(void) {
    return rol_actual < 0 ? "-" : NOMBRE_ROL[rol_actual];
}

int seg_rol_actual(void) {
    return rol_actual;
}

int seg_verificar(Objeto obj, unsigned char derecho) {
    if (rol_actual < 0) return 0;
    if ((matriz[rol_actual][obj] & derecho) == derecho) return 1;
    printf("  ACCESO DENEGADO (Permisos insuficientes para %s)\n", NOMBRE_OBJETO[obj]);
    return 0;
}

void seg_guardar_matriz(void) {
    mkdir(DIR_DATOS, 0755);
    int fd = open(ARCH_MATRIZ, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("open matriz"); return; }
    (void)write(fd, matriz, sizeof matriz);
    fsync(fd);
    close(fd);
}

void seg_cargar_matriz(void) {
    int fd = open(ARCH_MATRIZ, O_RDONLY);
    if (fd < 0) {
        seg_guardar_matriz();
        return;
    }
    if (read(fd, matriz, sizeof matriz) != sizeof matriz) {
        printf("matriz.dat dañada: usando matriz por defecto.\n");
    }
    close(fd);
}

static void fmt_derechos(unsigned char d, char *buf) {
    buf[0] = (d & D_CREATE) ? 'C' : '-';
    buf[1] = (d & D_READ)   ? 'R' : '-';
    buf[2] = (d & D_UPDATE) ? 'U' : '-';
    buf[3] = (d & D_DELETE) ? 'D' : '-';
    buf[4] = (d & D_EXEC)   ? 'X' : '-';
    buf[5] = '\0';
}

void seg_ver_matriz(void) {
    printf("\n=== MATRIZ DE ACCESO DE MINIOS ===\n");
    printf("%-12s", "ROL / OBJ");
    for (int o = 0; o < NUM_OBJETOS; o++) {
        printf("%-12s", NOMBRE_OBJETO[o]);
    }
    printf("\n----------------------------------------------------------------------------------\n");

    for (int r = 0; r < NUM_ROLES; r++) {
        printf("%-12s", NOMBRE_ROL[r]);
        for (int o = 0; o < NUM_OBJETOS; o++) {
            char dstr[10];
            fmt_derechos(matriz[r][o], dstr);
            printf("%-12s", dstr);
        }
        printf("\n");
    }
    printf("----------------------------------------------------------------------------------\n");
    printf("Leyenda: C=Create, R=Read, U=Update, D=Delete, X=Exec\n");
}

void seg_modificar_matriz(void) {
    if (rol_actual != ROL_ADMIN) {
        printf("ACCESO DENEGADO: Solo el administrador puede modificar la matriz.\n");
        return;
    }

    printf("\n--- Modificar Matriz de Acceso ---\n");
    int r = leer_entero(" Rol a modificar (0=admin, 1=operador, 2=invitado): ");
    if (r < 0 || r >= NUM_ROLES) { printf("Rol inválido.\n"); return; }

    int o = leer_entero(" Objeto (0=registros, 1=juegos, 2=partidas, 3=procesos, 4=matriz, 5=sistema): ");
    if (o < 0 || o >= NUM_OBJETOS) { printf("Objeto inválido.\n"); return; }

    int nuevo_val = leer_entero(" Nueva máscara de bits (0-31, ej: 31=CRUDX, 2=R, 0=Ninguno): ");
    if (nuevo_val < 0 || nuevo_val > 31) { printf("Valor inválido.\n"); return; }

    if (r == ROL_ADMIN && o == OBJ_MATRIZ && !(nuevo_val & D_UPDATE)) {
        printf("Error de Seguridad: El admin no puede revocarse a sí mismo el permiso UPDATE sobre la MATRIZ.\n");
        return;
    }

    matriz[r][o] = (unsigned char)nuevo_val;
    seg_guardar_matriz();
    printf("Matriz actualizada y guardada en disco exitosamente.\n");
}
