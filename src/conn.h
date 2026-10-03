#ifndef CHTTPD_CONN_H
#define CHTTPD_CONN_H

#include <netinet/in.h>

/*
 * conn.h
 *
 * Gestione di una singola connessione già accettata: lettura della
 * richiesta, parsing HTTP (Fase 3), risoluzione del path (Fase 4),
 * generazione della risposta (Fase 5) e logging (Fase 6). Da quando è
 * stata introdotta la concorrenza tramite pthread (Fase 7), ogni
 * chiamata a handle_connection() viene eseguita in un thread proprio,
 * generato dal ciclo accept() in main.c: la funzione stessa non è
 * cambiata, ma va considerata rientrante (nessuno stato condiviso
 * mutabile tra chiamate concorrenti — verificato anche nei moduli che
 * usa, vedi note in AGENTS.md).
 */

/*
 * handle_connection
 *
 * Legge la richiesta dal socket client, esegue il parsing HTTP,
 * risolve il path rispetto alla directory radice e invia la risposta
 * completa. Registra sempre una riga di log in formato Common Log
 * Format (AGENTS.md §6); se 'verbose' è non-zero, stampa anche un
 * dump diagnostico dettagliato (metodo, path, header, esito della
 * risoluzione) — la modalità usata durante lo sviluppo delle Fasi
 * 3-5, ora disponibile tramite il flag -v/--verbose-log. Chiude
 * sempre il socket client prima di ritornare.
 *
 * client_fd      : file descriptor del socket client accettato
 * client_addr    : indirizzo del client (per log e diagnostica)
 * root_dir       : directory radice servita, già canonicalizzata (realpath)
 * verbose        : se non-zero, stampa anche il dump diagnostico dettagliato
 * enable_listing : se non-zero (flag -l/--list-dir, v1.1), le directory
 *                  senza index.html producono un listing HTML invece di 403
 */
void handle_connection(int client_fd, const struct sockaddr_in *client_addr,
                        const char *root_dir, int verbose, int enable_listing);

#endif /* CHTTPD_CONN_H */
