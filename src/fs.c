#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "fs.h"
#include "kernel.h"
#include "seguridad.h"

#define MAGIC   0x4D494E49
#define MAX_REG 100

typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int total;
    unsigned int usados;
    unsigned int siguiente_id;
    unsigned int limpio;
} Superbloque;

typedef struct {
    int  id;
    int  activo;
    char nombre[50];
    char descripcion[100];
    char propietario[32];
} Registro;

static int disco_fd = -1;
static Superbloque sb;

static off_t pos_registro(int i) {
    return (off_t)sizeof(Superbloque) + (off_t)i * sizeof(Registro);
}

static int leer_registro(int i, Registro *r) {
    lseek(disco_fd, pos_registro(i), SEEK_SET);
    return read(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static int escribir_registro(int i, const Registro *r) {
    lseek(disco_fd, pos_registro(i), SEEK_SET);
    return write(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static void guardar_superbloque(void) {
    lseek(disco_fd, 0, SEEK_SET);
    (void)write(disco_fd, &sb, sizeof sb);
    fsync(disco_fd);
}

int fs_montar(const char *ruta) {
    mkdir(DIR_DATOS, 0755);
    disco_fd = open(ruta, O_RDWR | O_CREAT, 0600);
    if (disco_fd < 0) {
        perror("open disco");
        return -1;
    }

    if (read(disco_fd, &sb, sizeof sb) != sizeof sb || sb.magic != MAGIC) {
        printf("Disco sin formato: formateando...\n");
        memset(&sb, 0, sizeof sb);
        sb.magic = MAGIC;
        sb.version = 3;
        sb.total = MAX_REG;
        sb.usados = 0;
        sb.siguiente_id = 1;
        sb.limpio = 1;

        Registro vacio;
        memset(&vacio, 0, sizeof vacio);
        for (int i = 0; i < MAX_REG; i++) {
            escribir_registro(i, &vacio);
        }
    }

    if (!sb.limpio) {
        printf("ADVERTENCIA: El disco no se desmonteo correctamente.\n");
    }

    sb.limpio = 0;
    guardar_superbloque();
    return 0;
}

void fs_desmontar(void) {
    if (disco_fd >= 0) {
        sb.limpio = 1;
        guardar_superbloque();
        close(disco_fd);
        disco_fd = -1;
    }
}

void fs_crear(void) {
    if (sb.usados >= sb.total) {
        printf("Error: Disco lleno.\n");
        return;
    }

    int slot = -1;
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        printf("Error: No hay ranuras libres.\n");
        return;
    }

    memset(&r, 0, sizeof r);
    r.id = sb.siguiente_id++;
    r.activo = 1;
    leer_linea(" Nombre del registro: ", r.nombre, sizeof r.nombre);
    leer_linea(" Descripcion: ", r.descripcion, sizeof r.descripcion);
    strncpy(r.propietario, seg_usuario_actual(), sizeof r.propietario - 1);

    if (escribir_registro(slot, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        printf("Registro #%d creado exitosamente en ranura %d por %s.\n", r.id, slot, r.propietario);
    } else {
        printf("Error al escribir el registro en disco.\n");
    }
}

void fs_listar(void) {
    printf("\n%-6s %-20s %-30s %-15s\n", "ID", "NOMBRE", "DESCRIPCION", "PROPIETARIO");
    printf("-------------------------------------------------------------------------\n");
    Registro r;
    int mostrados = 0;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo) {
            printf("#%-5d %-20s %-30s %-15s\n", r.id, r.nombre, r.descripcion, r.propietario);
            mostrados++;
        }
    }
    printf("Total activos: %d / %d\n", mostrados, sb.total);
}

void fs_actualizar(void) {
    int id = leer_entero(" Ingrese ID a actualizar: ");
    if (id <= 0) return;

    Registro r;
    int slot = -1;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo && r.id == id) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        printf("Registro #%d no encontrado.\n", id);
        return;
    }

    if (seg_rol_actual() != ROL_ADMIN && strcmp(r.propietario, seg_usuario_actual()) != 0) {
        printf("ACCESO DENEGADO: Solo el propietario (%s) o el admin pueden editar este registro.\n", r.propietario);
        return;
    }

    leer_linea(" Nuevo nombre (ENTER para conservar): ", r.nombre, sizeof r.nombre);
    char nueva_desc[100];
    leer_linea(" Nueva descripcion (ENTER para conservar): ", nueva_desc, sizeof nueva_desc);
    if (strlen(nueva_desc) > 0) {
        strncpy(r.descripcion, nueva_desc, sizeof r.descripcion - 1);
    }

    if (escribir_registro(slot, &r) == 0) {
        guardar_superbloque();
        printf("Registro #%d actualizado correctamente.\n", id);
    }
}

void fs_borrar(void) {
    int id = leer_entero(" Ingrese ID a eliminar: ");
    if (id <= 0) return;

    Registro r;
    int slot = -1;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo && r.id == id) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        printf("Registro #%d no encontrado.\n", id);
        return;
    }

    if (seg_rol_actual() != ROL_ADMIN && strcmp(r.propietario, seg_usuario_actual()) != 0) {
        printf("ACCESO DENEGADO: Solo el propietario (%s) o el admin pueden eliminar este registro.\n", r.propietario);
        return;
    }

    r.activo = 0;
    if (escribir_registro(slot, &r) == 0) {
        if (sb.usados > 0) sb.usados--;
        guardar_superbloque();
        printf("Registro #%d eliminado (ranura %d liberada).\n", id, slot);
    }
}

void fs_estado(void) {
    printf("\n=== ESTADO DEL DISCO VIRTUAL ===\n");
    printf("Magic: 0x%X | Version: %d | Estado: %s\n",
           sb.magic, sb.version, sb.limpio ? "Limpio" : "En uso / Sucio");
    printf("Capacidad: %d registros | Usados: %d | Libres: %d | Sig. ID: %d\n",
           sb.total, sb.usados, sb.total - sb.usados, sb.siguiente_id);
    printf("\nMapa de Ocupacion (primeras 50 ranuras):\n");
    Registro r;
    for (int i = 0; i < 50 && i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo) {
            printf("[X]");
        } else {
            printf("[.]");
        }
        if ((i + 1) % 10 == 0) printf("\n");
    }
    printf("=================================\n");
}
