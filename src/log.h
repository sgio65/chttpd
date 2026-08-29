#ifndef CHTTPD_LOG_H
#define CHTTPD_LOG_H

/*
 * log.h
 *
 * Fase 6: logging delle richieste gestite, in formato Common Log
 * Format semplificato (vedi AGENTS.md §6), su stdout. Attivo sempre,
 * indipendentemente dal flag -v/--verbose-log (che controlla solo il
 * dump diagnostico dettagliato aggiuntivo in conn.c).
 */

/*
 * log_request
 *
 * Stampa una riga di log per la richiesta gestita, nel formato:
 *   <ip> - - [<timestamp>] "<request-line>" <status> <bytes>
 *
 * ip           : indirizzo IP del client
 * request_line : request-line così come ricevuta dal client (raw,
 *                senza terminatore di riga); se vuota o NULL viene
 *                stampato "-"
 * status       : codice di stato HTTP restituito
 * bytes        : numero di byte del corpo effettivamente inviati
 *                (0 per risposte HEAD o senza corpo)
 */
void log_request(const char *ip, const char *request_line, int status, long bytes);

#endif /* CHTTPD_LOG_H */
