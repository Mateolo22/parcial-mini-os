/*
 * minios.c
 * Mini Sistema Operativo - Menu CLI con Gestion de Memoria Secundaria REAL
 *
 * Los registros se guardan en el archivo "datos.dat" (simula el disco).
 * Create/Read/Update/Delete ya funcionan de verdad, leyendo y escribiendo
 * en ese archivo. Las opciones 5 y 6 (procesos y apagado) siguen igual.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include <unistd.h>
#include <time.h>

#define ARCHIVO "datos.dat"
#define TAM_NOMBRE 50
#define TAM_DESC   100

// Estructura de un registro almacenado en el "disco"
typedef struct {
    int id;
    char nombre[TAM_NOMBRE];
    char descripcion[TAM_DESC];
} Registro;

void limpiarBuffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// ---------- CREATE ----------
// Pide los datos por teclado y agrega el registro al final del archivo
void crearRegistro() {
    Registro r;

    printf("\n--- CREATE: nuevo registro ---\n");
    printf("ID: ");
    if (scanf("%d", &r.id) != 1) { printf("ID invalido.\n"); limpiarBuffer(); return; }
    limpiarBuffer();

    printf("Nombre: ");
    fgets(r.nombre, TAM_NOMBRE, stdin);
    r.nombre[strcspn(r.nombre, "\n")] = '\0'; // quita el salto de linea

    printf("Descripcion: ");
    fgets(r.descripcion, TAM_DESC, stdin);
    r.descripcion[strcspn(r.descripcion, "\n")] = '\0';

    FILE *f = fopen(ARCHIVO, "ab"); // append binario, crea el archivo si no existe
    if (f == NULL) {
        printf("Error: no se pudo abrir el archivo.\n");
        return;
    }
    fwrite(&r, sizeof(Registro), 1, f);
    fclose(f);

    printf("Registro con ID %d guardado en disco.\n", r.id);
}

// ---------- READ ----------
// Recorre el archivo completo y muestra todos los registros formateados
void leerRegistros() {
    Registro r;
    int contador = 0;

    FILE *f = fopen(ARCHIVO, "rb");
    if (f == NULL) {
        printf("\nNo hay registros aun (el archivo no existe).\n");
        return;
    }

    printf("\n--- READ: registros en disco ---\n");
    while (fread(&r, sizeof(Registro), 1, f) == 1) {
        printf("ID: %d | Nombre: %s | Descripcion: %s\n", r.id, r.nombre, r.descripcion);
        contador++;
    }
    fclose(f);

    if (contador == 0) printf("El archivo existe pero no tiene registros.\n");
}

// ---------- UPDATE ----------
// Busca un registro por ID y sobreescribe sus campos en el mismo lugar del archivo
void actualizarRegistro() {
    int idBuscado;
    Registro r;
    int encontrado = 0;

    printf("\n--- UPDATE: modificar registro ---\n");
    printf("ID a buscar: ");
    if (scanf("%d", &idBuscado) != 1) { printf("ID invalido.\n"); limpiarBuffer(); return; }
    limpiarBuffer();

    FILE *f = fopen(ARCHIVO, "rb+"); // lectura y escritura binaria
    if (f == NULL) {
        printf("No hay registros guardados todavia.\n");
        return;
    }

    long posicion;
    while (fread(&r, sizeof(Registro), 1, f) == 1) {
        if (r.id == idBuscado) {
            encontrado = 1;
            posicion = ftell(f) - sizeof(Registro); // donde empieza este registro

            printf("Registro encontrado -> Nombre actual: %s | Descripcion actual: %s\n",
                   r.nombre, r.descripcion);

            printf("Nuevo nombre: ");
            fgets(r.nombre, TAM_NOMBRE, stdin);
            r.nombre[strcspn(r.nombre, "\n")] = '\0';

            printf("Nueva descripcion: ");
            fgets(r.descripcion, TAM_DESC, stdin);
            r.descripcion[strcspn(r.descripcion, "\n")] = '\0';

            fseek(f, posicion, SEEK_SET);       // regresamos a la posicion del registro
            fwrite(&r, sizeof(Registro), 1, f); // lo sobreescribimos con los datos nuevos
            break;
        }
    }
    fclose(f);

    if (encontrado) printf("Registro con ID %d actualizado.\n", idBuscado);
    else printf("No se encontro ningun registro con ID %d.\n", idBuscado);
}

// ---------- DELETE ----------
// Reescribe el archivo completo SIN el registro eliminado (no corrompe el resto)
void eliminarRegistro() {
    int idBuscado;
    Registro r;
    int encontrado = 0;

    printf("\n--- DELETE: eliminar registro ---\n");
    printf("ID a eliminar: ");
    if (scanf("%d", &idBuscado) != 1) { printf("ID invalido.\n"); limpiarBuffer(); return; }
    limpiarBuffer();

    FILE *f = fopen(ARCHIVO, "rb");
    if (f == NULL) {
        printf("No hay registros guardados todavia.\n");
        return;
    }

    FILE *temp = fopen("temp.dat", "wb");
    if (temp == NULL) {
        printf("Error: no se pudo crear archivo temporal.\n");
        fclose(f);
        return;
    }

    while (fread(&r, sizeof(Registro), 1, f) == 1) {
        if (r.id == idBuscado) {
            encontrado = 1;
            continue; // no lo copiamos al temporal -> queda eliminado
        }
        fwrite(&r, sizeof(Registro), 1, temp); // copiamos todos los demas intactos
    }

    fclose(f);
    fclose(temp);

    remove(ARCHIVO);          // borramos el archivo viejo
    rename("temp.dat", ARCHIVO); // el temporal pasa a ser el archivo oficial

    if (encontrado) printf("Registro con ID %d eliminado. El resto del archivo quedo intacto.\n", idBuscado);
    else printf("No se encontro ningun registro con ID %d.\n", idBuscado);
}

// ---------- SNAKE (Reto BSDGames) ----------
#define SNAKE_ANCHO 40
#define SNAKE_ALTO  20
#define SNAKE_MAX   (SNAKE_ANCHO * SNAKE_ALTO)

typedef struct { int x, y; } Punto;

void jugarSnake() {
    Punto cuerpo[SNAKE_MAX];
    int largo = 3;
    int dx = 1, dy = 0;       // direccion inicial: derecha
    int puntaje = 0;
    int comidaX, comidaY;
    int tecla;
    int perdio = 0;
    int i;

    // --- Inicializar ncurses ---
    initscr();              // entra en modo pantalla especial
    cbreak();                // lee tecla por tecla, sin esperar Enter
    noecho();                 // no muestra lo que escribes
    keypad(stdscr, TRUE);    // habilita flechas del teclado
    curs_set(0);              // oculta el cursor parpadeante
    timeout(120);              // milisegundos que espera getch() por tecla (= velocidad del juego)

    // Posicion inicial de la serpiente, al centro
    for (i = 0; i < largo; i++) {
        cuerpo[i].x = SNAKE_ANCHO / 2 - i;
        cuerpo[i].y = SNAKE_ALTO / 2;
    }

    srand(time(NULL));
    comidaX = rand() % (SNAKE_ANCHO - 2) + 1;
    comidaY = rand() % (SNAKE_ALTO - 2) + 1;

    while (!perdio) {
        // --- Leer tecla (si el jugador presiono alguna en este ciclo) ---
        tecla = getch();
        switch (tecla) {
            case KEY_UP:    if (dy != 1)  { dx = 0; dy = -1; } break;
            case KEY_DOWN:  if (dy != -1) { dx = 0; dy = 1;  } break;
            case KEY_LEFT:  if (dx != 1)  { dx = -1; dy = 0; } break;
            case KEY_RIGHT: if (dx != -1) { dx = 1; dy = 0;  } break;
            case 'q': case 'Q': perdio = 1; continue;
        }

        // --- Mover la serpiente: cada segmento toma el lugar del anterior ---
        for (i = largo - 1; i > 0; i--) cuerpo[i] = cuerpo[i - 1];
        cuerpo[0].x += dx;
        cuerpo[0].y += dy;

        // --- Colision con las paredes ---
        if (cuerpo[0].x <= 0 || cuerpo[0].x >= SNAKE_ANCHO - 1 ||
            cuerpo[0].y <= 0 || cuerpo[0].y >= SNAKE_ALTO - 1) {
            perdio = 1;
        }

        // --- Colision con su propio cuerpo ---
        for (i = 1; i < largo; i++) {
            if (cuerpo[0].x == cuerpo[i].x && cuerpo[0].y == cuerpo[i].y) {
                perdio = 1;
            }
        }

        // --- Comio la comida ---
        if (cuerpo[0].x == comidaX && cuerpo[0].y == comidaY) {
            puntaje += 10;
            if (largo < SNAKE_MAX) largo++;
            comidaX = rand() % (SNAKE_ANCHO - 2) + 1;
            comidaY = rand() % (SNAKE_ALTO - 2) + 1;
        }

        // --- Dibujar el tablero completo ---
        clear();
        for (i = 0; i <= SNAKE_ANCHO; i++) { mvaddch(0, i, '#'); mvaddch(SNAKE_ALTO, i, '#'); }
        for (i = 0; i <= SNAKE_ALTO; i++)  { mvaddch(i, 0, '#'); mvaddch(i, SNAKE_ANCHO, '#'); }

        mvaddch(comidaY, comidaX, '*');
        for (i = 0; i < largo; i++) {
            mvaddch(cuerpo[i].y, cuerpo[i].x, i == 0 ? '@' : 'o');
        }

        mvprintw(SNAKE_ALTO + 1, 0, "Puntaje: %d   (usa las flechas, 'q' para salir)", puntaje);
        refresh();
    }

    // --- Pantalla final ---
    clear();
    mvprintw(SNAKE_ALTO / 2, SNAKE_ANCHO / 2 - 10, "JUEGO TERMINADO");
    mvprintw(SNAKE_ALTO / 2 + 1, SNAKE_ANCHO / 2 - 10, "Puntaje final: %d", puntaje);
    mvprintw(SNAKE_ALTO / 2 + 2, SNAKE_ANCHO / 2 - 10, "Presiona cualquier tecla...");
    refresh();
    timeout(-1);   // vuelve a esperar indefinidamente por una tecla
    getch();

    endwin();      // sale del modo ncurses y devuelve la terminal a la normalidad
}

// ---------- TETRIS (Reto BSDGames) ----------
#define TETRIS_ANCHO 10
#define TETRIS_ALTO  20

// Las 7 piezas clasicas, cada una en una cuadricula de 4x4 (16 casillas).
// 1 = casilla ocupada, 0 = casilla vacia. La rotacion se calcula en tiempo
// de ejecucion con una formula matematica, no hace falta guardar cada giro.
int PIEZAS_TETRIS[7][16] = {
    { 0,0,0,0,  1,1,1,1,  0,0,0,0,  0,0,0,0 }, // I
    { 0,0,0,0,  0,1,1,0,  0,1,1,0,  0,0,0,0 }, // O
    { 0,0,0,0,  0,1,1,1,  0,0,1,0,  0,0,0,0 }, // T
    { 0,0,0,0,  0,0,1,1,  0,1,1,0,  0,0,0,0 }, // S
    { 0,0,0,0,  0,1,1,0,  0,0,1,1,  0,0,0,0 }, // Z
    { 0,0,0,0,  1,0,0,0,  1,1,1,0,  0,0,0,0 }, // J
    { 0,0,0,0,  0,0,1,0,  1,1,1,0,  0,0,0,0 }, // L
};

// Gira una pieza 4x4 noventa grados en sentido horario (formula estandar
// de rotacion de matrices: nueva[i][j] = vieja[3-j][i])
void rotarPieza(int origen[16], int destino[16]) {
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            destino[i * 4 + j] = origen[(3 - j) * 4 + i];
        }
    }
}

// Revisa si la pieza (en la posicion px,py) chocaria con las paredes,
// el piso o un bloque ya fijado en el tablero
int colisionTetris(int tablero[TETRIS_ALTO][TETRIS_ANCHO], int pieza[16], int px, int py) {
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (!pieza[r * 4 + c]) continue;
            int bx = px + c;
            int by = py + r;
            if (bx < 0 || bx >= TETRIS_ANCHO || by >= TETRIS_ALTO) return 1;
            if (by >= 0 && tablero[by][bx]) return 1;
        }
    }
    return 0;
}

// "Pega" la pieza al tablero cuando ya no puede seguir cayendo
void fijarPieza(int tablero[TETRIS_ALTO][TETRIS_ANCHO], int pieza[16], int px, int py) {
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (!pieza[r * 4 + c]) continue;
            int bx = px + c;
            int by = py + r;
            if (by >= 0 && by < TETRIS_ALTO && bx >= 0 && bx < TETRIS_ANCHO) {
                tablero[by][bx] = 1;
            }
        }
    }
}

// Busca filas completas, las elimina y baja todo lo de arriba. Devuelve
// cuantas lineas se limpiaron (para sumar puntaje)
int limpiarLineasTetris(int tablero[TETRIS_ALTO][TETRIS_ANCHO]) {
    int lineas = 0;
    int y, x, k;
    for (y = TETRIS_ALTO - 1; y >= 0; y--) {
        int completa = 1;
        for (x = 0; x < TETRIS_ANCHO; x++) {
            if (!tablero[y][x]) { completa = 0; break; }
        }
        if (completa) {
            lineas++;
            for (k = y; k > 0; k--) {
                for (x = 0; x < TETRIS_ANCHO; x++) {
                    tablero[k][x] = tablero[k - 1][x];
                }
            }
            for (x = 0; x < TETRIS_ANCHO; x++) tablero[0][x] = 0;
            y++; // vuelve a revisar la misma fila, que ahora trajo la de arriba
        }
    }
    return lineas;
}

// Dibuja el tablero fijo + la pieza que esta cayendo + el puntaje.
// Cada casilla se pinta con "[]" (dos caracteres) para que se vea cuadrada.
void dibujarTetris(int tablero[TETRIS_ALTO][TETRIS_ANCHO], int pieza[16], int px, int py, int puntaje) {
    int anchoVisual = TETRIS_ANCHO * 2 + 2;
    int i, r, c;

    clear();

    for (i = 0; i < anchoVisual; i++) { mvaddch(0, i, '#'); mvaddch(TETRIS_ALTO + 1, i, '#'); }
    for (i = 0; i <= TETRIS_ALTO + 1; i++) { mvaddch(i, 0, '#'); mvaddch(i, anchoVisual - 1, '#'); }

    for (r = 0; r < TETRIS_ALTO; r++) {
        for (c = 0; c < TETRIS_ANCHO; c++) {
            if (tablero[r][c]) mvprintw(r + 1, c * 2 + 1, "[]");
        }
    }

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (!pieza[r * 4 + c]) continue;
            int by = py + r;
            int bx = px + c;
            if (by >= 0) mvprintw(by + 1, bx * 2 + 1, "[]");
        }
    }

    mvprintw(TETRIS_ALTO + 2, 0, "Puntaje: %d   (flechas = mover/rotar, abajo = bajar rapido, 'q' para salir)", puntaje);
    refresh();
}

void jugarTetris() {
    int tablero[TETRIS_ALTO][TETRIS_ANCHO];
    int piezaActual[16], piezaRotada[16];
    int px, py;
    int puntaje = 0;
    int perdio = 0;
    int tecla;
    int contadorCaida = 0;
    int velocidadCaida = 5; // cada cuantos ciclos del bucle cae un bloque solo

    memset(tablero, 0, sizeof(tablero));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);

    srand(time(NULL));
    memcpy(piezaActual, PIEZAS_TETRIS[rand() % 7], sizeof(piezaActual));
    px = TETRIS_ANCHO / 2 - 2;
    py = -2; // aparece un poco arriba del tablero visible

    while (!perdio) {
        tecla = getch();

        switch (tecla) {
            case KEY_LEFT:
                if (!colisionTetris(tablero, piezaActual, px - 1, py)) px--;
                break;
            case KEY_RIGHT:
                if (!colisionTetris(tablero, piezaActual, px + 1, py)) px++;
                break;
            case KEY_DOWN:
                if (!colisionTetris(tablero, piezaActual, px, py + 1)) py++;
                break;
            case KEY_UP:
                rotarPieza(piezaActual, piezaRotada);
                if (!colisionTetris(tablero, piezaRotada, px, py)) {
                    memcpy(piezaActual, piezaRotada, sizeof(piezaActual));
                }
                break;
            case 'q': case 'Q':
                perdio = 1;
                continue;
        }

        contadorCaida++;
        if (contadorCaida >= velocidadCaida) {
            contadorCaida = 0;
            if (!colisionTetris(tablero, piezaActual, px, py + 1)) {
                py++;
            } else {
                fijarPieza(tablero, piezaActual, px, py);
                puntaje += limpiarLineasTetris(tablero) * 100;

                memcpy(piezaActual, PIEZAS_TETRIS[rand() % 7], sizeof(piezaActual));
                px = TETRIS_ANCHO / 2 - 2;
                py = -2;

                if (colisionTetris(tablero, piezaActual, px, py)) {
                    perdio = 1; // ya no cabe una pieza nueva: fin del juego
                }
            }
        }

        dibujarTetris(tablero, piezaActual, px, py, puntaje);
    }

    clear();
    mvprintw(TETRIS_ALTO / 2, 2, "JUEGO TERMINADO");
    mvprintw(TETRIS_ALTO / 2 + 1, 2, "Puntaje final: %d", puntaje);
    mvprintw(TETRIS_ALTO / 2 + 2, 2, "Presiona cualquier tecla...");
    refresh();
    timeout(-1);
    getch();

    endwin();
}

void mostrarMenu() {
    printf("\n===== MINI SISTEMA OPERATIVO =====\n");
    printf("--- Gestion de Memoria Secundaria (CRUD) ---\n");
    printf("1. Create\n");
    printf("2. Read\n");
    printf("3. Update\n");
    printf("4. Delete\n");
    printf("--- Gestion de Procesos ---\n");
    printf("5. Iniciar Reto BSDGames (Snake / Tetris)\n");
    printf("--- Control del Sistema ---\n");
    printf("6. Apagar Sistema\n");
    printf("===================================\n");
    printf("Seleccione una opcion: ");
}

int main() {
    int opcion;
    int sistemaActivo = 1;

    // Fuerza que cada printf se muestre de inmediato en pantalla,
    // sin esperar a un salto de linea o a que el buffer se llene.
    setvbuf(stdout, NULL, _IONBF, 0);

    while (sistemaActivo) {
        mostrarMenu();

        if (scanf("%d", &opcion) != 1) {
            printf("Entrada invalida. Intente de nuevo.\n");
            limpiarBuffer();
            continue;
        }
        limpiarBuffer(); // limpia el salto de linea que deja scanf, para que fgets funcione bien despues

        switch (opcion) {
            case 1:
                crearRegistro();
                break;
            case 2:
                leerRegistros();
                break;
            case 3:
                actualizarRegistro();
                break;
            case 4:
                eliminarRegistro();
                break;
            case 5: {
                int juego;
                printf("\n--- Reto BSDGames ---\n");
                printf("1. Snake\n");
                printf("2. Tetris\n");
                printf("Elige un juego: ");
                if (scanf("%d", &juego) != 1) {
                    printf("Opcion invalida.\n");
                    limpiarBuffer();
                    break;
                }
                limpiarBuffer();

                if (juego == 1) {
                    printf("Iniciando Snake...\n");
                    jugarSnake();
                } else if (juego == 2) {
                    printf("Iniciando Tetris...\n");
                    jugarTetris();
                } else {
                    printf("Opcion invalida.\n");
                    break;
                }
                printf("De vuelta en el Mini Sistema Operativo.\n");
                break;
            }
            case 6:
                printf("Apagando el sistema de forma segura...\n");
                sistemaActivo = 0;
                break;
            default:
                printf("Opcion no valida. Por favor elija entre 1 y 6.\n");
                break;
        }
    }

    printf("Sistema apagado. Hasta pronto.\n");
    return 0;
}
