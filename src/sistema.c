#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include "sistema.h"
#include "kernel.h"

void acerca_de(void) {
    struct utsname u;
    uname(&u);
    printf("\n=== ACERCA DEL SISTEMA ===\n");
    printf("MiniOS v%s sobre %s %s (%s)\n", VERSION_MINIOS, u.sysname, u.release, u.machine);
    printf("Núcleos: %ld | PID: %d | UID Linux: %d\n",
           sysconf(_SC_NPROCESSORS_ONLN), getpid(), getuid());
    printf("===========================\n");
}
