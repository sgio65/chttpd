#ifndef CHTTPD_DIRLIST_H
#define CHTTPD_DIRLIST_H

#include "http_request.h"

/*
 * dirlist.h
 *
 * v1.1: generazione e invio del listing HTML di una directory, usato
 * quando non è presente index.html e il flag -l/--list-dir è attivo
 * (altrimenti si ottiene sempre 403, comportamento di default
 * invariato rispetto alle Fasi 1-8).
 */

/*
 * dirlist_send
 *
 * Genera il listing HTML della directory 'dir_path' (path filesystem
 * assoluto, già risolto e validato da fsmap.c: contenuto nella radice,
 * accessibile al momento della validazione) e lo invia come risposta
 * 200 OK. 'url_path' è il path così come richiesto dal client (con
 * '/' finale, dato che fsmap.c instrada qui solo le richieste già in
 * forma di directory): viene usato per il titolo della pagina
 * (HTML-escaped) e per decidere se mostrare il collegamento alla
 * directory superiore, nascosto quando 'url_path' è "/" (radice del
 * sito servito). Se 'method' è HTTP_METHOD_HEAD, invia solo gli
 * header.
 *
 * Le voci della directory sono ordinate con le sotto-directory prima
 * dei file, entrambe in ordine alfabetico case-insensitive. Le voci
 * il cui stato non è leggibile (es. symlink rotti) vengono omesse dal
 * listing. Nomi e path vengono rispettivamente HTML-escaped e
 * percent-encoded prima dell'inserimento nella pagina, per sicurezza
 * (protezione da XSS riflesso tramite nomi di file "maliziosi") e
 * correttezza (nomi con spazi o Unicode).
 *
 * Ritorna il numero di byte del corpo effettivamente inviati (0 per
 * HEAD), oppure -1 se la lettura della directory fallisce (il
 * chiamante deve in tal caso inviare una risposta 500: race TOCTOU
 * tra la validazione in fsmap.c e questa chiamata, analoga a quella
 * gestita da http_send_file()).
 */
long dirlist_send(int fd, enum http_method method, const char *dir_path,
                   const char *url_path);

#endif /* CHTTPD_DIRLIST_H */
