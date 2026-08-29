#ifndef CHTTPD_HTTP_REQUEST_H
#define CHTTPD_HTTP_REQUEST_H

#include <stddef.h>

/*
 * http_request.h
 *
 * Fase 3: parsing strutturato della request-line e degli header HTTP
 * a partire dal buffer grezzo già raccolto da conn.c. Nessuna
 * risoluzione del filesystem (Fase 4) né generazione di risposta
 * (Fase 5) è svolta qui.
 */

#define HTTP_MAX_METHOD_LEN   16
#define HTTP_MAX_PATH_LEN     1024
#define HTTP_MAX_QUERY_LEN    512
#define HTTP_MAX_HEADER_NAME  64
#define HTTP_MAX_HEADER_VALUE 256
#define HTTP_MAX_HEADERS      32

enum http_method {
    HTTP_METHOD_GET,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_UNSUPPORTED
};

enum http_version {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1
};

struct http_header {
    char name[HTTP_MAX_HEADER_NAME];
    char value[HTTP_MAX_HEADER_VALUE];
};

struct http_request {
    enum http_method method;
    char method_str[HTTP_MAX_METHOD_LEN];
    char path[HTTP_MAX_PATH_LEN];    /* raw, non ancora decodificato: vedi Fase 4 */
    char query[HTTP_MAX_QUERY_LEN];
    enum http_version version;
    struct http_header headers[HTTP_MAX_HEADERS];
    size_t header_count;
};

enum http_parse_result {
    HTTP_PARSE_OK,
    HTTP_PARSE_BAD_REQUEST
};

/*
 * http_parse_request
 *
 * Esegue il parsing della request-line e degli header HTTP a partire
 * dal buffer grezzo 'raw' (lunghezza 'len'), già letto dal socket.
 * Popola 'req' in caso di successo strutturale.
 *
 * Il parsing accetta sia terminatori di riga CRLF (standard) sia LF
 * "nudo" per facilitare i test manuali con strumenti come netcat;
 * questo non altera la semantica HTTP osservabile dal client.
 *
 * err_detail, se non NULL, riceve una breve descrizione testuale del
 * motivo del fallimento (utile per il logging), troncata a
 * 'err_detail_len' byte.
 *
 * Ritorna HTTP_PARSE_OK se la request-line e gli header sono
 * sintatticamente validi. Questo NON garantisce che il metodo sia
 * supportato: il chiamante deve controllare req->method e gestire
 * HTTP_METHOD_UNSUPPORTED (→ 501, vedi AGENTS.md sezione 4).
 * Ritorna HTTP_PARSE_BAD_REQUEST in caso di richiesta malformata o
 * incompleta (→ 400, vedi AGENTS.md sezione 5).
 */
enum http_parse_result http_parse_request(const char *raw, size_t len,
                                           struct http_request *req,
                                           char *err_detail,
                                           size_t err_detail_len);

/*
 * http_get_header
 *
 * Cerca un header per nome (case-insensitive, come da RFC 7230) in una
 * richiesta già analizzata. Ritorna il valore (stringa terminata da
 * NUL) se trovato, altrimenti NULL.
 */
const char *http_get_header(const struct http_request *req, const char *name);

#endif /* CHTTPD_HTTP_REQUEST_H */
