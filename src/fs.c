#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "kernel.h"
#include "seguridad.h"
#include "fs.h"

#define MAGIC   0x4D494E49
#define MAX_REG 100

typedef struct {
    unsigned int magic, version, total, usados, siguiente_id, limpio;
} Superbloque;

typedef struct {
    int  id, activo;
    char nombre[50], descripcion[100], propietario[32];
} Registro;

static int fd_disco = -1, fd_espejo = -1;
static Superbloque sb;

static off_t pos_registro(int i) {
    return (off_t)sizeof(Superbloque) + (off_t)i * sizeof(Registro);
}

static int escribir_en_ambos(off_t pos, const void *datos, size_t n) {
    int ok = 0;
    if (lseek(fd_disco, pos, SEEK_SET) >= 0 && write(fd_disco, datos, n) == (ssize_t)n) ok++;
    if (lseek(fd_espejo, pos, SEEK_SET) >= 0 && write(fd_espejo, datos, n) == (ssize_t)n) ok++;
    return ok == 2 ? 0 : -1;
}

static int copiar_disco(int origen, int destino) {
    char bloque[4096];
    ssize_t n;
    lseek(origen, 0, SEEK_SET);
    lseek(destino, 0, SEEK_SET);
    while ((n = read(origen, bloque, sizeof bloque)) > 0) {
        if (write(destino, bloque, n) != n) return -1;
    }
    fsync(destino);
    return 0;
}

static void guardar_superbloque(void) {
    escribir_en_ambos(0, &sb, sizeof sb);
    if (fd_disco >= 0) fsync(fd_disco);
    if (fd_espejo >= 0) fsync(fd_espejo);
}

int fs_montar(const char *ruta_principal, const char *ruta_espejo) {
    fd_disco  = open(ruta_principal, O_RDWR | O_CREAT, 0600);
    fd_espejo = open(ruta_espejo,    O_RDWR | O_CREAT, 0600);
    if (fd_disco < 0 || fd_espejo < 0) return -1;

    Superbloque sb1, sb2;
    int ok1 = (read(fd_disco,  &sb1, sizeof sb1) == sizeof sb1 && sb1.magic == MAGIC);
    int ok2 = (read(fd_espejo, &sb2, sizeof sb2) == sizeof sb2 && sb2.magic == MAGIC);

    if (!ok1 && ok2) {
        printf("ADVERTENCIA: Disco principal dañado. Reconstruyendo desde el espejo...\n");
        copiar_disco(fd_espejo, fd_disco);
        sb = sb2;
    } else if (ok1 && !ok2) {
        printf("ADVERTENCIA: Disco espejo dañado. Reconstruyendo desde el principal...\n");
        copiar_disco(fd_disco, fd_espejo);
        sb = sb1;
    } else if (!ok1 && !ok2) {
        printf("Formateando arreglo de discos espejo (RAID 1)...\n");
        memset(&sb, 0, sizeof sb);
        sb.magic = MAGIC; sb.version = 3; sb.total = MAX_REG;
        sb.siguiente_id = 1; sb.limpio = 1;
        Registro vacio; memset(&vacio, 0, sizeof vacio);
        for (int i = 0; i < MAX_REG; i++) escribir_en_ambos(pos_registro(i), &vacio, sizeof vacio);
    } else {
        sb = sb1;
    }

    if (!sb.limpio) printf("ADVERTENCIA: El disco no se desmontó correctamente.\n");
    sb.limpio = 0;
    guardar_superbloque();
    return 0;
}

void fs_desmontar(void) {
    sb.limpio = 1;
    guardar_superbloque();
    if (fd_disco >= 0)  { close(fd_disco);  fd_disco = -1; }
    if (fd_espejo >= 0) { close(fd_espejo); fd_espejo = -1; }
}

static int leer_registro(int i, Registro *r) {
    lseek(fd_disco, pos_registro(i), SEEK_SET);
    return read(fd_disco, r, sizeof *r) == sizeof *r ? 0 : -1;
}

void fs_crear(void) {
    Registro r; memset(&r, 0, sizeof r);
    int slot = -1;
    for (int i = 0; i < MAX_REG; i++) {
        Registro tmp;
        if (leer_registro(i, &tmp) == 0 && tmp.activo == 0) { slot = i; break; }
    }
    if (slot == -1) { printf("Disco lleno.\n"); return; }
    r.id = sb.siguiente_id++; r.activo = 1;
    leer_linea("Nombre: ", r.nombre, sizeof r.nombre);
    leer_linea("Descripción: ", r.descripcion, sizeof r.descripcion);
    strncpy(r.propietario, seg_usuario_actual(), sizeof r.propietario - 1);
    escribir_en_ambos(pos_registro(slot), &r, sizeof r);
    sb.usados++;
    guardar_superbloque();
    printf("Registro ID %d creado en la ranura %d.\n", r.id, slot);
}

void fs_guardar_record(const char *jugador, int puntaje) {
    int slot = -1;
    for (int i = 0; i < MAX_REG; i++) {
        Registro tmp;
        if (leer_registro(i, &tmp) == 0 && tmp.activo == 0) { slot = i; break; }
    }
    if (slot == -1) return;

    Registro r; memset(&r, 0, sizeof r);
    r.id = sb.siguiente_id++;
    r.activo = 1;
    snprintf(r.nombre, sizeof r.nombre, "Récord Snake - %s", jugador);
    snprintf(r.descripcion, sizeof r.descripcion, "Puntaje alcanzado: %d", puntaje);
    strncpy(r.propietario, jugador, sizeof r.propietario - 1);

    escribir_en_ambos(pos_registro(slot), &r, sizeof r);
    sb.usados++;
    guardar_superbloque();
    printf("Récord del juego guardado automáticamente en disco virtual (ID %d).\n", r.id);
}

void fs_listar(void) {
    printf("\n%-5s %-25s %-35s %-15s\n", "ID", "NOMBRE", "DESCRIPCIÓN", "PROPIETARIO");
    printf("-----------------------------------------------------------------------------------\n");
    for (int i = 0; i < MAX_REG; i++) {
        Registro r;
        if (leer_registro(i, &r) == 0 && r.activo)
            printf("%-5d %-25s %-35s %-15s\n", r.id, r.nombre, r.descripcion, r.propietario);
    }
}

void fs_actualizar(void) {
    int id = leer_entero("ID a actualizar: ");
    for (int i = 0; i < MAX_REG; i++) {
        Registro r;
        if (leer_registro(i, &r) == 0 && r.activo && r.id == id) {
            if (strcmp(seg_rol_actual(), "admin") != 0 && strcmp(r.propietario, seg_usuario_actual()) != 0) {
                printf("  ACCESO DENEGADO: No es propietario del registro.\n");
                return;
            }
            leer_linea("Nuevo Nombre: ", r.nombre, sizeof r.nombre);
            leer_linea("Nueva Descripción: ", r.descripcion, sizeof r.descripcion);
            escribir_en_ambos(pos_registro(i), &r, sizeof r);
            guardar_superbloque();
            printf("Registro %d actualizado correctamente.\n", id);
            return;
        }
    }
    printf("Registro no encontrado.\n");
}

void fs_borrar(void) {
    int id = leer_entero("ID a borrar: ");
    for (int i = 0; i < MAX_REG; i++) {
        Registro r;
        if (leer_registro(i, &r) == 0 && r.activo && r.id == id) {
            if (strcmp(seg_rol_actual(), "admin") != 0 && strcmp(r.propietario, seg_usuario_actual()) != 0) {
                printf("  ACCESO DENEGADO: No es el propietario del registro.\n");
                return;
            }
            r.activo = 0;
            escribir_en_ambos(pos_registro(i), &r, sizeof r);
            sb.usados--;
            guardar_superbloque();
            printf("Registro %d eliminado.\n", id);
            return;
        }
    }
    printf("Registro no encontrado.\n");
}

void fs_estado(void) {
    printf("\n=== ESTADO DEL DISCO VIRTUAL ===\n");
    printf("Número Mágico:      0x%X\n", sb.magic);
    printf("Versión:            %d\n", sb.version);
    printf("Estado del Disco:   %s\n", sb.limpio ? "Limpio (Desmontado OK)" : "En uso (Sucio)");
    printf("Capacidad Total:    %d ranuras\n", sb.total);
    printf("Ranuras Usadas:     %d\n", sb.usados);
    printf("Ranuras Libres:     %d\n", sb.total - sb.usados);
    printf("Siguiente ID:       %d\n", sb.siguiente_id);
    
    printf("\nMapa de Ocupación de Ranuras ([X]=Usado, [.]=Libre):\n");
    for (unsigned int i = 0; i < sb.total; i++) {
        Registro r;
        if (leer_registro(i, &r) == 0 && r.activo) printf("[X]");
        else printf("[.]");
        if ((i + 1) % 20 == 0) printf("\n");
    }
    printf("=================================\n");
}
