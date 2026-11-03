#ifndef KERNEL_H
#define KERNEL_H

#define VERSION_MINIOS "3.0"
#define DIR_DATOS      "datos/"

int  leer_entero(const char *prompt);
void leer_linea(const char *prompt, char *buf, int n);

#endif
