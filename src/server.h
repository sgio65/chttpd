#ifndef CHTTPD_SERVER_H
#define CHTTPD_SERVER_H

/*
 * server.h
 *
 * Fase 1: apertura, bind e listen del socket TCP di ascolto.
 * Nessuna gestione delle richieste in questa fase (vedi Fase 2).
 */

/*
 * server_listen
 *
 * Crea un socket TCP, esegue bind sull'indirizzo/porta indicati e
 * mette il socket in ascolto (listen).
 *
 * bind_addr : indirizzo IPv4 in formato dotted-decimal (es. "0.0.0.0")
 * port      : porta TCP (1-65535)
 * backlog   : dimensione della coda di connessioni in attesa
 *
 * Ritorna il file descriptor del socket in ascolto in caso di successo,
 * oppure -1 in caso di errore (un messaggio diagnostico viene stampato
 * su stderr tramite perror/fprintf).
 */
int server_listen(const char *bind_addr, int port, int backlog);

/*
 * server_close
 *
 * Chiude in modo pulito il socket in ascolto.
 */
void server_close(int fd);

#endif /* CHTTPD_SERVER_H */
