/*
 * http_request.c
 *
 * Fase 3: parsing della request-line ("METODO PATH VERSIONE") e degli
 * header HTTP a partire dal buffer grezzo già letto da conn.c.
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>  /* strcasecmp (POSIX) */

#include "http_request.h"

static void set_err(char *err_detail, size_t err_detail_len, const char *msg)
{
    if (err_detail != NULL && err_detail_len > 0) {
        strncpy(err_detail, msg, err_detail_len - 1);
        err_detail[err_detail_len - 1] = '\0';
    }
}

/*
 * Individua la fine della riga corrente a partire da 'p' (fino a
 * 'end' escluso), accettando sia CRLF sia LF nudo. Restituisce il
 * puntatore al carattere successivo al terminatore, oppure NULL se
 * nessun terminatore è presente nel buffer. 'line_len' (se non NULL)
 * riceve la lunghezza della riga, terminatore escluso.
 */
static const char *find_line_end(const char *p, const char *end, size_t *line_len)
{
    const char *nl;

    nl = memchr(p, '\n', (size_t) (end - p));
    if (nl == NULL) {
        return NULL;
    }
    if (line_len != NULL) {
        if (nl > p && *(nl - 1) == '\r') {
            *line_len = (size_t) (nl - 1 - p);
        } else {
            *line_len = (size_t) (nl - p);
        }
    }
    return nl + 1;
}

static void copy_bounded(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t n;

    if (dst_size == 0) {
        return;
    }
    n = (src_len < dst_size - 1) ? src_len : dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int parse_request_line(const char *line, size_t line_len,
                               struct http_request *req,
                               char *err_detail, size_t err_detail_len)
{
    const char *p = line;
    const char *end = line + line_len;
    const char *sp1;
    const char *sp2;
    const char *target;
    const char *version;
    const char *qmark;
    size_t method_len;
    size_t target_len;
    size_t version_len;

    /* METODO: dall'inizio della riga fino al primo spazio */
    sp1 = memchr(p, ' ', (size_t) (end - p));
    if (sp1 == NULL) {
        set_err(err_detail, err_detail_len,
                "request-line malformata (manca separatore dopo il metodo)");
        return -1;
    }
    method_len = (size_t) (sp1 - p);
    if (method_len == 0 || method_len >= HTTP_MAX_METHOD_LEN) {
        set_err(err_detail, err_detail_len, "metodo HTTP mancante o troppo lungo");
        return -1;
    }
    copy_bounded(req->method_str, sizeof(req->method_str), p, method_len);

    /* TARGET: dopo il metodo (tollerando spazi multipli) fino al prossimo spazio */
    p = sp1 + 1;
    while (p < end && *p == ' ') {
        p++;
    }
    sp2 = memchr(p, ' ', (size_t) (end - p));
    if (sp2 == NULL) {
        set_err(err_detail, err_detail_len,
                "request-line malformata (manca la versione HTTP)");
        return -1;
    }
    target = p;
    target_len = (size_t) (sp2 - p);
    if (target_len == 0) {
        set_err(err_detail, err_detail_len, "target della richiesta mancante");
        return -1;
    }

    /* VERSIONE: dopo il target (tollerando spazi multipli) fino a fine riga */
    p = sp2 + 1;
    while (p < end && *p == ' ') {
        p++;
    }
    version = p;
    version_len = (size_t) (end - p);
    if (version_len == 0) {
        set_err(err_detail, err_detail_len, "versione HTTP mancante");
        return -1;
    }

    /* Il target deve essere in origin-form: comincia con '/' (AGENTS.md §3/§5) */
    if (target[0] != '/') {
        set_err(err_detail, err_detail_len,
                "target della richiesta non valido (atteso un path assoluto)");
        return -1;
    }
    if (target_len >= (size_t) (HTTP_MAX_PATH_LEN + HTTP_MAX_QUERY_LEN)) {
        set_err(err_detail, err_detail_len, "target della richiesta troppo lungo");
        return -1;
    }

    qmark = memchr(target, '?', target_len);
    if (qmark != NULL) {
        size_t path_len = (size_t) (qmark - target);
        size_t query_len = target_len - path_len - 1;
        copy_bounded(req->path, sizeof(req->path), target, path_len);
        copy_bounded(req->query, sizeof(req->query), qmark + 1, query_len);
    } else {
        copy_bounded(req->path, sizeof(req->path), target, target_len);
        req->query[0] = '\0';
    }

    /* Versione: solo HTTP/1.0 e HTTP/1.1 sono accettate (AGENTS.md §4) */
    if (version_len == 8 && strncmp(version, "HTTP/1.1", 8) == 0) {
        req->version = HTTP_VERSION_1_1;
    } else if (version_len == 8 && strncmp(version, "HTTP/1.0", 8) == 0) {
        req->version = HTTP_VERSION_1_0;
    } else {
        set_err(err_detail, err_detail_len, "versione HTTP non riconosciuta");
        return -1;
    }

    /* Metodo supportato? (AGENTS.md §4: solo GET/HEAD; altri -> 501) */
    if (strcmp(req->method_str, "GET") == 0) {
        req->method = HTTP_METHOD_GET;
    } else if (strcmp(req->method_str, "HEAD") == 0) {
        req->method = HTTP_METHOD_HEAD;
    } else {
        req->method = HTTP_METHOD_UNSUPPORTED;
    }

    return 0;
}

static int parse_header_line(const char *line, size_t line_len,
                              struct http_request *req,
                              char *err_detail, size_t err_detail_len)
{
    const char *colon;
    const char *name_start;
    const char *value_start;
    const char *value_end;
    size_t name_len;
    size_t value_len;

    colon = memchr(line, ':', line_len);
    if (colon == NULL) {
        set_err(err_detail, err_detail_len, "riga di header malformata (manca ':')");
        return -1;
    }

    name_start = line;
    name_len = (size_t) (colon - line);
    if (name_len == 0) {
        set_err(err_detail, err_detail_len, "nome di header vuoto");
        return -1;
    }

    value_start = colon + 1;
    value_end = line + line_len;
    while (value_start < value_end && *value_start == ' ') {
        value_start++;
    }
    while (value_end > value_start && *(value_end - 1) == ' ') {
        value_end--;
    }
    value_len = (size_t) (value_end - value_start);

    if (req->header_count < HTTP_MAX_HEADERS) {
        struct http_header *h = &req->headers[req->header_count];
        copy_bounded(h->name, sizeof(h->name), name_start, name_len);
        copy_bounded(h->value, sizeof(h->value), value_start, value_len);
        req->header_count++;
    }
    /* Oltre HTTP_MAX_HEADERS gli header eccedenti vengono ignorati
       silenziosamente: non è considerato un errore fatale. */

    return 0;
}

enum http_parse_result http_parse_request(const char *raw, size_t len,
                                           struct http_request *req,
                                           char *err_detail,
                                           size_t err_detail_len)
{
    const char *p = raw;
    const char *end = raw + len;
    const char *next;
    size_t line_len;

    memset(req, 0, sizeof(*req));

    /* Request-line */
    next = find_line_end(p, end, &line_len);
    if (next == NULL) {
        set_err(err_detail, err_detail_len, "richiesta incompleta (manca la request-line)");
        return HTTP_PARSE_BAD_REQUEST;
    }
    if (parse_request_line(p, line_len, req, err_detail, err_detail_len) != 0) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    p = next;

    /* Header, fino alla riga vuota che segna la fine della sezione header */
    for (;;) {
        next = find_line_end(p, end, &line_len);
        if (next == NULL) {
            set_err(err_detail, err_detail_len, "richiesta incompleta (header non terminati)");
            return HTTP_PARSE_BAD_REQUEST;
        }
        if (line_len == 0) {
            break; /* riga vuota: fine degli header */
        }
        if (parse_header_line(p, line_len, req, err_detail, err_detail_len) != 0) {
            return HTTP_PARSE_BAD_REQUEST;
        }
        p = next;
    }

    return HTTP_PARSE_OK;
}

const char *http_get_header(const struct http_request *req, const char *name)
{
    size_t i;

    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}
