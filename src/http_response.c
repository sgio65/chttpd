/*
 * http_response.c
 *
 * Fase 5: costruzione ed invio della risposta HTTP completa.
 * - http_send_file(): risposta 200 OK con Content-Type dedotto
 *   dall'estensione ed invio del corpo a blocchi (o nessun corpo per
 *   HEAD).
 * - http_send_error(): risposta di errore con status-line, header
 *   essenziali (incluso Location per il 301) e un piccolo corpo HTML.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "http_response.h"
#include "version.h"

#define SERVER_NAME "chttpd/" CHTTPD_VERSION
#define IO_BLOCK_SIZE 8192

/*
 * send_all
 *
 * Scrive esattamente 'len' byte su 'fd', gestendo le scritture
 * parziali (comportamento normale dei socket TCP) e ritentando su
 * EINTR. Ritorna 0 in caso di successo, -1 in caso di errore
 * irreversibile (es. connessione chiusa dal client: la scrittura
 * viene interrotta, non è un errore fatale per il processo dato che
 * SIGPIPE è ignorato in main.c).
 */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t) n;
    }
    return 0;
}

static void format_http_date(char *buf, size_t buf_size)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    /*
     * gmtime_r() (variante thread-safe di gmtime()) è necessaria da
     * quando più thread possono generare risposte in parallelo
     * (Fase 7): scrive nel buffer fornito dal chiamante invece che in
     * un buffer statico interno condiviso.
     */
    struct tm *tm_info = gmtime_r(&now, &tm_buf);

    if (tm_info != NULL) {
        strftime(buf, buf_size, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    } else {
        strncpy(buf, "Thu, 01 Jan 1970 00:00:00 GMT", buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
}

struct mime_entry {
    const char *ext;
    const char *type;
};

/* Tabella MIME minima, come da AGENTS.md §8. */
static const struct mime_entry MIME_TABLE[] = {
    { ".html", "text/html" },
    { ".htm",  "text/html" },
    { ".txt",  "text/plain" },
    { ".css",  "text/css" },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".png",  "image/png" },
    { ".jpg",  "image/jpeg" },
    { ".jpeg", "image/jpeg" },
    { ".gif",  "image/gif" },
    { ".svg",  "image/svg+xml" },
    { ".pdf",  "application/pdf" }
};
#define MIME_TABLE_SIZE (sizeof(MIME_TABLE) / sizeof(MIME_TABLE[0]))

static const char *get_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    char ext_lower[16];
    size_t len;
    size_t i;
    size_t j;

    if (dot == NULL) {
        return "application/octet-stream";
    }
    len = strlen(dot);
    if (len >= sizeof(ext_lower)) {
        return "application/octet-stream";
    }
    for (j = 0; j < len; j++) {
        ext_lower[j] = (char) tolower((unsigned char) dot[j]);
    }
    ext_lower[len] = '\0';

    for (i = 0; i < MIME_TABLE_SIZE; i++) {
        if (strcmp(ext_lower, MIME_TABLE[i].ext) == 0) {
            return MIME_TABLE[i].type;
        }
    }
    return "application/octet-stream";
}

long http_send_file(int fd, enum http_method method, const char *resolved_path)
{
    int file_fd;
    struct stat st;
    char header[512];
    int header_len;
    char date_buf[64];
    const char *mime;
    long body_sent = 0;

    file_fd = open(resolved_path, O_RDONLY);
    if (file_fd < 0) {
        return -1;
    }
    if (fstat(file_fd, &st) != 0) {
        close(file_fd);
        return -1;
    }

    mime = get_mime_type(resolved_path);
    format_http_date(date_buf, sizeof(date_buf));

    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: " SERVER_NAME "\r\n"
        "Date: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        date_buf, mime, (long) st.st_size);

    if (header_len < 0 || (size_t) header_len >= sizeof(header)) {
        close(file_fd);
        return -1;
    }

    if (send_all(fd, header, (size_t) header_len) != 0) {
        close(file_fd);
        return 0; /* client disconnesso: nessun corpo inviato, non è un errore del server */
    }

    if (method != HTTP_METHOD_HEAD) {
        char buf[IO_BLOCK_SIZE];
        ssize_t n;

        while ((n = read(file_fd, buf, sizeof(buf))) > 0) {
            if (send_all(fd, buf, (size_t) n) != 0) {
                break; /* client disconnesso durante il trasferimento */
            }
            body_sent += (long) n;
        }
    }

    close(file_fd);
    return body_sent;
}

long http_send_html(int fd, enum http_method method, const char *html, size_t html_len)
{
    char header[256];
    int header_len;
    char date_buf[64];

    format_http_date(date_buf, sizeof(date_buf));

    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: " SERVER_NAME "\r\n"
        "Date: %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "\r\n",
        date_buf, (unsigned long) html_len);

    if (header_len < 0 || (size_t) header_len >= sizeof(header)) {
        return -1;
    }

    if (send_all(fd, header, (size_t) header_len) != 0) {
        return 0; /* client disconnesso: nessun corpo inviato */
    }

    if (method != HTTP_METHOD_HEAD) {
        if (send_all(fd, html, html_len) != 0) {
            return 0; /* client disconnesso durante l'invio */
        }
        return (long) html_len;
    }

    return 0;
}
long http_send_error(int fd, enum http_method method, int status_code,
                      const char *reason, const char *extra)
{
    char body[256];
    char header[512];
    int body_len;
    int header_len;
    char date_buf[64];

    body_len = snprintf(body, sizeof(body),
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1></body></html>",
        status_code, reason, status_code, reason);
    if (body_len < 0) {
        body_len = 0;
    } else if ((size_t) body_len >= sizeof(body)) {
        body_len = (int) sizeof(body) - 1;
    }

    format_http_date(date_buf, sizeof(date_buf));

    if (status_code == 301 && extra != NULL) {
        header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\n"
            "Server: " SERVER_NAME "\r\n"
            "Date: %s\r\n"
            "Location: %s\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            status_code, reason, date_buf, extra, body_len);
    } else {
        header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\n"
            "Server: " SERVER_NAME "\r\n"
            "Date: %s\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            status_code, reason, date_buf, body_len);
    }

    if (header_len < 0 || (size_t) header_len >= sizeof(header)) {
        return 0; /* non dovrebbe accadere con reason/extra di lunghezza ragionevole */
    }

    if (send_all(fd, header, (size_t) header_len) != 0) {
        return 0; /* client già disconnesso: nessun corpo inviato */
    }
    if (method != HTTP_METHOD_HEAD) {
        if (send_all(fd, body, (size_t) body_len) != 0) {
            return 0; /* invio del corpo interrotto: client disconnesso */
        }
        return (long) body_len;
    }
    return 0;
}
