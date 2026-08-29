/*
 * conn.c
 *
 * Gestione di una singola connessione già accettata: lettura della
 * richiesta grezza in un buffer (Fase 2), parsing strutturato tramite
 * http_request.c (Fase 3), risoluzione del path tramite fsmap.c
 * (Fase 4), generazione della risposta HTTP completa tramite
 * http_response.c (Fase 5) e logging tramite log.c (Fase 6). Dalla
 * Fase 7, ogni connessione viene gestita in un thread POSIX proprio
 * (vedi il ciclo accept() in main.c): questa funzione non contiene
 * stato condiviso mutabile tra chiamate concorrenti.
 *
 * Ogni richiesta gestita produce sempre una riga di log in formato
 * Common Log Format (vedi log_request()). Se 'verbose' è non-zero
 * (flag -v/--verbose-log), viene stampato in aggiunta un dump
 * diagnostico dettagliato — la modalità usata durante lo sviluppo
 * delle Fasi 3-5 — con metodo, path, header ed esito della
 * risoluzione per ogni richiesta.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#include "conn.h"
#include "http_request.h"
#include "fsmap.h"
#include "http_response.h"
#include "log.h"

#define REQUEST_BUFFER_SIZE 8192
#define REQUEST_LINE_LOG_LEN 300

/*
 * extract_request_line
 *
 * Estrae la prima riga di 'buf' (fino a 'total' byte), così come
 * ricevuta dal client, in 'out' (bounded, terminata da NUL), per
 * poterla usare nel log anche quando il parsing strutturato fallisce
 * o il metodo non è supportato. Accetta sia CRLF sia LF nudo, come
 * http_request.c. Se non viene trovato alcun terminatore di riga nel
 * buffer letto (richiesta incompleta), usa quanto ricevuto finora.
 */
static void extract_request_line(const char *buf, size_t total, char *out, size_t out_size)
{
    const char *nl = memchr(buf, '\n', total);
    size_t len;

    if (nl != NULL) {
        len = (size_t) (nl - buf);
        if (len > 0 && buf[len - 1] == '\r') {
            len--;
        }
    } else {
        len = total;
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, buf, len);
    out[len] = '\0';
}

void handle_connection(int client_fd, const struct sockaddr_in *client_addr,
                        const char *root_dir, int verbose)
{
    char buf[REQUEST_BUFFER_SIZE];
    size_t total = 0;
    ssize_t n;
    char ip[INET_ADDRSTRLEN];
    int port = ntohs(client_addr->sin_port);
    int timed_out = 0;

    if (inet_ntop(AF_INET, &client_addr->sin_addr, ip, sizeof(ip)) == NULL) {
        strncpy(ip, "?", sizeof(ip));
    }

    /*
     * Ciclo di lettura: read() può restituire meno byte di quanti
     * richiesti (comportamento normale dei socket TCP), quindi si
     * accumula nel buffer finché non si trova la sequenza CRLFCRLF
     * (fine degli header), il client chiude la connessione (n == 0),
     * si verifica un errore, il timeout di lettura scade (Fase 8:
     * SO_RCVTIMEO impostato da main.c su ogni socket accettato, per
     * evitare che un client lento/inattivo blocchi un thread
     * indefinitamente), oppure il buffer si esaurisce.
     */
    while (total < sizeof(buf) - 1) {
        n = read(client_fd, buf + total, sizeof(buf) - 1 - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                timed_out = 1; /* timeout di lettura (SO_RCVTIMEO) scaduto */
            } else {
                perror("read");
            }
            break;
        }
        if (n == 0) {
            break; /* client ha chiuso la connessione */
        }
        total += (size_t) n;
        buf[total] = '\0';

        if (strstr(buf, "\r\n\r\n") != NULL) {
            break; /* fine degli header individuata */
        }
    }

    if (timed_out) {
        if (total > 0) {
            /* Erano già arrivati dei dati (richiesta iniziata ma mai
               completata entro il timeout): rispondiamo con 408,
               come da semantica HTTP standard per questo caso. */
            char request_line[REQUEST_LINE_LOG_LEN];
            long bytes;

            extract_request_line(buf, total, request_line, sizeof(request_line));
            bytes = http_send_error(client_fd, HTTP_METHOD_GET, 408, "Request Timeout", NULL);
            log_request(ip, request_line, 408, bytes);
            if (verbose) {
                printf("--- Connessione da %s:%d | 408 Request Timeout (richiesta incompleta) ---\n\n",
                       ip, port);
                fflush(stdout);
            }
        }
        /* Se non era arrivato nulla, non c'è una richiesta a cui
           rispondere (e nulla di significativo da loggare): si chiude
           semplicemente la connessione, come per il caso 'total == 0'
           sotto. */
        close(client_fd);
        return;
    }

    if (total == 0) {
        /* Nessun dato ricevuto: il client ha chiuso subito la
           connessione, non c'è nulla da registrare nel log. */
        close(client_fd);
        return;
    }

    {
        struct http_request req;
        char err_detail[128];
        enum http_parse_result pr;
        char request_line[REQUEST_LINE_LOG_LEN];
        int status = 0;
        long bytes = 0;

        extract_request_line(buf, total, request_line, sizeof(request_line));

        pr = http_parse_request(buf, total, &req, err_detail, sizeof(err_detail));

        if (pr != HTTP_PARSE_OK) {
            status = 400;
            bytes = http_send_error(client_fd, req.method, 400, "Bad Request", NULL);
            if (verbose) {
                printf("--- Connessione da %s:%d | 400 Bad Request (%s) ---\n\n",
                       ip, port, err_detail);
            }
        } else if (req.method == HTTP_METHOD_UNSUPPORTED) {
            status = 501;
            bytes = http_send_error(client_fd, req.method, 501, "Not Implemented", NULL);
            if (verbose) {
                printf("--- Connessione da %s:%d | 501 Not Implemented (metodo: %s) ---\n\n",
                       ip, port, req.method_str);
            }
        } else {
            struct fsmap_lookup lookup;

            if (verbose) {
                size_t i;

                printf("--- Connessione da %s:%d | richiesta valida ---\n", ip, port);
                printf("  Metodo   : %s\n", req.method_str);
                printf("  Path     : %s\n", req.path);
                printf("  Query    : %s\n", req.query[0] != '\0' ? req.query : "(nessuna)");
                printf("  Versione : %s\n",
                       req.version == HTTP_VERSION_1_1 ? "HTTP/1.1" : "HTTP/1.0");
                printf("  Header (%lu):\n", (unsigned long) req.header_count);
                for (i = 0; i < req.header_count; i++) {
                    printf("    %s: %s\n", req.headers[i].name, req.headers[i].value);
                }
            }

            fsmap_resolve(root_dir, req.path, &lookup);

            switch (lookup.result) {
            case FSMAP_OK: {
                long sent = http_send_file(client_fd, req.method, lookup.resolved_path);

                if (sent >= 0) {
                    status = 200;
                    bytes = sent;
                    if (verbose) {
                        printf("  Risposta : 200 OK -> %s (%ld byte)\n\n",
                               lookup.resolved_path, sent);
                    }
                } else {
                    /* Race TOCTOU: il file era valido in fsmap.c ma
                       non più apribile/leggibile a questo punto. */
                    status = 500;
                    bytes = http_send_error(client_fd, req.method, 500,
                                             "Internal Server Error", NULL);
                    if (verbose) {
                        printf("  Risposta : 500 Internal Server Error (apertura file fallita: %s)\n\n",
                               lookup.resolved_path);
                    }
                }
                break;
            }
            case FSMAP_REDIRECT:
                status = 301;
                bytes = http_send_error(client_fd, req.method, 301, "Moved Permanently",
                                         lookup.redirect_location);
                if (verbose) {
                    printf("  Risposta : 301 Moved Permanently -> %s\n\n",
                           lookup.redirect_location);
                }
                break;
            case FSMAP_NOT_FOUND:
                status = 404;
                bytes = http_send_error(client_fd, req.method, 404, "Not Found", NULL);
                if (verbose) {
                    printf("  Risposta : 404 Not Found\n\n");
                }
                break;
            case FSMAP_BAD_REQUEST:
                status = 400;
                bytes = http_send_error(client_fd, req.method, 400, "Bad Request", NULL);
                if (verbose) {
                    printf("  Risposta : 400 Bad Request (percent-encoding malformato)\n\n");
                }
                break;
            case FSMAP_DIR_NO_INDEX:
                status = 403;
                bytes = http_send_error(client_fd, req.method, 403, "Forbidden", NULL);
                if (verbose) {
                    printf("  Risposta : 403 Forbidden (directory senza index.html)\n\n");
                }
                break;
            case FSMAP_FORBIDDEN:
            default:
                status = 403;
                bytes = http_send_error(client_fd, req.method, 403, "Forbidden", NULL);
                if (verbose) {
                    printf("  Risposta : 403 Forbidden (permessi o path traversal)\n\n");
                }
                break;
            }
        }

        /* Riga di log CLF: sempre stampata, indipendentemente da 'verbose'. */
        log_request(ip, request_line, status, bytes);

        if (verbose) {
            fflush(stdout);
        }
    }

    close(client_fd);
}
