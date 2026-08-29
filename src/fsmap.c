/*
 * fsmap.c
 *
 * Fase 4: decodifica percent-encoding, normalizzazione lessicale del
 * path (risoluzione di "." e "..", con rifiuto di ogni tentativo di
 * risalire sopra la radice), risoluzione rispetto alla directory
 * servita, gestione dell'index.html per le directory, e verifica
 * finale tramite realpath() per rifiutare anche gli escape realizzati
 * tramite symlink che puntano fuori dalla radice.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#include "fsmap.h"

#define FSMAP_MAX_COMPONENTS    128
#define FSMAP_MAX_COMPONENT_LEN 256

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/*
 * percent_decode
 *
 * Decodifica le sequenze percent-encoded (%XX) di 'src' in 'dst'
 * (buffer di dimensione 'dst_size'). Rifiuta sequenze incomplete o
 * non esadecimali e il byte NUL decodificato (%00), che potrebbe
 * troncare il path in modo pericoloso.
 *
 * Ritorna 0 in caso di successo, -1 in caso di errore.
 */
static int percent_decode(const char *src, char *dst, size_t dst_size)
{
    size_t si = 0;
    size_t di = 0;
    size_t src_len = strlen(src);

    while (si < src_len) {
        char c = src[si];

        if (c == '%') {
            int hi, lo, value;

            if (si + 2 >= src_len) {
                return -1; /* sequenza incompleta a fine stringa */
            }
            hi = hex_val(src[si + 1]);
            lo = hex_val(src[si + 2]);
            if (hi < 0 || lo < 0) {
                return -1; /* cifre esadecimali non valide */
            }
            value = hi * 16 + lo;
            if (value == 0) {
                return -1; /* %00: NUL decodificato, rifiutato */
            }
            if (di >= dst_size - 1) {
                return -1; /* buffer di destinazione esaurito */
            }
            dst[di++] = (char) value;
            si += 3;
        } else {
            if (di >= dst_size - 1) {
                return -1;
            }
            dst[di++] = c;
            si++;
        }
    }
    dst[di] = '\0';
    return 0;
}

/*
 * normalize_path
 *
 * Risolve lessicalmente i componenti "." e ".." nel path assoluto
 * 'decoded' (deve iniziare con '/'), scrivendo il path normalizzato
 * (relativo, senza '/' iniziale né finale) in 'rel_out'. Se il path
 * richiede di risalire oltre la radice, ritorna -1 (path traversal).
 * Imposta '*is_dir_request' a 1 se il path originale termina con '/'.
 */
static int normalize_path(const char *decoded, char *rel_out,
                           size_t rel_out_size, int *is_dir_request)
{
    /*
     * NOTA (Fase 7): questo buffer NON è 'static'. Con l'introduzione
     * della concorrenza tramite pthread, un array locale statico
     * sarebbe condiviso tra tutti i thread che eseguono questa
     * funzione contemporaneamente, causando una race condition. Come
     * variabile automatica (sullo stack), ogni thread ne ha una copia
     * propria. Dimensione: 128 * 256 = 32 KB, trascurabile rispetto
     * allo stack di default di un thread POSIX.
     */
    char comps[FSMAP_MAX_COMPONENTS][FSMAP_MAX_COMPONENT_LEN];
    int ncomps = 0;
    const char *p = decoded;
    size_t out_len;
    int i;
    size_t decoded_len = strlen(decoded);

    *is_dir_request = (decoded_len > 0 && decoded[decoded_len - 1] == '/');

    if (*p == '/') {
        p++;
    }

    while (*p != '\0') {
        const char *seg_start = p;
        size_t seg_len;

        while (*p != '\0' && *p != '/') {
            p++;
        }
        seg_len = (size_t) (p - seg_start);
        if (*p == '/') {
            p++;
        }

        if (seg_len == 0) {
            continue; /* "//" -> componente vuota, ignorata */
        }
        if (seg_len == 1 && seg_start[0] == '.') {
            continue; /* "." -> componente corrente, ignorata */
        }
        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
            if (ncomps > 0) {
                ncomps--; /* risale di un livello */
            } else {
                return -1; /* tentativo di uscire dalla radice */
            }
            continue;
        }
        if (ncomps >= FSMAP_MAX_COMPONENTS || seg_len >= FSMAP_MAX_COMPONENT_LEN) {
            return -1;
        }
        memcpy(comps[ncomps], seg_start, seg_len);
        comps[ncomps][seg_len] = '\0';
        ncomps++;
    }

    out_len = 0;
    rel_out[0] = '\0';
    for (i = 0; i < ncomps; i++) {
        size_t clen = strlen(comps[i]);

        if (out_len + clen + 2 >= rel_out_size) {
            return -1; /* path risultante troppo lungo */
        }
        if (out_len > 0) {
            rel_out[out_len++] = '/';
        }
        memcpy(rel_out + out_len, comps[i], clen);
        out_len += clen;
        rel_out[out_len] = '\0';
    }

    return 0;
}

/*
 * path_is_within_root
 *
 * Verifica che 'candidate' (una volta risolto con realpath, quindi
 * con eventuali symlink completamente espansi) sia effettivamente
 * contenuto nella directory 'root_canon' (anch'essa canonica). Questo
 * intercetta i tentativi di escape realizzati tramite symlink che il
 * solo controllo lessicale di normalize_path() non può rilevare.
 */
static int path_is_within_root(const char *root_canon, const char *candidate)
{
    char canon[PATH_MAX];
    size_t root_len;

    if (realpath(candidate, canon) == NULL) {
        return 0;
    }
    root_len = strlen(root_canon);
    if (strncmp(canon, root_canon, root_len) != 0) {
        return 0;
    }
    return (canon[root_len] == '\0' || canon[root_len] == '/');
}

void fsmap_resolve(const char *root_dir, const char *url_path,
                    struct fsmap_lookup *out)
{
    char decoded[PATH_MAX];
    char rel_path[PATH_MAX];
    char full_path[PATH_MAX];
    int is_dir_request = 0;
    struct stat st;

    memset(out, 0, sizeof(*out));

    if (percent_decode(url_path, decoded, sizeof(decoded)) != 0) {
        out->result = FSMAP_BAD_REQUEST;
        return;
    }
    if (decoded[0] != '/') {
        out->result = FSMAP_BAD_REQUEST;
        return;
    }
    if (normalize_path(decoded, rel_path, sizeof(rel_path), &is_dir_request) != 0) {
        out->result = FSMAP_FORBIDDEN;
        return;
    }

    if (rel_path[0] == '\0') {
        if (snprintf(full_path, sizeof(full_path), "%s", root_dir)
                >= (int) sizeof(full_path)) {
            out->result = FSMAP_FORBIDDEN;
            return;
        }
    } else {
        if (snprintf(full_path, sizeof(full_path), "%s/%s", root_dir, rel_path)
                >= (int) sizeof(full_path)) {
            out->result = FSMAP_FORBIDDEN;
            return;
        }
    }

    if (stat(full_path, &st) != 0) {
        out->result = FSMAP_NOT_FOUND;
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!is_dir_request) {
            /* Directory richiesta senza '/' finale: redirect 301,
               come in http.server (AGENTS.md §5). */
            if (snprintf(out->redirect_location, sizeof(out->redirect_location),
                         "%s/", url_path) >= (int) sizeof(out->redirect_location)) {
                out->result = FSMAP_FORBIDDEN;
                return;
            }
            out->result = FSMAP_REDIRECT;
            return;
        }
        {
            char index_path[PATH_MAX];
            struct stat idx_st;

            if (snprintf(index_path, sizeof(index_path), "%s/index.html", full_path)
                    >= (int) sizeof(index_path)) {
                out->result = FSMAP_FORBIDDEN;
                return;
            }
            if (stat(index_path, &idx_st) == 0 && S_ISREG(idx_st.st_mode)) {
                strncpy(full_path, index_path, sizeof(full_path) - 1);
                full_path[sizeof(full_path) - 1] = '\0';
            } else {
                out->result = FSMAP_DIR_NO_INDEX;
                return;
            }
        }
    } else if (!S_ISREG(st.st_mode)) {
        /* Non un file regolare (device, socket, fifo, ...): rifiutato. */
        out->result = FSMAP_FORBIDDEN;
        return;
    }

    if (access(full_path, R_OK) != 0) {
        out->result = FSMAP_FORBIDDEN;
        return;
    }

    if (!path_is_within_root(root_dir, full_path)) {
        out->result = FSMAP_FORBIDDEN;
        return;
    }

    strncpy(out->resolved_path, full_path, sizeof(out->resolved_path) - 1);
    out->resolved_path[sizeof(out->resolved_path) - 1] = '\0';
    out->result = FSMAP_OK;
}
