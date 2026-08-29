#ifndef CHTTPD_HTTP_RESPONSE_H
#define CHTTPD_HTTP_RESPONSE_H

#include "http_request.h"

/*
 * http_response.h
 *
 * Fase 5: generazione della risposta HTTP completa (status-line,
 * header Server/Date/Content-Type/Content-Length/Connection, corpo).
 * Sostituisce le funzioni provvisorie send_status_line()/
 * send_redirect() introdotte in conn.c durante le Fasi 3-4.
 */

/*
 * http_send_file
 *
 * Invia una risposta 200 OK con il contenuto del file indicato da
 * 'resolved_path' (già risolto e validato da fsmap.c: percorso
 * assoluto, contenuto nella radice, leggibile al momento della
 * validazione). Il Content-Type viene dedotto dall'estensione del
 * file (tabella MIME minima, AGENTS.md §8). Se 'method' è
 * HTTP_METHOD_HEAD, vengono inviati solo gli header, nessun corpo.
 *
 * Ritorna il numero di byte del corpo effettivamente inviati (0 per
 * HEAD, o se il client si disconnette prima che l'invio inizi/
 * completi: non è un errore fatale, vedi AGENTS.md §6 per come questo
 * valore viene usato nel log), utile al chiamante per il logging.
 * Ritorna -1 solo se l'apertura/lettura del file fallisce (tipicamente
 * una race TOCTOU: il file esisteva al momento della validazione in
 * fsmap.c ma non è più accessibile); il chiamante deve in tal caso
 * inviare una risposta 500.
 */
long http_send_file(int fd, enum http_method method, const char *resolved_path);

/*
 * http_send_error
 *
 * Invia una risposta di errore completa: status-line, header
 * essenziali (incluso Location se status_code è 301) e un piccolo
 * corpo HTML esplicativo (omesso per le richieste HEAD).
 *
 * status_code : codice numerico (301, 400, 403, 404, 501, 500, ...)
 * reason      : reason-phrase da usare nella status-line e nel corpo
 * extra       : per status_code == 301, l'URL di destinazione da
 *               riportare nell'header Location; ignorato altrimenti
 *               (può essere NULL)
 *
 * Ritorna il numero di byte del corpo HTML effettivamente inviati (0
 * per HEAD, o se l'header non è stato inviato con successo), utile al
 * chiamante per il logging.
 */
long http_send_error(int fd, enum http_method method, int status_code,
                      const char *reason, const char *extra);

#endif /* CHTTPD_HTTP_RESPONSE_H */
