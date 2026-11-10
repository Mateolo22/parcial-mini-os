/* Reemplazar src/main.c */
#include <stdio.h>
#include "kernel.h"
#include "fs.h"
#include "seguridad.h"
#include "bitacora.h"
#include "virtualizacion.h"
#include "juego.h"
#include "proc.h"
#include "sistema.h"

static void mostrar_menu(void) {
    printf("\n===== MiniOS v%s | Usuario: %s (%s) =====\n", 
           VERSION_MINIOS, seg_usuario_actual(), seg_rol_actual());
    printf(" 1. Create Registro      (Disco Virtual)\n");
    printf(" 2. Read Registros       (Disco Virtual)\n");
    printf(" 3. Update Registro      (Disco Virtual)\n");
    printf(" 4. Delete Registro      (Disco Virtual)\n");
    printf(" 5. Estado del Disco     (Mapa & Superbloque)\n");
    printf(" 6. Gestión de Procesos  (BSDGames / Signals)\n");
    printf(" 7. Nueva Partida Snake  (Videojuego)\n");
    printf(" 8. Continuar Partida    (Partida Guardada)\n");
    printf(" 9. Ver / Modif Matriz   (Protección)\n");
    printf("10. Ver Bitácora         (Auditoría - Admin)\n");
    printf("11. Gestionar Usuarios   (Admin)\n");
    printf("12. Cambiar Contraseña   (Perfil)\n");
    printf("13. Virtualización       (Hardware Info)\n");
    printf("14. Acerca del Sistema   (Kernel Info)\n");
    printf("15. Cerrar Sesión        (Cambiar Usuario)\n");
    printf(" 0. Apagar MiniOS\n");
}

int main(void) {
    if (fs_montar(DIR_DATOS "minios.disk", DIR_DATOS "minios_espejo.disk") != 0) return 1;
    seg_cargar_matriz();
    auditar("ARRANQUE", "sistema", "OK");

    while (1) {
        while (!seg_login()) { }

        int en_sesion = 1;
        while (en_sesion) {
            mostrar_menu();
            switch (leer_entero(" Opción: ")) {
                case 1:  if (seg_verificar(OBJ_REGISTROS, D_CREATE)) fs_crear(); break;
                case 2:  if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_listar(); break;
                case 3:  if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) fs_actualizar(); break;
                case 4:  if (seg_verificar(OBJ_REGISTROS, D_DELETE)) fs_borrar(); break;
                case 5:  if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_estado(); break;
                case 6:  if (seg_verificar(OBJ_PROCESOS,  D_EXEC))   proc_lanzar_menu(); break;
                case 7:  if (seg_verificar(OBJ_JUEGOS,    D_EXEC))   juego_iniciar(); break;
                case 8:  if (seg_verificar(OBJ_PARTIDAS,  D_READ))   juego_continuar(); break;
                case 9:  
                    if (seg_verificar(OBJ_MATRIZ, D_READ)) seg_mostrar_matriz();
                    if (seg_verificar(OBJ_MATRIZ, D_UPDATE)) {
                        int m = leer_entero("¿Modificar matriz? (1=Sí, 0=No): ");
                        if (m == 1) seg_modificar_matriz();
                    }
                    break;
                case 10: if (seg_verificar(OBJ_BITACORA,  D_READ))   bitacora_mostrar(); break;
                case 11: if (seg_verificar(OBJ_USUARIOS,  D_CREATE)) seg_gestionar_usuarios(); break;
                case 12: seg_cambiar_clave(); break;
                case 13: if (seg_verificar(OBJ_VIRTUALIZACION, D_EXEC)) virt_info(); break;
                case 14: if (seg_verificar(OBJ_SISTEMA,   D_READ))   acerca_de(); break;
                case 15: seg_cerrar_sesion(); en_sesion = 0; break;
                case 0:
                    if (seg_verificar(OBJ_SISTEMA, D_EXEC)) {
                        auditar("APAGADO", "sistema", "OK");
                        fs_desmontar();
                        printf("Apagando MiniOS v3.0...\n");
                        return 0;
                    }
                    break;
                default:
                    if (feof(stdin)) { fs_desmontar(); return 0; }
                    printf("Opción inválida.\n");
            }
        }
    }
}
