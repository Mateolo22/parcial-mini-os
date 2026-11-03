#ifndef FS_H
#define FS_H

int  fs_montar(const char *ruta);
void fs_desmontar(void);
void fs_crear(void);
void fs_listar(void);
void fs_actualizar(void);
void fs_borrar(void);
void fs_estado(void);

#endif
