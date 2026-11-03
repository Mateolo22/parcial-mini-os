#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "juego.h"
#include "kernel.h"
#include "seguridad.h"

typedef struct {
    char  jugador[32];
    int   puntaje;
    int   nivel;
    int   intentos_restantes;
    int   numero_secreto;
    long  guardada_en;
} Partida;

static int guardar_partida(const Partida *p) {
    char ruta[128];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", p->jugador);
    int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, p, sizeof *p);
    fsync(fd);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

static int cargar_partida(const char *jugador, Partida *p) {
    char ruta[128];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", jugador);
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, p, sizeof *p);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

static void jugar(Partida *p) {
    printf("\n--- ADIVINA EL NUMERO (Juego MiniOS) ---\n");
    printf("Jugador: %s | Nivel: %d | Puntaje: %d | Intentos restantes: %d\n",
           p->jugador, p->nivel, p->puntaje, p->intentos_restantes);
    printf("Escriba un número entre 1 y 100, 'g' para guardar y salir, o 'q' para salir sin guardar.\n");

    while (p->intentos_restantes > 0) {
        char in[32];
        leer_linea(" Adivina / Opcion: ", in, sizeof in);
        if (strcmp(in, "g") == 0 || strcmp(in, "G") == 0) {
            p->guardada_en = time(NULL);
            if (guardar_partida(p) == 0) {
                printf(" Partida guardada con éxito en datos/partida_%s.sav\n", p->jugador);
            } else {
                printf(" Error al guardar partida.\n");
            }
            return;
        }
        if (strcmp(in, "q") == 0 || strcmp(in, "Q") == 0) {
            printf(" Saliendo del juego...\n");
            return;
        }

        int num = atoi(in);
        if (num <= 0 || num > 100) {
            printf(" Ingrese un número válido entre 1 y 100.\n");
            continue;
        }

        p->intentos_restantes--;
        if (num == p->numero_secreto) {
            p->puntaje += (p->intentos_restantes + 1) * 10;
            printf(" ¡FELICIDADES! Adivinaste el número %d. Puntaje total: %d\n", p->numero_secreto, p->puntaje);
            p->intentos_restantes = 7;
            p->nivel++;
            p->numero_secreto = (rand() % 100) + 1;
            p->guardada_en = time(NULL);
            guardar_partida(p);
            return;
        } else if (num < p->numero_secreto) {
            printf(" El número secreto es MAYOR. (Intentos restantes: %d)\n", p->intentos_restantes);
        } else {
            printf(" El número secreto es MENOR. (Intentos restantes: %d)\n", p->intentos_restantes);
        }
    }

    printf(" Se agotaron los intentos. El número era %d.\n", p->numero_secreto);
    p->intentos_restantes = 7;
    p->puntaje = 0;
    p->numero_secreto = (rand() % 100) + 1;
    guardar_partida(p);
}

void juego_menu(void) {
    srand(time(NULL));
    const char *usr = seg_usuario_actual();

    while (1) {
        printf("\n=== VIDEOJUEGO PROPIO (PERSISTENTE) ===\n");
        printf("1. Nueva Partida\n");
        printf("2. Continuar Partida Guardada\n");
        printf("0. Volver al Menú Principal\n");
        printf("========================================\n");

        int op = leer_entero(" Opción: ");
        if (op == 0) break;

        Partida p;
        if (op == 1) {
            memset(&p, 0, sizeof p);
            strncpy(p.jugador, usr, sizeof p.jugador - 1);
            p.puntaje = 0;
            p.nivel = 1;
            p.intentos_restantes = 7;
            p.numero_secreto = (rand() % 100) + 1;
            jugar(&p);
        } else if (op == 2) {
            if (cargar_partida(usr, &p) == 0) {
                printf("Partida cargada para %s (Guardada el: %s", usr, ctime(&p.guardada_en));
                jugar(&p);
            } else {
                printf("No existe partida guardada para el usuario '%s'.\n", usr);
            }
        } else {
            printf("Opción inválida.\n");
        }
    }
}
