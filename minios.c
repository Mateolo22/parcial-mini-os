/*
 * minios.c - Mini Sistema Operativo v2.0
 *
 * Evolución del miniOS del Taller 1:
 *  - El CRUD sobre "datos.dat" se reemplaza por un Gestor de Archivos real
 *    sobre el disco ~/miniSO/disk/ (solo llamadas al sistema, sin system()).
 *  - Los juegos propios con ncurses se reemplazan por juegos de bsdgames
 *    ejecutados como procesos hijos (fork + exec + waitpid).
 *
 * Compilar: gcc -Wall -Wextra minios.c -o minios   (ya no requiere -lncurses)
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BLOCK 4096
#define MAXIN 512
#define HEX_BYTES 64
#define MAX_DEPTH 32

static char disk_root[PATH_MAX]; /* ruta real de ~/miniSO/disk */
static char log_path[PATH_MAX];  /* ~/miniSO/bitacora.log (fuera del disco) */

/* ------------------------------------------------------------------ */
/* Utilidades generales                                                */
/* ------------------------------------------------------------------ */

static int join_path(char *out, size_t n, const char *a, const char *b)
{
    int r = snprintf(out, n, "%s/%s", a, b);
    if (r < 0 || (size_t)r >= n) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static ssize_t read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;
    char *p = buf;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static void log_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void log_event(const char *fmt, ...)
{
    char msg[1024], line[1100], ts[32];
    va_list ap;
    time_t t = time(NULL);
    struct tm tmv;
    int len, fd;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    localtime_r(&t, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    len = snprintf(line, sizeof line, "[%s] %s\n", ts, msg);
    if (len < 0)
        return;
    if ((size_t)len >= sizeof line)
        len = (int)sizeof line - 1;
    fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    (void)write_all(fd, line, (size_t)len);
    close(fd);
}

/* Lee una línea sin el salto de línea. -1 en EOF. */
static int ask(const char *prompt, char *buf, size_t n)
{
    size_t l;
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin))
        return -1;
    l = strlen(buf);
    if (l > 0 && buf[l - 1] == '\n') {
        buf[--l] = '\0';
    } else {
        int c; /* línea demasiado larga: descartar el resto */
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    return 0;
}

/* Lee una opción numérica en [lo, hi]. Devuelve -1 si es inválida. */
static int read_option(int lo, int hi, int eof_val)
{
    char buf[MAXIN], *end;
    long v;

    if (ask("Seleccione una opción: ", buf, sizeof buf) < 0) {
        printf("\n");
        return eof_val;
    }
    v = strtol(buf, &end, 10);
    while (*end == ' ' || *end == '\t')
        end++;
    if (end == buf || *end != '\0' || v < lo || v > hi) {
        printf("Opción inválida. Ingrese un número entre %d y %d.\n", lo, hi);
        return -1;
    }
    return (int)v;
}

/* ------------------------------------------------------------------ */
/* Aislamiento del disco                                               */
/* ------------------------------------------------------------------ */

static int is_inside(const char *p)
{
    size_t n = strlen(disk_root);
    if (strncmp(p, disk_root, n) != 0)
        return 0;
    return p[n] == '\0' || p[n] == '/';
}

static int is_root(const char *p)
{
    return strcmp(p, disk_root) == 0;
}

/*
 * Convierte una ruta ingresada por el usuario (relativa a la raíz del
 * disco) en una ruta absoluta real, verificando que no salga del disco.
 * must_exist = 1: la ruta debe existir (realpath sobre la ruta completa).
 * must_exist = 0: se resuelve el directorio padre y se agrega el nombre.
 */
static int resolve(const char *in, int must_exist, char *out)
{
    char clean[MAXIN], tmp[PATH_MAX], real[PATH_MAX];
    const char *orig = in;
    size_t len;

    while (*in == '/')
        in++;
    snprintf(clean, sizeof clean, "%s", in);
    len = strlen(clean);
    while (len > 0 && clean[len - 1] == '/')
        clean[--len] = '\0';

    if (len == 0) {
        if (must_exist) {
            snprintf(out, PATH_MAX, "%s", disk_root);
            return 0;
        }
        fprintf(stderr, "Error: el nombre no puede estar vacío.\n");
        return -1;
    }
    if (join_path(tmp, sizeof tmp, disk_root, clean) < 0) {
        perror("ruta");
        return -1;
    }

    if (must_exist) {
        if (!realpath(tmp, real)) {
            perror(clean);
            return -1;
        }
    } else {
        char parent[PATH_MAX], nm[NAME_MAX + 1];
        char *slash = strrchr(tmp, '/');
        const char *name = slash + 1;

        if (*name == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            fprintf(stderr, "Error: nombre no válido.\n");
            return -1;
        }
        if (strlen(name) > NAME_MAX) {
            errno = ENAMETOOLONG;
            perror(clean);
            return -1;
        }
        snprintf(nm, sizeof nm, "%s", name);
        *slash = '\0';
        if (!realpath(tmp, parent)) {
            perror(clean);
            return -1;
        }
        if (join_path(real, sizeof real, parent, nm) < 0) {
            perror(clean);
            return -1;
        }
    }

    if (!is_inside(real)) {
        fprintf(stderr, "Acceso denegado: '%s' queda fuera del disco del sistema.\n", orig);
        log_event("SEGURIDAD: ruta rechazada '%s'", orig);
        return -1;
    }
    snprintf(out, PATH_MAX, "%s", real);
    return 0;
}

/* Si dst es un directorio existente, el destino pasa a ser dst/basename(src). */
static int adjust_dest(const char *src, char *dst)
{
    struct stat st;
    if (lstat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
        const char *base = strrchr(src, '/');
        char tmp[PATH_MAX];
        base = base ? base + 1 : src;
        if (join_path(tmp, sizeof tmp, dst, base) < 0) {
            perror("ruta");
            return -1;
        }
        snprintf(dst, PATH_MAX, "%s", tmp);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gestor de Archivos                                                  */
/* ------------------------------------------------------------------ */

static void perm_str(mode_t m, char *s)
{
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : '-';
    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_IXUSR) ? 'x' : '-';
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_IXGRP) ? 'x' : '-';
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_IXOTH) ? 'x' : '-';
    s[10] = '\0';
}

static void fmt_time(time_t t, char *buf, size_t n)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tmv);
}

static const char *type_name(mode_t m)
{
    if (S_ISDIR(m))
        return "directorio";
    if (S_ISREG(m))
        return "archivo";
    if (S_ISLNK(m))
        return "enlace";
    return "otro";
}

static int listarDirectorio(void)
{
    char in[MAXIN], dir[PATH_MAX];
    struct stat sb;
    DIR *d;
    struct dirent *e;
    int count = 0;

    if (ask("Directorio a listar (vacío = raíz del disco): ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, dir) < 0)
        return -1;
    if (stat(dir, &sb) < 0) {
        perror("stat");
        return -1;
    }
    if (!S_ISDIR(sb.st_mode)) {
        fprintf(stderr, "'%s' no es un directorio.\n", in);
        return -1;
    }
    d = opendir(dir);
    if (!d) {
        perror("opendir");
        return -1;
    }
    printf("%-28s %-11s %12s %-11s %s\n", "NOMBRE", "TIPO", "TAMAÑO (B)", "PERMISOS", "MODIFICADO");
    while ((e = readdir(d)) != NULL) {
        char full[PATH_MAX], perms[11], tbuf[32];
        struct stat st;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (join_path(full, sizeof full, dir, e->d_name) < 0 || lstat(full, &st) < 0) {
            perror(e->d_name);
            continue;
        }
        perm_str(st.st_mode, perms);
        fmt_time(st.st_mtime, tbuf, sizeof tbuf);
        printf("%-28s %-11s %12lld %-11s %s\n", e->d_name, type_name(st.st_mode),
               (long long)st.st_size, perms, tbuf);
        count++;
    }
    closedir(d);
    printf("(%d elementos)\n", count);
    return 0;
}

/* Lee líneas desde stdin y las escribe en fd hasta una línea con solo ".". */
static int capture_to_fd(int fd)
{
    char line[1024];
    int at_start = 1;

    printf("Escriba el contenido. Termine con una línea que contenga solo un punto (.)\n");
    while (fgets(line, sizeof line, stdin) != NULL) {
        size_t l = strlen(line);
        if (at_start && strcmp(line, ".\n") == 0)
            break;
        if (write_all(fd, line, l) < 0)
            return -1;
        at_start = (l > 0 && line[l - 1] == '\n');
    }
    return 0;
}

static int crearArchivoTexto(void)
{
    char in[MAXIN], path[PATH_MAX];
    int fd;

    if (ask("Nombre del archivo nuevo: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 0, path) < 0)
        return -1;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    if (capture_to_fd(fd) < 0) {
        perror("write");
        close(fd);
        return -1;
    }
    if (close(fd) < 0) {
        perror("close");
        return -1;
    }
    printf("Archivo '%s' creado.\n", in);
    return 0;
}

static int crearDirectorio(void)
{
    char in[MAXIN], path[PATH_MAX];

    if (ask("Nombre del directorio nuevo: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 0, path) < 0)
        return -1;
    if (mkdir(path, 0755) < 0) {
        perror("mkdir");
        return -1;
    }
    printf("Directorio '%s' creado.\n", in);
    return 0;
}

/* 1 = texto, 0 = binario, -1 = error */
static int is_text_file(const char *path)
{
    unsigned char buf[512];
    ssize_t n, i;
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return -1;
    n = read_full(fd, buf, sizeof buf);
    close(fd);
    if (n < 0)
        return -1;
    for (i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == 0 || c == 0x7f)
            return 0;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f')
            return 0;
    }
    return 1;
}

static void print_metadata(const char *name, const struct stat *st)
{
    char perms[11], tbuf[32];
    perm_str(st->st_mode, perms);
    fmt_time(st->st_mtime, tbuf, sizeof tbuf);
    printf("Nombre:    %s\n", name);
    printf("Tipo:      %s\n", type_name(st->st_mode));
    printf("Tamaño:    %lld bytes\n", (long long)st->st_size);
    printf("Permisos:  %s (%04o)\n", perms, (unsigned)(st->st_mode & 07777));
    printf("Modificado: %s\n", tbuf);
}

static void hex_dump(const unsigned char *b, size_t n)
{
    size_t off, j;
    for (off = 0; off < n; off += 16) {
        printf("%04zx  ", off);
        for (j = 0; j < 16; j++) {
            if (off + j < n)
                printf("%02x ", b[off + j]);
            else
                printf("   ");
            if (j == 7)
                printf(" ");
        }
        printf(" |");
        for (j = 0; j < 16 && off + j < n; j++)
            putchar(isprint(b[off + j]) ? b[off + j] : '.');
        printf("|\n");
    }
}

static int leerArchivo(void)
{
    char in[MAXIN], path[PATH_MAX];
    struct stat st;
    int kind, fd;

    if (ask("Archivo a leer: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, path) < 0)
        return -1;
    if (stat(path, &st) < 0) {
        perror("stat");
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        fprintf(stderr, "'%s' es un directorio; use la opción Listar.\n", in);
        return -1;
    }
    kind = is_text_file(path);
    if (kind < 0) {
        perror("open");
        return -1;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    if (kind == 1) {
        char buf[BLOCK];
        ssize_t n;
        printf("----- %s -----\n", in);
        fflush(stdout);
        while ((n = read(fd, buf, sizeof buf)) != 0) {
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                perror("read");
                close(fd);
                return -1;
            }
            if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) {
                perror("write");
                close(fd);
                return -1;
            }
        }
        printf("\n----- fin (%lld bytes) -----\n", (long long)st.st_size);
    } else {
        unsigned char buf[HEX_BYTES];
        ssize_t n = read_full(fd, buf, sizeof buf);
        if (n < 0) {
            perror("read");
            close(fd);
            return -1;
        }
        printf("Archivo no es de texto.\n");
        print_metadata(in, &st);
        printf("Primeros %zd bytes (hex):\n", n);
        hex_dump(buf, (size_t)n);
    }
    close(fd);
    return 0;
}

static int editarArchivo(void)
{
    char in[MAXIN], path[PATH_MAX], mode[MAXIN];
    struct stat st;
    int flags, fd, kind;

    if (ask("Archivo a editar: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, path) < 0)
        return -1;
    if (stat(path, &st) < 0) {
        perror("stat");
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "Solo se pueden editar archivos regulares.\n");
        return -1;
    }
    kind = is_text_file(path);
    if (kind < 0) {
        perror("open");
        return -1;
    }
    if (kind == 0) {
        fprintf(stderr, "'%s' no es un archivo de texto.\n", in);
        return -1;
    }
    if (ask("1) Agregar al final  2) Sobrescribir todo: ", mode, sizeof mode) < 0)
        return -1;
    if (strcmp(mode, "1") == 0) {
        flags = O_WRONLY | O_APPEND;
    } else if (strcmp(mode, "2") == 0) {
        flags = O_WRONLY | O_TRUNC;
    } else {
        fprintf(stderr, "Modo inválido.\n");
        return -1;
    }
    fd = open(path, flags);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    if (capture_to_fd(fd) < 0) {
        perror("write");
        close(fd);
        return -1;
    }
    if (close(fd) < 0) {
        perror("close");
        return -1;
    }
    printf("Archivo '%s' actualizado.\n", in);
    return 0;
}

/* 1 = idénticos, 0 = distintos, -1 = error */
static int files_identical(const char *a, const char *b)
{
    struct stat sa, sb;
    unsigned char ba[BLOCK], bb[BLOCK];
    int fa, fb, result = 1;
    long long off = 0;

    if (stat(a, &sa) < 0 || stat(b, &sb) < 0) {
        perror("stat");
        return -1;
    }
    printf("Tamaño origen: %lld bytes | copia: %lld bytes\n", (long long)sa.st_size,
           (long long)sb.st_size);
    if (sa.st_size != sb.st_size)
        return 0;
    fa = open(a, O_RDONLY);
    if (fa < 0) {
        perror("open origen");
        return -1;
    }
    fb = open(b, O_RDONLY);
    if (fb < 0) {
        perror("open copia");
        close(fa);
        return -1;
    }
    for (;;) {
        ssize_t na = read_full(fa, ba, sizeof ba);
        ssize_t nb = read_full(fb, bb, sizeof bb);
        ssize_t i;
        if (na < 0 || nb < 0) {
            perror("read");
            result = -1;
            break;
        }
        if (na != nb) {
            result = 0;
            break;
        }
        if (na == 0)
            break;
        for (i = 0; i < na; i++) {
            if (ba[i] != bb[i]) {
                printf("Primera diferencia en el byte %lld\n", off + i);
                result = 0;
                break;
            }
        }
        if (result == 0)
            break;
        off += na;
    }
    close(fa);
    close(fb);
    return result;
}

static int copiarArchivo(void)
{
    char in[MAXIN], src[PATH_MAX], dst[PATH_MAX], buf[BLOCK];
    struct stat ss;
    int in_fd, out_fd, ok = 1, same;
    long long total = 0;
    ssize_t n;

    if (ask("Archivo origen: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, src) < 0)
        return -1;
    if (stat(src, &ss) < 0) {
        perror("stat");
        return -1;
    }
    if (!S_ISREG(ss.st_mode)) {
        fprintf(stderr, "Solo se pueden copiar archivos regulares.\n");
        return -1;
    }
    if (ask("Destino (nombre nuevo o directorio): ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 0, dst) < 0 || adjust_dest(src, dst) < 0)
        return -1;

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        perror("open origen");
        return -1;
    }
    out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, ss.st_mode & 0777);
    if (out_fd < 0) {
        perror("open destino");
        close(in_fd);
        return -1;
    }
    while ((n = read(in_fd, buf, sizeof buf)) != 0) {
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            ok = 0;
            break;
        }
        if (write_all(out_fd, buf, (size_t)n) < 0) {
            perror("write");
            ok = 0;
            break;
        }
        total += n;
    }
    close(in_fd);
    if (close(out_fd) < 0) {
        perror("close");
        ok = 0;
    }
    if (!ok) {
        unlink(dst); /* no dejar una copia parcial */
        return -1;
    }
    printf("Copiados %lld bytes en bloques de %d.\n", total, BLOCK);
    same = files_identical(src, dst);
    if (same == 1) {
        printf("Verificación: la copia es IDÉNTICA al original.\n");
        return 0;
    }
    if (same == 0)
        printf("Verificación: la copia DIFIERE del original.\n");
    return -1;
}

static int renombrarArchivo(void)
{
    char in[MAXIN], src[PATH_MAX], dst[PATH_MAX];
    struct stat st;

    if (ask("Archivo o directorio origen: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, src) < 0)
        return -1;
    if (is_root(src)) {
        fprintf(stderr, "No se puede mover la raíz del disco.\n");
        return -1;
    }
    if (ask("Nuevo nombre o directorio destino: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 0, dst) < 0 || adjust_dest(src, dst) < 0)
        return -1;
    if (lstat(dst, &st) == 0) { /* rename() sobrescribiría: lo evitamos */
        errno = EEXIST;
        perror("rename");
        return -1;
    }
    if (rename(src, dst) < 0) {
        perror("rename");
        return -1;
    }
    printf("Renombrado/movido correctamente.\n");
    return 0;
}

static int cambiarPermisos(void)
{
    char in[MAXIN], path[PATH_MAX], ms[MAXIN], *end, perms[11];
    struct stat st;
    long mode;

    if (ask("Archivo: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, path) < 0)
        return -1;
    if (ask("Nuevos permisos en octal (ej. 644): ", ms, sizeof ms) < 0)
        return -1;
    mode = strtol(ms, &end, 8);
    if (end == ms || *end != '\0' || mode < 0 || mode > 0777) {
        fprintf(stderr, "Permisos inválidos (use un octal entre 000 y 777).\n");
        return -1;
    }
    if (chmod(path, (mode_t)mode) < 0) {
        perror("chmod");
        return -1;
    }
    if (stat(path, &st) == 0) {
        perm_str(st.st_mode, perms);
        printf("Permisos actuales: %s (%04o)\n", perms, (unsigned)(st.st_mode & 07777));
    }
    return 0;
}

static int search_dir(const char *dir, const char *pat, int ext_mode, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (!d) {
        perror(dir);
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        char full[PATH_MAX];
        struct stat st;
        int match;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (join_path(full, sizeof full, dir, e->d_name) < 0 || lstat(full, &st) < 0)
            continue;
        if (ext_mode) {
            size_t ln = strlen(e->d_name), lp = strlen(pat);
            match = ln >= lp && strcasecmp(e->d_name + ln - lp, pat) == 0;
        } else {
            match = strcasestr(e->d_name, pat) != NULL;
        }
        if (match) {
            printf("  %s  (%s, %lld bytes)\n", full + strlen(disk_root) + 1,
                   type_name(st.st_mode), (long long)st.st_size);
            found++;
        }
        if (S_ISDIR(st.st_mode) && depth < MAX_DEPTH)
            found += search_dir(full, pat, ext_mode, depth + 1);
    }
    closedir(d);
    return found;
}

static int buscarArchivos(void)
{
    char in[MAXIN];
    const char *pat;
    int found;

    if (ask("Nombre o extensión a buscar (ej. nota, .txt): ", in, sizeof in) < 0)
        return -1;
    pat = in;
    if (*pat == '*')
        pat++;
    if (*pat == '\0') {
        fprintf(stderr, "Búsqueda vacía.\n");
        return -1;
    }
    found = search_dir(disk_root, pat, pat[0] == '.' && pat[1] != '\0', 0);
    printf("%d coincidencia(s).\n", found);
    return 0;
}

static int eliminarArchivo(void)
{
    char in[MAXIN], path[PATH_MAX], conf[MAXIN];
    struct stat st;

    if (ask("Archivo o directorio vacío a eliminar: ", in, sizeof in) < 0)
        return -1;
    if (resolve(in, 1, path) < 0)
        return -1;
    if (is_root(path)) {
        fprintf(stderr, "No se puede eliminar la raíz del disco.\n");
        return -1;
    }
    if (lstat(path, &st) < 0) {
        perror("lstat");
        return -1;
    }
    printf("¿Eliminar %s '%s'? (s/N): ", S_ISDIR(st.st_mode) ? "el directorio" : "el archivo", in);
    fflush(stdout);
    if (!fgets(conf, sizeof conf, stdin))
        return -1;
    if (conf[0] != 's' && conf[0] != 'S') {
        printf("Operación cancelada.\n");
        return 0;
    }
    if (S_ISDIR(st.st_mode) ? rmdir(path) < 0 : unlink(path) < 0) {
        perror(S_ISDIR(st.st_mode) ? "rmdir" : "unlink");
        return -1;
    }
    printf("Eliminado.\n");
    return 0;
}

static void gestorArchivos(void)
{
    for (;;) {
        int op, rc = 0;
        const char *name;

        printf("\n------- GESTOR DE ARCHIVOS -------\n"
               " 1. Listar\n"
               " 2. Crear archivo de texto\n"
               " 3. Crear directorio\n"
               " 4. Leer archivo\n"
               " 5. Editar archivo de texto\n"
               " 6. Copiar archivo\n"
               " 7. Renombrar / mover\n"
               " 8. Cambiar permisos\n"
               " 9. Buscar por nombre o extensión\n"
               "10. Eliminar archivo o directorio vacío\n"
               "11. Volver al menú principal\n"
               "----------------------------------\n");
        op = read_option(1, 11, 11);
        switch (op) {
        case 1: name = "Listar"; rc = listarDirectorio(); break;
        case 2: name = "Crear archivo"; rc = crearArchivoTexto(); break;
        case 3: name = "Crear directorio"; rc = crearDirectorio(); break;
        case 4: name = "Leer archivo"; rc = leerArchivo(); break;
        case 5: name = "Editar archivo"; rc = editarArchivo(); break;
        case 6: name = "Copiar archivo"; rc = copiarArchivo(); break;
        case 7: name = "Renombrar/mover"; rc = renombrarArchivo(); break;
        case 8: name = "Cambiar permisos"; rc = cambiarPermisos(); break;
        case 9: name = "Buscar"; rc = buscarArchivos(); break;
        case 10: name = "Eliminar"; rc = eliminarArchivo(); break;
        case 11: return;
        default: continue;
        }
        log_event("Gestor de archivos: %s -> %s", name, rc == 0 ? "OK" : "ERROR");
    }
}

/* ------------------------------------------------------------------ */
/* Centro de Juegos                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *paths[4];
} Game;

static const Game games[] = {
    {"Tetris", {"/usr/games/tetris-bsd", "/usr/games/tetris", "/usr/local/games/tetris-bsd", NULL}},
    {"Snake", {"/usr/games/snake", "/usr/games/snake-bsd", "/usr/local/games/snake", NULL}},
};
#define NGAMES (sizeof games / sizeof games[0])

static void ejecutarJuego(const Game *g)
{
    char exe[PATH_MAX], hora[32];
    char *args[2];
    const char *found = NULL;
    int first_errno = 0, status = 0, have_tty;
    size_t i;
    struct termios saved;
    struct sigaction ign, old;
    time_t start, end;
    pid_t pid, r;

    /* 1. Verificar con access() */
    for (i = 0; i < 4 && g->paths[i]; i++) {
        if (access(g->paths[i], X_OK) == 0) {
            found = g->paths[i];
            break;
        }
        if (first_errno == 0)
            first_errno = errno;
    }
    if (!found) {
        fprintf(stderr, "No se puede ejecutar %s: access(\"%s\"): %s\n", g->name, g->paths[0],
                strerror(first_errno));
        fprintf(stderr, "Instale los juegos con: sudo apt install bsdgames\n");
        log_event("Juegos: %s no disponible (%s)", g->name, strerror(first_errno));
        return;
    }
    snprintf(exe, sizeof exe, "%s", found);

    /* 7. Guardar estado de la terminal */
    have_tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0;

    /* 2. Hora de inicio */
    start = time(NULL);
    fmt_time(start, hora, sizeof hora);
    printf("\nIniciando %s (%s) a las %s...\n", g->name, exe, hora);
    fflush(stdout);
    fflush(stderr);

    /* 5a. El padre ignora SIGINT (antes del fork, sin ventana de carrera) */
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT, &ign, &old);

    /* 3. fork */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        sigaction(SIGINT, &old, NULL);
        return;
    }
    if (pid == 0) {
        /* 4. Hijo: SIGINT por defecto y exec */
        struct sigaction dfl;
        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(SIGINT, &dfl, NULL);
        args[0] = exe;
        args[1] = NULL;
        execv(exe, args);
        perror("execv");
        _exit(127);
    }

    /* 5b. Padre: esperar al hijo */
    do {
        r = waitpid(pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        perror("waitpid");
    end = time(NULL);

    /* 5c. Restaurar señales y terminal */
    sigaction(SIGINT, &old, NULL);
    if (have_tty)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    clearerr(stdin);

    /* 6. Informe */
    printf("\n===== Informe de la partida =====\n");
    printf("Juego:    %s\n", g->name);
    printf("PID hijo: %ld\n", (long)pid);
    if (r < 0) {
        printf("Estado:   desconocido (waitpid falló)\n");
    } else if (WIFEXITED(status)) {
        printf("Estado:   terminó normalmente (código de salida %d)%s\n", WEXITSTATUS(status),
               WEXITSTATUS(status) == 127 ? " - no se pudo ejecutar el juego" : "");
    } else if (WIFSIGNALED(status)) {
        printf("Estado:   terminado por la señal %d (%s)\n", WTERMSIG(status),
               strsignal(WTERMSIG(status)));
    }
    printf("Duración: %.0f segundos\n", difftime(end, start));
    printf("=================================\n");
    log_event("Juegos: %s pid=%ld duración=%.0fs", g->name, (long)pid, difftime(end, start));
}

static void centroJuegos(void)
{
    for (;;) {
        size_t i;
        int op;

        printf("\n------- CENTRO DE JUEGOS -------\n");
        for (i = 0; i < NGAMES; i++)
            printf(" %zu. %s\n", i + 1, games[i].name);
        printf(" 0. Volver al menú principal\n--------------------------------\n");
        op = read_option(0, (int)NGAMES, 0);
        if (op == 0)
            return;
        if (op > 0)
            ejecutarJuego(&games[op - 1]);
    }
}

/* ------------------------------------------------------------------ */
/* Bitácora y programa principal                                       */
/* ------------------------------------------------------------------ */

static void verBitacora(void)
{
    char buf[BLOCK];
    ssize_t n;
    int fd = open(log_path, O_RDONLY);

    if (fd < 0) {
        if (errno == ENOENT)
            printf("La bitácora está vacía.\n");
        else
            perror("open bitácora");
        return;
    }
    printf("\n------- BITÁCORA DEL SISTEMA -------\n");
    fflush(stdout);
    while ((n = read(fd, buf, sizeof buf)) != 0) {
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            break;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) {
            perror("write");
            break;
        }
    }
    close(fd);
    printf("------------------------------------\n");
}

static int init_system(void)
{
    const char *home = getenv("HOME");
    char base[PATH_MAX], disk[PATH_MAX];

    if (!home || !*home) {
        fprintf(stderr, "La variable HOME no está definida.\n");
        return -1;
    }
    if (join_path(base, sizeof base, home, "miniSO") < 0 ||
        join_path(disk, sizeof disk, base, "disk") < 0) {
        perror("ruta");
        return -1;
    }
    if (mkdir(base, 0755) < 0 && errno != EEXIST) {
        perror("mkdir ~/miniSO");
        return -1;
    }
    if (mkdir(disk, 0755) < 0 && errno != EEXIST) {
        perror("mkdir ~/miniSO/disk");
        return -1;
    }
    if (!realpath(disk, disk_root)) {
        perror("realpath");
        return -1;
    }
    if (join_path(log_path, sizeof log_path, base, "bitacora.log") < 0) {
        perror("ruta");
        return -1;
    }
    return 0;
}

static void mostrarMenu(void)
{
    printf("\n========= MINI OS v2.0 =========\n"
           "1. Gestor de Archivos\n"
           "2. Centro de Juegos\n"
           "3. Ver Bitácora del Sistema\n"
           "0. Apagar Sistema\n"
           "================================\n");
}

int main(void)
{
    int sistemaActivo = 1;

    /* Cada printf se muestra de inmediato (igual que en el Taller 1) */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (init_system() < 0)
        return 1;
    log_event("Sistema iniciado (disco: %s)", disk_root);

    while (sistemaActivo) {
        int op;

        mostrarMenu();
        op = read_option(0, 3, 0);
        switch (op) {
        case 1: gestorArchivos(); break;
        case 2: centroJuegos(); break;
        case 3: verBitacora(); break;
        case 0:
            log_event("Sistema apagado");
            printf("Apagando el sistema de forma segura...\n");
            sistemaActivo = 0;
            break;
        default: break; /* entrada inválida: ya se informó, se repite el menú */
        }
    }

    printf("Sistema apagado. Hasta pronto.\n");
    return 0;
}
