/*
 * log.c
 *
 * Fase 6: formattazione e stampa della riga di log per ogni richiesta
 * gestita, in formato Common Log Format semplificato.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"

void log_request(const char *ip, const char *request_line, int status, long bytes)
{
    time_t now;
    struct tm tm_buf;
    struct tm *tm_info;
    char timebuf[32];
    const char *line;

    /*
     * Si usa gmtime_r() (variante rientrante/thread-safe di gmtime(),
     * che scrive nel buffer fornito dal chiamante invece che in un
     * buffer statico interno) invece di gmtime(): necessario da
     * quando più thread possono chiamare log_request() in parallelo
     * (Fase 7). Inoltre si usa sempre UTC: lo specificatore "%z" di
     * strftime() per il fuso orario locale non è standard ISO C90 e
     * genera un warning con "-ansi -pedantic".
     */
    now = time(NULL);
    tm_info = gmtime_r(&now, &tm_buf);
    if (tm_info != NULL) {
        strftime(timebuf, sizeof(timebuf), "%d/%b/%Y:%H:%M:%S +0000", tm_info);
    } else {
        strncpy(timebuf, "01/Jan/1970:00:00:00 +0000", sizeof(timebuf) - 1);
        timebuf[sizeof(timebuf) - 1] = '\0';
    }

    line = (request_line != NULL && request_line[0] != '\0') ? request_line : "-";

    printf("%s - - [%s] \"%s\" %d %ld\n",
           (ip != NULL) ? ip : "-", timebuf, line, status, bytes);
    fflush(stdout);
}
