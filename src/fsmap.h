#ifndef CHTTPD_FSMAP_H
#define CHTTPD_FSMAP_H

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
 * fsmap.h
 *
 * Fase 4: risoluzione del path richiesto dal client rispetto alla
 * directory radice servita, con protezione da path traversal — sia
 * lessicale (sequenze "..") sia tramite symlink che punterebbero
 * fuori dalla radice — e gestione minima delle directory (ricerca di
 * index.html). Nessun invio del contenuto del file: la generazione
 * della risposta completa (Content-Type, corpo, ecc.) è demandata
 * alla Fase 5.
 */

enum fsmap_result {
    FSMAP_OK,            /* file regolare trovato e leggibile: resolved_path valido */
    FSMAP_REDIRECT,      /* directory richiesta senza '/' finale: redirect_location valido */
    FSMAP_NOT_FOUND,     /* risorsa inesistente */
    FSMAP_FORBIDDEN,     /* permessi insufficienti, path traversal o escape via symlink */
    FSMAP_BAD_REQUEST,   /* percent-encoding malformato nel path richiesto */
    FSMAP_DIR_NO_INDEX,  /* directory senza index.html, listing disattivo (default) -> 403 */
    FSMAP_DIR_LISTING    /* directory senza index.html, listing attivo (v1.1, -l/--list-dir):
                             resolved_path valido, punta alla directory stessa */
};

struct fsmap_lookup {
    enum fsmap_result result;
    char resolved_path[PATH_MAX];      /* valido se result == FSMAP_OK */
    char redirect_location[PATH_MAX];  /* valido se result == FSMAP_REDIRECT */
};

/*
 * fsmap_resolve
 *
 * root_dir       : directory radice servita, GIA' canonicalizzata
 *                  (percorso assoluto risolto tramite realpath da
 *                  main.c all'avvio)
 * url_path       : path grezzo (non decodificato) così come estratto
 *                  dalla request-line (campo path di struct http_request)
 * enable_listing : se non-zero (flag -l/--list-dir, v1.1), una
 *                  directory senza index.html produce FSMAP_DIR_LISTING
 *                  invece di FSMAP_DIR_NO_INDEX
 * out            : popolato con l'esito della risoluzione
 */
void fsmap_resolve(const char *root_dir, const char *url_path,
                    int enable_listing, struct fsmap_lookup *out);

#endif /* CHTTPD_FSMAP_H */
