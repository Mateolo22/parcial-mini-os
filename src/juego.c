#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <pthread.h>
#include <time.h>
#include "kernel.h"
#include "seguridad.h"
#include "fs.h"
#include "juego.h"

#define ANCHO 20
#define ALTO  10
#define MAX_COLA 200

typedef struct { int x, y; } Punto;

typedef struct {
    char  jugador[32];
    int   puntaje, nivel, direccion, largo, activo;
    Punto cuerpo[MAX_COLA];
    Punto comida;
    long  guardada_en;
} Partida;

static Partida         partida;
static pthread_mutex_t mutex_partida = PTHREAD_MUTEX_INITIALIZER;
static volatile int    jugando = 0;
static struct termios  termios_orig;

static void modo_juego_raw(int activar) {
    if (activar) {
        struct termios t;
        tcgetattr(STDIN_FILENO, &termios_orig);
        t = termios_orig;
        t.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &termios_orig);
    }
}

static int tecla(int ms) {
    fd_set fds;
    struct timeval tv = { 0, ms * 1000 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return c;
    }
    return -1;
}

static int guardar_partida_archivo(const Partida *p) {
    char ruta[64];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", p->jugador);
    int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, p, sizeof *p);
    fsync(fd);
    close(fd);
    return n == sizeof *p ? 0 : -1;
}

static void *hilo_autoguardado(void *arg) {
    int seg = *(int *)arg;
    while (jugando) {
        sleep(seg);
        if (!jugando) break;
        Partida copia;
        pthread_mutex_lock(&mutex_partida);
        copia = partida;
        copia.guardada_en = time(NULL);
        pthread_mutex_unlock(&mutex_partida);
        guardar_partida_archivo(&copia);
    }
    return NULL;
}

static void generar_comida(Partida *p) {
    p->comida.x = rand() % (ANCHO - 2) + 1;
    p->comida.y = rand() % (ALTO - 2) + 1;
}

static void renderizar(const Partida *p) {
    printf("\033[H\033[J"); /* Limpiar pantalla */
    printf("=== MINI SNAKE | Jugador: %s | Puntaje: %d | Guardar: 'g' | Salir: 'q' ===\n",
           p->jugador, p->puntaje);

    for (int y = 0; y < ALTO; y++) {
        for (int x = 0; x < ANCHO; x++) {
            if (y == 0 || y == ALTO - 1 || x == 0 || x == ANCHO - 1) {
                printf("#");
            } else if (x == p->cuerpo[0].x && y == p->cuerpo[0].y) {
                printf("O"); /* Cabeza */
            } else if (x == p->comida.x && y == p->comida.y) {
                printf("*"); /* Comida */
            } else {
                int es_cuerpo = 0;
                for (int i = 1; i < p->largo; i++) {
                    if (p->cuerpo[i].x == x && p->cuerpo[i].y == y) {
                        printf("o");
                        es_cuerpo = 1;
                        break;
                    }
                }
                if (!es_cuerpo) printf(" ");
            }
        }
        printf("\n");
    }
}

void juego_iniciar(void) {
    srand((unsigned)time(NULL));
    pthread_mutex_lock(&mutex_partida);
    memset(&partida, 0, sizeof partida);
    strncpy(partida.jugador, seg_usuario_actual(), 31);
    partida.largo = 3;
    partida.direccion = 1; /* 0: Arriba, 1: Derecha, 2: Abajo, 3: Izquierda */
    partida.cuerpo[0] = (Punto){ ANCHO / 2, ALTO / 2 };
    partida.cuerpo[1] = (Punto){ ANCHO / 2 - 1, ALTO / 2 };
    partida.cuerpo[2] = (Punto){ ANCHO / 2 - 2, ALTO / 2 };
    partida.activo = 1;
    generar_comida(&partida);
    pthread_mutex_unlock(&mutex_partida);

    jugando = 1;
    int interval = 15;
    pthread_t h;
    pthread_create(&h, NULL, hilo_autoguardado, &interval);

    modo_juego_raw(1);
    while (jugando && partida.activo) {
        renderizar(&partida);
        int c = tecla(200);
        
        pthread_mutex_lock(&mutex_partida);
        if (c == 'w' && partida.direccion != 2) partida.direccion = 0;
        if (c == 'd' && partida.direccion != 3) partida.direccion = 1;
        if (c == 's' && partida.direccion != 0) partida.direccion = 2;
        if (c == 'a' && partida.direccion != 1) partida.direccion = 3;
        if (c == 'g') {
            guardar_partida_archivo(&partida);
            printf("\n[Partida guardada manualmente]\n");
            sleep(1);
        }
        if (c == 'q') { jugando = 0; pthread_mutex_unlock(&mutex_partida); break; }

        /* Mover serpiente */
        Punto nueva_cabeza = partida.cuerpo[0];
        if (partida.direccion == 0) nueva_cabeza.y--;
        if (partida.direccion == 1) nueva_cabeza.x++;
        if (partida.direccion == 2) nueva_cabeza.y++;
        if (partida.direccion == 3) nueva_cabeza.x--;

        /* Colisión con bordes */
        if (nueva_cabeza.x <= 0 || nueva_cabeza.x >= ANCHO - 1 ||
            nueva_cabeza.y <= 0 || nueva_cabeza.y >= ALTO - 1) {
            partida.activo = 0;
        }

        /* Colisión consigo misma */
        for (int i = 0; i < partida.largo; i++) {
            if (partida.cuerpo[i].x == nueva_cabeza.x && partida.cuerpo[i].y == nueva_cabeza.y) {
                partida.activo = 0;
            }
        }

        if (partida.activo) {
            for (int i = partida.largo; i > 0; i--) partida.cuerpo[i] = partida.cuerpo[i - 1];
            partida.cuerpo[0] = nueva_cabeza;

            /* Comer comida */
            if (nueva_cabeza.x == partida.comida.x && nueva_cabeza.y == partida.comida.y) {
                partida.puntaje += 10;
                if (partida.largo < MAX_COLA) partida.largo++;
                generar_comida(&partida);
            }
        }
        pthread_mutex_unlock(&mutex_partida);
    }
    modo_juego_raw(0);

    jugando = 0;
    pthread_join(h, NULL);

    printf("\n=== GAME OVER ===\nPuntaje Final: %d\n", partida.puntaje);
    
    /* Guardar RÉCORD en el disco virtual minios.disk */
    fs_guardar_record(seg_usuario_actual(), partida.puntaje);
}

void juego_continuar(void) {
    char ruta[64];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", seg_usuario_actual());
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) {
        printf("No existe partida guardada para el usuario %s.\n", seg_usuario_actual());
        return;
    }
    read(fd, &partida, sizeof partida);
    close(fd);

    printf("Partida cargada para %s. Puntaje actual: %d. Reanudando...\n", partida.jugador, partida.puntaje);
    sleep(2);
    juego_iniciar();
}
