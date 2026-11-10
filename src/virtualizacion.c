#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "virtualizacion.h"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

static void leer_dmi(const char *campo, char *buf, int n) {
    char ruta[96];
    snprintf(ruta, sizeof ruta, "/sys/class/dmi/id/%s", campo);
    FILE *f = fopen(ruta, "r");
    strcpy(buf, "(no disponible)");
    if (f) {
        if (fgets(buf, n, f)) buf[strcspn(buf, "\n")] = '\0';
        fclose(f);
    }
}

void virt_info(void) {
    printf("\n========= INFORMACIÓN DE VIRTUALIZACIÓN =========\n");
    char hypervisor_name[32] = "Físico / Desconocido";

#if defined(__x86_64__) || defined(__i386__)
    unsigned int a, b, c, d;
    char fabricante[13] = {0};
    __get_cpuid(0, &a, &b, &c, &d);
    memcpy(fabricante, &b, 4); memcpy(fabricante + 4, &d, 4); memcpy(fabricante + 8, &c, 4);
    printf("CPU Vendor:            %s\n", fabricante);

    __get_cpuid(1, &a, &b, &c, &d);
    int en_vm = (c >> 31) & 1;
    int vtx_expuesto = (c >> 5) & 1;
    printf("Dentro de una VM:      %s\n", en_vm ? "SÍ" : "NO");
    printf("VT-x expuesto (VMX):   %s\n", vtx_expuesto ? "SÍ" : "NO");

    __get_cpuid(0x80000001, &a, &b, &c, &d);
    int amdv_expuesto = (c >> 2) & 1;
    printf("AMD-V expuesto (SVM):  %s\n", amdv_expuesto ? "SÍ" : "NO");

    if (en_vm) {
        char firma[13] = {0};
        __cpuid(0x40000000, a, b, c, d);
        memcpy(firma, &b, 4); memcpy(firma + 4, &c, 4); memcpy(firma + 8, &d, 4);
        printf("Firma del hipervisor:  %s\n", firma);
        if (strstr(firma, "VBox")) strcpy(hypervisor_name, "VirtualBox");
        else if (strstr(firma, "KVM")) strcpy(hypervisor_name, "KVM");
        else if (strstr(firma, "VMware")) strcpy(hypervisor_name, "VMware");
        else strncpy(hypervisor_name, firma, 31);
    }
#else
    printf("CPUID solo está disponible en arquitecturas x86.\n");
#endif

    FILE *fproc = fopen("/proc/cpuinfo", "r");
    int ept_npt = 0;
    if (fproc) {
        char l[256];
        while (fgets(l, sizeof l, fproc)) {
            if (strstr(l, "ept") || strstr(l, "npt")) { ept_npt = 1; break; }
        }
        fclose(fproc);
    }
    printf("Soporte EPT/NPT:       %s\n", ept_npt ? "SÍ" : "NO");

    char fab[96], prod[96];
    leer_dmi("sys_vendor", fab, sizeof fab);
    leer_dmi("product_name", prod, sizeof prod);
    printf("DMI Vendor / Producto: %s / %s\n", fab, prod);

    printf("systemd-detect-virt:   ");
    fflush(stdout);
    system("systemd-detect-virt 2>/dev/null || echo no disponible");

    printf("\nConclusión: MiniOS corre dentro de %s sobre procesador %s.\n", hypervisor_name, fab);
    printf("==================================================\n");
}
