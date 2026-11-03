#include <stdio.h>
#include "kernel.h"
#include "fs.h"
#include "proc.h"
#include "juego.h"
#include "seguridad.h"
#include "sistema.h"

static void mostrar_menu(void) {
    printf("\n=================== MiniOS v%s | Usuario: %s ===================\n",
           VERSION_MINIOS, seg_usuario_actual());
    printf(" 1. Create Registro        (Disco Virtual)\n");
    printf(" 2. Read Registros         (Disco Virtual)\n");
    printf(" 3. Update Registro        (Disco Virtual)\n");
    printf(" 4. Delete Registro        (Disco Virtual)\n");
    printf(" 5. Estado del Disco       (Disco Virtual)\n");
    printf(" 6. Gestion de Procesos    (BSDGames / Signals)\n");
    printf(" 7. Videojuego Propio      (Partida Guardada)\n");
    printf(" 8. Ver Matriz de Acceso   (Protección)\n");
    printf(" 9. Modificar Matriz       (Solo Admin)\n");
    printf("10. Acerca del Sistema     (Kernel Info)\n");
    printf(" 0. Apagar MiniOS\n");
    printf("========================================================================\n");
}

int main(void) {
    if (fs_montar(DIR_DATOS "minios.disk") != 0) {
        fprintf(stderr, "Error al montar el disco virtual.\n");
        return 1;
    }

    seg_cargar_matriz();
    seg_iniciar_sesion();

    while (1) {
        mostrar_menu();
        int op = leer_entero(" Opción: ");

        switch (op) {
            case 1: if (seg_verificar(OBJ_REGISTROS, D_CREATE)) fs_crear();      break;
            case 2: if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_listar();     break;
            case 3: if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) fs_actualizar(); break;
            case 4: if (seg_verificar(OBJ_REGISTROS, D_DELETE)) fs_borrar();     break;
            case 5: if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_estado();     break;
            case 6: if (seg_verificar(OBJ_PROCESOS,  D_EXEC))   proc_menu();     break;
            case 7: if (seg_verificar(OBJ_JUEGOS,    D_EXEC))   juego_menu();    break;
            case 8: if (seg_verificar(OBJ_MATRIZ,    D_READ))   seg_ver_matriz(); break;
            case 9: if (seg_verificar(OBJ_MATRIZ,    D_UPDATE)) seg_modificar_matriz(); break;
            case 10: acerca_de(); break;
            case 0:
                if (seg_verificar(OBJ_SISTEMA, D_EXEC)) {
                    fs_desmontar();
                    printf("Apagando MiniOS de forma segura...\n");
                    return 0;
                }
                break;
            default:
                if (feof(stdin)) {
                    fs_desmontar();
                    return 0;
                }
                printf("Opción inválida.\n");
                break;
        }
    }
}
