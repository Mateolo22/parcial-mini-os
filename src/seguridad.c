#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include "kernel.h"
#include "seguridad.h"
#include "bitacora.h"

#define ARCH_MATRIZ   DIR_DATOS "matriz.dat"
#define ARCH_USUARIOS DIR_DATOS "usuarios.dat"
#define MAX_INTENTOS  3

typedef struct {
    char          nombre[32];
    unsigned long sal, hash;
    int           rol, intentos_fallidos, bloqueado;
} Usuario;

static Usuario sesion;
static int     hay_sesion = 0;
static int     denegaciones_sesion = 0;

static unsigned char matriz[NUM_ROLES][NUM_OBJETOS] = {
/*  reg   jue   par   pro   mat   sis   usr   bit   vir */
  { 0x0F, 0x10, 0x0F, 0x1F, 0x06, 0x12, 0x07, 0x02, 0x10 }, /* admin: D_READ|D_EXEC en sis (0x12) */
  { 0x07, 0x10, 0x07, 0x06, 0x02, 0x02, 0x00, 0x00, 0x10 }, /* operador */
  { 0x02, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00 }  /* invitado */
};

static const char *NOMBRE_ROL[NUM_ROLES] = { "admin", "operador", "invitado" };
static const char *NOMBRE_OBJ[NUM_OBJETOS] = {
    "registros", "juegos", "partidas", "procesos", 
    "matriz", "sistema", "usuarios", "bitacora", "virtualiz"
};

static unsigned long hash_clave(const char *clave, unsigned long sal) {
    unsigned long h = 5381 ^ sal;
    for (const unsigned char *p = (const unsigned char *)clave; *p; p++)
        h = ((h << 5) + h) + *p;
    return h;
}

static void leer_clave(const char *prompt, char *buf, int n) {
    struct termios viejo, nuevo;
    int tty = isatty(STDIN_FILENO);
    if (tty) {
        tcgetattr(STDIN_FILENO, &viejo);
        nuevo = viejo;
        nuevo.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &nuevo);
    }
    leer_linea(prompt, buf, n);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &viejo);
        printf("\n");
    }
}

static void crear_usuario(const char *nombre, const char *clave, Rol rol) {
    Usuario u;
    memset(&u, 0, sizeof u);
    strncpy(u.nombre, nombre, sizeof u.nombre - 1);
    u.sal  = ((unsigned long)rand() << 16) ^ (unsigned long)time(NULL);
    u.hash = hash_clave(clave, u.sal);
    u.rol  = rol;
    int fd = open(ARCH_USUARIOS, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    write(fd, &u, sizeof u);
    close(fd);
}

static int buscar_usuario(const char *nombre, Usuario *u, off_t *pos) {
    int fd = open(ARCH_USUARIOS, O_RDONLY);
    if (fd < 0) return 0;
    off_t p = 0;
    while (read(fd, u, sizeof *u) == sizeof *u) {
        if (strcmp(u->nombre, nombre) == 0) { *pos = p; close(fd); return 1; }
        p += sizeof *u;
    }
    close(fd);
    return 0;
}

static void guardar_usuario(const Usuario *u, off_t pos) {
    int fd = open(ARCH_USUARIOS, O_WRONLY);
    if (fd < 0) return;
    lseek(fd, pos, SEEK_SET);
    write(fd, u, sizeof *u);
    fsync(fd);
    close(fd);
}

int seg_login(void) {
    if (access(ARCH_USUARIOS, F_OK) != 0) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        crear_usuario("admin",    "admin123", ROL_ADMIN);
        crear_usuario("operador", "oper123",  ROL_OPERADOR);
        crear_usuario("invitado", "inv123",   ROL_INVITADO);
    }
    char nombre[32], clave[64];
    Usuario u;
    off_t pos;

    leer_linea("Usuario: ", nombre, sizeof nombre);
    if (feof(stdin)) exit(0);
    leer_clave("Contraseña: ", clave, sizeof clave);

    if (!buscar_usuario(nombre, &u, &pos)) {
        auditar("LOGIN", nombre, "LOGIN FALLIDO");
        printf("Usuario o contraseña incorrectos.\n");
        return 0;
    }
    if (u.bloqueado) {
        auditar("LOGIN", nombre, "CUENTA BLOQUEADA");
        printf("Cuenta bloqueada. Contacte al administrador.\n");
        return 0;
    }
    if (hash_clave(clave, u.sal) != u.hash) {
        u.intentos_fallidos++;
        if (u.intentos_fallidos >= MAX_INTENTOS) u.bloqueado = 1;
        guardar_usuario(&u, pos);
        auditar("LOGIN", nombre, u.bloqueado ? "BLOQUEO" : "LOGIN FALLIDO");
        printf("Usuario o contraseña incorrectos.\n");
        return 0;
    }
    u.intentos_fallidos = 0;
    guardar_usuario(&u, pos);
    sesion = u;
    hay_sesion = 1;
    denegaciones_sesion = 0;
    auditar("LOGIN", nombre, "LOGIN EXITOSO");
    return 1;
}

void seg_cerrar_sesion(void) {
    auditar("LOGOUT", sesion.nombre, "OK");
    hay_sesion = 0;
    memset(&sesion, 0, sizeof sesion);
}

const char *seg_usuario_actual(void) { return hay_sesion ? sesion.nombre : "-"; }
const char *seg_rol_actual(void)     { return hay_sesion ? NOMBRE_ROL[sesion.rol] : "-"; }

int seg_verificar(Objeto obj, unsigned char derecho) {
    char accion[32];
    snprintf(accion, sizeof accion, "DERECHO_0x%02X", derecho);

    if (hay_sesion && (matriz[sesion.rol][obj] & derecho) == derecho) {
        auditar(accion, NOMBRE_OBJ[obj], "PERMITIDO");
        return 1;
    }
    
    denegaciones_sesion++;
    auditar(accion, NOMBRE_OBJ[obj], "DENEGADO");
    printf("  ACCESO DENEGADO\n");

    if (denegaciones_sesion >= 5) {
        printf("  [ALERTA SECTOR SEGURIDAD] Acumula %d accesos denegados en esta sesión.\n", denegaciones_sesion);
        auditar("ALERTA_SEGURIDAD", "sistema", "5_DENEGACIONES_ALCANZADAS");
    }
    return 0;
}

void seg_gestionar_usuarios(void) {
    printf("\n--- GESTIÓN DE USUARIOS ---\n");
    printf("1. Listar usuarios\n2. Crear usuario\n3. Desbloquear usuario\nOpción: ");
    int op = leer_entero("");
    if (op == 1) {
        int fd = open(ARCH_USUARIOS, O_RDONLY);
        if (fd < 0) return;
        Usuario u;
        printf("%-15s %-10s %-10s %-10s\n", "USUARIO", "ROL", "FALLOS", "ESTADO");
        while (read(fd, &u, sizeof u) == sizeof u) {
            printf("%-15s %-10s %-10d %-10s\n", u.nombre, NOMBRE_ROL[u.rol], 
                   u.intentos_fallidos, u.bloqueado ? "BLOQUEADO" : "Activo");
        }
        close(fd);
    } else if (op == 2) {
        char n[32], c[64];
        leer_linea("Nombre nuevo usuario: ", n, sizeof n);
        leer_clave("Contraseña: ", c, sizeof c);
        if (strlen(c) < 6) { printf("Error: mínimo 6 caracteres.\n"); return; }
        int r = leer_entero("Rol (0=admin, 1=operador, 2=invitado): ");
        if (r < 0 || r >= NUM_ROLES) return;
        crear_usuario(n, c, (Rol)r);
        auditar("CREAR_USUARIO", n, "OK");
        printf("Usuario creado exitosamente.\n");
    } else if (op == 3) {
        char n[32]; Usuario u; off_t pos;
        leer_linea("Usuario a desbloquear: ", n, sizeof n);
        if (buscar_usuario(n, &u, &pos)) {
            u.bloqueado = 0;
            u.intentos_fallidos = 0;
            guardar_usuario(&u, pos);
            auditar("DESBLOQUEAR", n, "OK");
            printf("Usuario desbloqueado.\n");
        } else printf("Usuario no encontrado.\n");
    }
}

void seg_cambiar_clave(void) {
    char actual[64], nueva[64];
    leer_clave("Contraseña actual: ", actual, sizeof actual);
    if (hash_clave(actual, sesion.sal) != sesion.hash) {
        printf("Contraseña actual incorrecta.\n");
        return;
    }
    leer_clave("Nueva contraseña (mínimo 6 caracteres): ", nueva, sizeof nueva);
    if (strlen(nueva) < 6) {
        printf("Error: la contraseña debe tener al menos 6 caracteres.\n");
        return;
    }
    Usuario u; off_t pos;
    if (buscar_usuario(sesion.nombre, &u, &pos)) {
        u.hash = hash_clave(nueva, u.sal);
        guardar_usuario(&u, pos);
        sesion.hash = u.hash;
        auditar("CAMBIO_CLAVE", sesion.nombre, "OK");
        printf("Contraseña actualizada con éxito.\n");
    }
}

void seg_guardar_matriz(void) {
    int fd = open(ARCH_MATRIZ, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    write(fd, matriz, sizeof matriz);
    fsync(fd);
    close(fd);
}

void seg_cargar_matriz(void) {
    int fd = open(ARCH_MATRIZ, O_RDONLY);
    if (fd < 0) { seg_guardar_matriz(); return; }
    if (read(fd, matriz, sizeof matriz) != sizeof matriz) seg_guardar_matriz();
    close(fd);
}

void seg_mostrar_matriz(void) {
    printf("\n%-10s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
           "ROL", "REGIST", "JUEGOS", "PARTID", "PROCES", "MATRIZ", "SISTEM", "USUARI", "BITACO", "VIRTUA");
    for (int r = 0; r < NUM_ROLES; r++) {
        printf("%-10s ", NOMBRE_ROL[r]);
        for (int o = 0; o < NUM_OBJETOS; o++) {
            char d[6] = "-----";
            unsigned char val = matriz[r][o];
            if (val & D_CREATE) d[0] = 'C';
            if (val & D_READ)   d[1] = 'R';
            if (val & D_UPDATE) d[2] = 'U';
            if (val & D_DELETE) d[3] = 'D';
            if (val & D_EXEC)   d[4] = 'X';
            printf("%-8s ", d);
        }
        printf("\n");
    }
}

void seg_modificar_matriz(void) {
    int r = leer_entero("Rol (0=admin, 1=operador, 2=invitado): ");
    if (r < 0 || r >= NUM_ROLES) return;
    int o = leer_entero("Objeto (0=reg..8=virt): ");
    if (o < 0 || o >= NUM_OBJETOS) return;
    int n = leer_entero("Nuevos derechos (0..31): ");
    if (n < 0 || n > 31) return;
    if (r == ROL_ADMIN && o == OBJ_MATRIZ && !(n & D_UPDATE)) {
        printf("ERROR: El admin no puede quitarse el control de la matriz.\n");
        return;
    }
    matriz[r][o] = (unsigned char)n;
    seg_guardar_matriz();
    auditar("MODIF_MATRIZ", NOMBRE_OBJ[o], "OK");
    printf("Matriz actualizada en disco.\n");
}
