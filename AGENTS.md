# AGENTS.md — Specifiche di progetto: `chttpd`

Questo documento definisce le regole, l'interfaccia e i vincoli che ogni fase
di sviluppo del progetto deve rispettare. È il riferimento vincolante per
qualunque agente (umano o automatico) che scriva codice per questo progetto.
Ogni fase successiva deve essere conforme a quanto qui descritto, salvo
modifiche esplicitamente approvate e verbalizzate in questo file.

**Versione corrente: 1.1** (vedi §13 per lo stato di avanzamento delle fasi,
§14 per la checklist di collaudo del rilascio 1.0, §15 per il directory
listing introdotto nella 1.1).

## 1. Obiettivo del progetto

Implementare un server HTTP/1.0-1.1 minimale per la sola distribuzione di
file statici, funzionalmente equivalente (per il sottoinsieme di feature
scelto) al modulo Python `http.server` invocato come:

```
python3 -m http.server [porta] [--bind INDIRIZZO] [--directory DIR]
```

Nome del binario: **`chttpd`**

## 2. Vincoli tecnici

- Linguaggio: **ANSI C (C89/C90)**. Nessuna estensione GNU, nessun C99/C11.
- Librerie ammesse:
  - Libreria standard C (`stdio.h`, `stdlib.h`, `string.h`, `errno.h`,
    `signal.h`, `time.h`, `ctype.h`, ecc.)
  - Chiamate **POSIX** (`unistd.h`, `sys/socket.h`, `netinet/in.h`,
    `arpa/inet.h`, `sys/stat.h`, `dirent.h`, `fcntl.h`, `netdb.h`,
    `pthread.h` dalla Fase 7)
- Compilazione target:
  `cc -ansi -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -pthread -o chttpd *.c`
  (la macro `_XOPEN_SOURCE=700`, aggiunta in Fase 4, è necessaria per
  esporre il prototipo di `realpath()` in modalità `-ansi`; `-pthread`,
  aggiunta in Fase 7, abilita l'uso di POSIX Threads)
- Nessuna dipendenza esterna, nessuna libreria di terze parti.
- Nessun uso di funzioni deprecate/non sicure senza giustificazione
  (es. preferire `snprintf` a `sprintf`, `read`/`write` a basso livello per
  i socket).
- Il codice deve compilare senza warning con i flag sopra indicati.

**Portabilità Windows (MSYS2):** il progetto compila ed esegue anche in
ambiente Windows 11 tramite **MSYS2, usando l'ambiente "MSYS"** (terminale
"MSYS2 MSYS", non "MinGW64"/"UCRT64"): quest'ultimo espone le API Windows
native (Winsock2) e NON fornisce gli header BSD sockets (`arpa/inet.h`,
`netinet/in.h`, `sys/socket.h`) né `realpath()`, che il progetto usa fin
dalla Fase 1/4. L'ambiente MSYS fornisce invece un livello di
compatibilità POSIX (in stile Cygwin) che li rende disponibili. Pacchetti
necessari: `pacman -S gcc make` (dal terminale MSYS). Compilazione
verificata funzionante con lo stesso `Makefile`, senza modifiche al
codice. Il binario risultante dipende da `msys-2.0.dll` (va distribuita
insieme, o l'ambiente MSYS2 deve essere installato sulla macchina di
destinazione): non è un eseguibile Windows nativo standalone. Un porting
nativo per MinGW64/UCRT64 (Winsock2, senza dipendenza da `msys-2.0.dll`)
non è stato implementato: richiederebbe sostituire le chiamate BSD
sockets con le equivalenti Winsock2 (`WSAStartup`/`WSACleanup`,
`closesocket()` al posto di `close()`, tipo `SOCKET` al posto di `int`)
ed è fuori dall'ambito attuale del progetto.

## 3. Interfaccia a riga di comando

```
chttpd [porta] [-b INDIRIZZO | --bind INDIRIZZO] [-d DIR | --directory DIR] [-h | --help]
```

| Argomento              | Obbligatorio | Default     | Descrizione                                             |
|------------------------|:------------:|-------------|----------------------------------------------------------|
| `porta` (posizionale)  | No           | `8000`      | Porta TCP su cui ascoltare                                |
| `-b`, `--bind`         | No           | `0.0.0.0`   | Indirizzo IP locale su cui fare bind                       |
| `-d`, `--directory`    | No           | `.` (cwd)   | Directory radice servita staticamente                      |
| `-v`, `--verbose-log`  | No           | disattivo   | Stampa anche un dump diagnostico dettagliato per richiesta   |
| `-l`, `--list-dir`     | No           | disattivo   | Mostra il listing HTML delle directory senza `index.html` (v1.1), invece di 403 |
| `-h`, `--help`         | No           | —           | Stampa help e termina con exit code 0                       |
| `--version`            | No           | —           | Stampa la versione (Fase 8) e termina con exit code 0         |

Regole di parsing:

- Gli argomenti opzionali possono precedere o seguire l'argomento posizionale
  (come in `http.server`).
- Porta non numerica o fuori range (1–65535) → errore su stderr, exit code 2.
- Directory indicata con `-d` che non esiste o non è una directory → errore
  su stderr, exit code 2.
- Argomento sconosciuto → messaggio di usage su stderr, exit code 2.

Messaggio all'avvio (su stdout), analogo a `http.server`:

```
Serving HTTP on <bind> port <porta> (http://<bind>:<porta>/) ...
```

## 4. Metodi HTTP supportati

| Metodo  | Comportamento                                                |
|---------|----------------------------------------------------------------|
| `GET`   | Supportato: restituisce header + corpo del file                 |
| `HEAD`  | Supportato: restituisce solo gli header, nessun corpo            |
| `POST`  | Rifiutato: `501 Not Implemented`                                 |
| altri   | Rifiutato: `501 Not Implemented`                                 |

Versioni HTTP accettate nella request-line: `HTTP/1.0`, `HTTP/1.1`.
Versione non riconosciuta o mancante → `400 Bad Request`.

## 5. Codici di stato previsti (fasi successive)

| Codice | Significato          | Quando                                           |
|--------|-----------------------|---------------------------------------------------|
| 200    | OK                    | File trovato e leggibile, o listing di una directory (v1.1)  |
| 301    | Moved Permanently     | Richiesta directory senza `/` finale                |
| 400    | Bad Request           | Request-line malformata o versione non valida        |
| 403    | Forbidden             | Permessi insufficienti, path traversal, oppure directory senza `index.html` con listing disattivo (default) |
| 404    | Not Found             | Risorsa inesistente                                  |
| 405    | Method Not Allowed    | Riservato per usi futuri (attualmente si usa 501)      |
| 501    | Not Implemented       | Metodo non supportato                                |
| 500    | Internal Server Error | Errore interno imprevisto                            |
| 408    | Request Timeout       | Timeout di lettura scaduto con richiesta incompleta (Fase 8) |
| 503    | Service Unavailable   | Limite di connessioni concorrenti raggiunto (Fase 8)   |

Ogni risposta di errore include un piccolo corpo HTML esplicativo, come in
`http.server`.

## 6. Formato di logging

Ogni richiesta gestita produce **sempre** una riga di log su **stdout**,
in formato Common Log Format semplificato (come `http.server`), a
prescindere dal flag `-v`/`--verbose-log`:

```
<ip_client> - - [<timestamp dd/Mon/yyyy:HH:MM:SS +0000>] "<request-line>" <status> <bytes_inviati>
```

Esempio:

```
127.0.0.1 - - [26/Aug/2026:10:15:32 +0000] "GET /index.html HTTP/1.1" 200 512
```

Note implementative:
- Il timestamp è sempre in **UTC** (`+0000` fisso), non nel fuso orario
  locale: lo specificatore `%z` di `strftime()` per il fuso locale non
  è standard ISO C90 e genera un warning con `-ansi -pedantic`.
- La `request-line` loggata è quella **grezza**, così come ricevuta dal
  client (compresa quando la richiesta è malformata o incompleta), non
  una ricostruzione a partire dai campi analizzati.
- `bytes_inviati` è il numero di byte del **corpo** effettivamente
  scritti sul socket (0 per le richieste `HEAD` o se il client si
  disconnette prima/durante l'invio), non la dimensione totale della
  risposta header inclusi.

Se il flag `-v`/`--verbose-log` è attivo, per ogni richiesta viene
stampato **in aggiunta** (prima della riga CLF) un dump diagnostico
dettagliato — metodo, path, query, versione, tutti gli header, ed esito
della risoluzione/risposta — pensato per scopi didattici e di debug.
Questa modalità corrisponde al log verboso usato durante lo sviluppo
delle Fasi 3-5 ed è disattiva di default.

Gli errori di sistema (fallimenti di `bind`, `accept`, ecc.) vengono
stampati su **stderr** con `perror`/messaggio esplicativo e causano, se
irreversibili, la terminazione del processo con exit code diverso da 0.

## 7. Sicurezza filesystem (dettaglio in Fase 4)

- Nessun path risolto può uscire dalla directory radice (`--directory`),
  verificato sia lessicalmente (normalizzazione di `.`/`..`) sia tramite
  `realpath()` (protegge anche da escape realizzati con symlink).
- Sequenze `..` nel path URL vengono normalizzate/rifiutate.
- Il path URL viene decodificato da percent-encoding prima della
  risoluzione.
- Se il path risolto è una directory:
  - se contiene `index.html`, questo viene servito (priorità sempre
    massima, anche con `-l`/`--list-dir` attivo);
  - altrimenti, se il flag `-l`/`--list-dir` è attivo (v1.1, disattivo
    di default), viene generato un listing HTML della directory (vedi
    §15); altrimenti `403 Forbidden`.

## 8. Tabella MIME minima (dettaglio in Fase 5)

| Estensione        | Content-Type               |
|--------------------|------------------------------|
| `.html`, `.htm`     | `text/html`                   |
| `.txt`              | `text/plain`                  |
| `.css`              | `text/css`                    |
| `.js`               | `application/javascript`      |
| `.json`             | `application/json`            |
| `.png`              | `image/png`                   |
| `.jpg`, `.jpeg`      | `image/jpeg`                  |
| `.gif`              | `image/gif`                   |
| `.svg`              | `image/svg+xml`               |
| `.pdf`              | `application/pdf`             |
| sconosciuto         | `application/octet-stream`    |

## 9. Concorrenza

**Decisione (Fase 7): thread POSIX (pthread), un thread "detached" per
connessione.** Il ciclo `accept()` in `main.c` non gestisce più le
richieste direttamente: per ogni connessione accettata alloca un
piccolo task sull'heap (`struct conn_task`, con file descriptor,
indirizzo del client, directory radice e flag verbose) e lo passa a
`pthread_create()`; il thread worker esegue `handle_connection()`
(invariata rispetto alle Fasi 2-6), poi libera il task e decrementa un
contatore di connessioni attive. I thread sono creati "detached"
(`pthread_detach()`): non vengono mai `join()`-ati esplicitamente, dato
che il loro numero nel tempo non è limitato e la loro durata di vita è
breve (una singola richiesta HTTP/1.x con `Connection: close`).

**Limite di connessioni concorrenti (Fase 8):** `MAX_CONCURRENT_CONNECTIONS`
(200, costante in `main.c`, non esposta via CLI per non allargare
l'interfaccia di §3). Oltre questa soglia, il ciclo `accept()` rifiuta
subito la nuova connessione con **503 Service Unavailable** (loggato,
request-line "-" dato che la richiesta non viene nemmeno letta) invece
di generare un thread aggiuntivo, proteggendo da esaurimento delle
risorse sotto carico eccessivo o in stile "slowloris" (molte
connessioni aperte e mai completate). Il controllo, effettuato solo dal
thread principale nel ciclo `accept()`, non necessita di sincronizzazione
aggiuntiva oltre al mutex già usato per il contatore. Non è comunque
implementato un pool di thread riutilizzabili: ogni connessione accettata
sotto soglia crea comunque un nuovo thread (scelta adeguata al carico
atteso di un server didattico/dimostrativo).

Gli errori di `accept()` dovuti a esaurimento di risorse di sistema
(`EMFILE`, `ENFILE`, `ENOBUFS`, `ENOMEM`) comportano una breve pausa
(100 ms, `nanosleep()`) prima di ritentare, per evitare un busy-loop al
100% di una CPU finché la situazione persiste (Fase 8).

## 10. Gestione dei segnali

- `SIGINT`/`SIGTERM`: il thread principale smette di accettare nuove
  connessioni (uscita dal ciclo `accept()`), poi **attende che tutte le
  connessioni già in corso completino** (contatore protetto da mutex +
  condition variable, incrementato prima di ogni `pthread_create()` e
  decrementato al termine di ogni thread worker) prima di chiudere il
  socket di ascolto e uscire con exit code 0. Dalla Fase 8 questa attesa
  è **a tempo limitato** (`SHUTDOWN_DRAIN_TIMEOUT_SECONDS`, 35s,
  costante in `main.c`): se scade con connessioni ancora attive, il
  processo stampa un avviso su stderr ed esce comunque (uscita forzata),
  invece di restare bloccato indefinitamente. In pratica, grazie ai
  timeout di lettura/scrittura sui socket (§9 e sotto), ogni connessione
  termina comunque entro pochi secondi anche nel caso peggiore, per cui
  il timeout di drenaggio è principalmente una rete di sicurezza.
- **Timeout di lettura/scrittura sui socket (Fase 8):** ogni socket
  client accettato riceve `SO_RCVTIMEO`/`SO_SNDTIMEO`
  (`REQUEST_READ_TIMEOUT_SECONDS`/`RESPONSE_WRITE_TIMEOUT_SECONDS`, 30s
  ciascuno, costanti in `main.c`), impostati subito dopo `accept()`
  prima di passare la connessione al thread worker. Se il timeout di
  lettura scade mentre si attende ancora la richiesta, `conn.c` risponde
  con **408 Request Timeout** se erano già arrivati dei dati (richiesta
  iniziata ma incompleta), oppure chiude silenziosamente la connessione
  se non era arrivato nulla (nessun log, coerentemente con il
  comportamento già esistente per le connessioni chiuse senza dati).
  Questo impedisce a un client lento o inattivo di occupare un thread
  indefinitamente.
- Poiché più thread sono coinvolti, `SIGINT`/`SIGTERM` vengono
  temporaneamente bloccati sul thread principale attorno a ogni
  `pthread_create()`: il nuovo thread worker eredita la maschera
  bloccata (e la ri-blocca esplicitamente per sicurezza), mentre il
  thread principale la ripristina subito dopo. In questo modo tali
  segnali sono sempre recapitati al solo thread principale, garantendo
  che interrompano in modo affidabile la sua `accept()` bloccante;
  senza questa precauzione, un segnale process-wide potrebbe essere
  recapitato a un thread worker qualsiasi anziché al principale,
  lasciando la `accept()` bloccata indefinitamente.
- `SIGPIPE`: ignorato a livello di processo (la disposizione di un
  segnale è condivisa da tutti i thread), necessario perché più thread
  possono scrivere concorrentemente su socket già chiusi dal client.

## 11. Struttura dei file sorgente (indicativa, da confermare in Fase 1)

```
chttpd/
├── AGENTS.md          (questo file)
├── README.md          (guida rapida per l'utente/lettore)
├── src/
│   ├── main.c         (parsing CLI, avvio server, accept loop)
│   ├── server.c/.h    (socket, bind, listen)
│   ├── conn.c/.h      (gestione connessione: lettura buffer, orchestrazione)
│   ├── http_request.c/.h   (parsing request-line e header — Fase 3, completato)
│   ├── http_response.c/.h  (costruzione risposta — Fase 5, completato)
│   ├── fsmap.c/.h     (risoluzione path, sicurezza, MIME — Fase 4, completato)
│   ├── log.c/.h       (formattazione log — Fase 6, completato)
│   ├── dirlist.c/.h   (listing HTML delle directory — v1.1)
│   └── version.h      (stringa di versione condivisa — Fase 8)
└── Makefile
```

**Nota implementativa (Fase 2):** è stato introdotto il modulo `conn.c/.h`,
non previsto nell'elenco iniziale, con la responsabilità di leggere la
richiesta grezza dal socket client in un buffer e orchestrare le fasi
successive di elaborazione.

**Nota implementativa (Fase 3):** `http_request.c/.h` esegue il parsing
strutturato di request-line e header a partire dal buffer letto da
`conn.c`. Per gli esiti "richiesta malformata" (400) e "metodo non
supportato" (501), `conn.c` invia già una risposta HTTP minima
(status-line + `Connection: close` + `Content-Length: 0`, nessun corpo)
tramite una funzione `send_status_line()` interna, PROVVISORIA: verrà
sostituita/integrata dal modulo `http_response.c/.h` nella Fase 5, che
gestirà anche i codici 200/301/403/404 con corpo ed invio di file. È
stato inoltre aggiunto `signal(SIGPIPE, SIG_IGN)` in `main.c`, necessario
da questa fase in poi poiché il server scrive sui socket.

**Nota implementativa (Fase 4):** `fsmap.c/.h` risolve `req.path` rispetto
alla directory radice, con: decodifica percent-encoding (rifiuta sequenze
malformate e `%00` → 400), normalizzazione lessicale di `.`/`..` (rifiuta
ogni tentativo di risalire sopra la radice → 403), gestione di
`index.html` per le directory e redirect 301 se manca lo slash finale.
Come ulteriore livello di sicurezza, `main.c` canonicalizza la directory
radice con `realpath()` all'avvio, e `fsmap.c` verifica — sempre con
`realpath()` — che il file risolto sia effettivamente contenuto nella
radice canonica: questo intercetta anche i tentativi di **escape tramite
symlink** che il solo controllo lessicale non potrebbe rilevare (verifica
manuale eseguita: un symlink interno alla radice che punta a un file
esterno viene correttamente rifiutato con 403). Di conseguenza è stata
aggiunta la macro di compilazione `-D_XOPEN_SOURCE=700` (necessaria per
il prototipo di `realpath()` in modalità `-ansi`), riportata anche nel
comando di compilazione in §2.
Per le richieste risolte con successo (`FSMAP_OK`), `conn.c` stampa solo
il path risolto a scopo diagnostico: l'invio effettivo del contenuto del
file (200 OK, header `Content-Type`/`Content-Length`, corpo) resta
demandato alla Fase 5, così come il directory listing (attualmente
`FSMAP_DIR_NO_INDEX` produce sempre 403).

**Nota implementativa (Fase 5):** `http_response.c/.h` sostituisce
integralmente le funzioni provvisorie `send_status_line()`/
`send_redirect()` introdotte in `conn.c` durante le Fasi 3-4 (ora
rimosse). Fornisce due funzioni: `http_send_file()` (risposta 200 OK
con `Content-Type` dedotto dalla tabella MIME di §8, `Content-Length`
esatto e invio del corpo a blocchi da 8 KB tramite `read()`/`write()`;
nessun corpo per `HEAD`) e `http_send_error()` (status-line, header
`Server`/`Date`/`Content-Type`/`Content-Length`/`Connection`, corpo
HTML minimale, header `Location` per il 301; nessun corpo per `HEAD`).
`conn.c` ora invia risposte complete per tutti gli esiti: 200 (file),
301 (redirect), 400, 403, 404, 500 (race TOCTOU tra la validazione in
fsmap.c e l'apertura del file) e 501. Verifiche eseguite: integrità
binaria (confronto MD5) su file piccoli e su un file di 500 KB
(a conferma del corretto funzionamento dell'invio a blocchi),
`Content-Type` corretto per tutte le estensioni della tabella MIME e
per estensioni sconosciute (`application/octet-stream`), corpo HTML
presente su tutti gli errori e assente su `HEAD`, redirect 301
effettivamente seguito da un client fino al 200 finale.

**Nota implementativa (Fase 6):** `log.c/.h` implementa `log_request()`,
chiamata incondizionatamente da `conn.c` per ogni richiesta gestita (non
per le connessioni chiuse dal client senza inviare alcun dato). È stato
introdotto il flag `-v`/`--verbose-log`: quando attivo, `conn.c` stampa
anche il dump diagnostico dettagliato usato come log "di lavoro" durante
le Fasi 3-5, in aggiunta (non in sostituzione) alla riga CLF. `main.c` e
`conn.h` sono stati aggiornati di conseguenza (nuovo campo `verbose` in
`struct config`, nuovo parametro in `handle_connection()`).
`http_response.c/.h` sono stati modificati per ritornare (invece di
`void`/solo esito) il numero di byte del corpo effettivamente inviati,
necessario per popolare correttamente il campo `bytes` del log.
Durante l'implementazione sono emersi due problemi di conformità
`-ansi -pedantic` non rilevabili nelle fasi precedenti: lo specificatore
`%z` di `strftime()` (estensione non-ISO C90, sostituito con un offset
UTC fisso `+0000`) e una stringa letteraria di usage troppo lunga per il
limite minimo garantito da ISO C90 (509 caratteri; risolto suddividendo
il messaggio in due chiamate `fprintf()`). Entrambi corretti e verificati
con compilazione pulita da zero.

**Nota implementativa (Fase 7):** introdotta la concorrenza tramite
thread POSIX (vedi §9/§10 per il design). Modifiche ai file esistenti:
`main.c` (ciclo `accept()` riscritto per generare un thread per
connessione, contatore di connessioni attive con mutex/condition
variable per l'arresto pulito, gestione della maschera dei segnali
attorno a `pthread_create()`), `Makefile` (aggiunta `-pthread`).
Durante l'implementazione sono stati identificati e corretti due
problemi di thread-safety non rilevanti nelle fasi precedenti (dato il
modello sequenziale) ma potenzialmente gravi con la concorrenza:
- in `fsmap.c`, `normalize_path()` usava un array locale dichiarato
  `static` come buffer di lavoro: con più thread concorrenti sarebbe
  stato condiviso tra tutte le chiamate simultanee, causando una race
  condition silenziosa (corruzione di path tra richieste diverse).
  Reso automatico (sullo stack, quindi una copia per thread).
- in `log.c` e `http_response.c`, `gmtime()` (che scrive in un buffer
  statico interno, non rientrante) è stata sostituita con la variante
  rientrante `gmtime_r()`.
Nessun'altra funzione usata dal progetto è risultata non rientrante
(verificati in particolare `inet_ntop()`, `strcasecmp()`, `realpath()`,
`access()`, `stat()`, `perror()`: tutte sicure per l'uso concorrente
fatto qui). Verifiche eseguite: (1) test di concorrenza reale — una
connessione tenuta volutamente "aperta" e mai completata (bloccata in
`read()` in un thread) non ha impedito a una richiesta normale
concorrente di essere servita in ~12 ms, laddove nel modello sequenziale
delle Fasi 1-6 quest'ultima avrebbe dovuto attendere il completamento
della prima; (2) test dell'arresto pulito — inviato `SIGTERM` con una
connessione lenta ancora attiva, il processo ha atteso correttamente
~2.8s (il tempo residuo di vita di quella connessione) prima di
terminare, confermando il corretto funzionamento del drenaggio; (3)
batteria di non-regressione con 10 richieste di tipo diverso (file di
vari MIME type, `HEAD`, 301/400/403/404/501, path traversal, escape via
symlink) lanciate in vera concorrenza: tutti gli esiti coerenti con le
Fasi precedenti.

**Nota implementativa (Fase 8):** colmate le due lacune di robustezza
esplicitamente annotate come "non implementate" al termine della Fase 7
(timeout sui socket, limite di connessioni concorrenti — vedi §9/§10 per
il design), più: flag `--version` (stampa `chttpd 1.0` ed esce), header
`Server:` aggiornato a `chttpd/1.0` (stringa di versione condivisa tra
`main.c` e `http_response.c` tramite il nuovo `version.h`, per evitare
duplicazioni disallineate), `LISTEN_BACKLOG` aumentato da 16 a 128 (più
margine per raffiche di connessioni in ingresso), backoff su errori di
`accept()` da esaurimento risorse. Nuovi codici di stato gestiti: 408
(§5), 503 (§5). Modifiche ai file esistenti: `main.c` (costanti di
robustezza, logica del ciclo `accept()` estesa, drenaggio a tempo
limitato, parsing di `--version`), `conn.c` (distinzione tra timeout di
lettura — `EAGAIN`/`EWOULDBLOCK` — e altri errori/chiusure, risposta 408
quando pertinente), `http_response.c` (uso di `version.h`).
Verifiche eseguite: (1) 408 Request Timeout — testato con una build di
prova a timeout ridotto (2s): una connessione con richiesta iniziata ma
mai completata riceve correttamente 408 con corpo HTML e viene loggata;
una connessione che non invia nulla viene chiusa silenziosamente senza
comparire nel log, coerentemente con il comportamento preesistente; (2)
503 Service Unavailable — testato con una build di prova a soglia
ridotta (3 connessioni): con tutti gli slot occupati da connessioni
lente, una richiesta aggiuntiva riceve 503 immediatamente, e torna a
funzionare normalmente (200) non appena gli slot si liberano; (3)
batteria di non-regressione completa (11 casi: MIME vari, HEAD,
301/400/403/404/501, path traversal, escape via symlink, percent-encoding
malformato) e verifica di integrità binaria (MD5) su un file di 200 KB,
tutti eseguiti in concorrenza con i valori di produzione (non le build di
prova): esiti tutti coerenti con le fasi precedenti; (4) compilazione
pulita da zero del pacchetto completo di consegna, senza warning.

**Nota implementativa (v1.1):** aggiunto il directory listing opzionale
(vedi §15 per il design completo). Nuovo modulo `dirlist.c/.h`. Modifiche
ai file esistenti: `fsmap.h/.c` (nuovo esito `FSMAP_DIR_LISTING`, nuovo
parametro `enable_listing` in `fsmap_resolve()`; una directory
elencabile richiede sia `R_OK` sia `X_OK`, controllati esplicitamente
qui perché il codice comune condiviso con i file regolari verifica solo
`R_OK`), `conn.h/.c` (nuovo parametro `enable_listing` in
`handle_connection()`, gestione del nuovo esito), `http_response.h/.c`
(nuova funzione `http_send_html()`, per inviare contenuto generato
dinamicamente invece che letto da file — riusata da `dirlist.c`, che
quindi non duplica la logica di invio header/corpo/gestione `HEAD` già
presente in `http_response.c`), `main.c` (flag `-l`/`--list-dir`,
propagato tramite `struct conn_task` come già avviene per `verbose`).
Durante l'implementazione è emerso lo stesso problema di conformità
`-ansi -pedantic` già incontrato in Fase 6 (limite di 509 caratteri per
singolo letterale stringa ISO C90), questa volta sul markup HTML/CSS
della pagina: risolto con lo stesso approccio (più chiamate a
`strbuf_append()` con letterali più corti, invece di un unico array
`static const char[]` inizializzato per concatenazione).
Verifiche eseguite: (1) regressione — senza `-l`, una directory priva di
`index.html` restituisce ancora `403` esattamente come prima; con `-l`
attivo, una directory CON `index.html` continua a servire quest'ultimo
(priorità invariata, non genera un listing); (2) contenuto del listing
generato — ordinamento (directory prima, poi file, alfabetico
case-insensitive), formato data ISO 8601 in UTC, dimensioni leggibili,
`Content-Length` coincidente con i byte realmente inviati; (3) sicurezza
— nome di file contenente `& < > " '` correttamente HTML-escaped nel
testo e percent-encoded nell'`href` (nessuna iniezione HTML/XSS
riflessa), nome con spazi e nome Unicode (lettere accentate, simbolo
`€`, ideogrammi, un'emoji) visualizzati e percent-encoded correttamente,
con click-through verificato (GET sul link generato → 200, contenuto
corretto); un symlink presente nella directory e puntante fuori dalla
radice **compare nell'elenco** ma cliccandolo resta `403` (nessuna
regressione sulla protezione anti-escape della Fase 4: il listing si
limita a mostrare i nomi, la richiesta generata cliccando passa
comunque per l'intera pipeline di sicurezza esistente); (4) `HEAD` su un
listing — verificato con una richiesta grezza via `nc` che il socket si
chiude subito dopo la riga vuota degli header, senza alcun byte di
corpo, nonostante il `Content-Length` dichiarato; (5) directory vuota —
pagina valida con solo l'intestazione e il collegamento alla directory
superiore; (6) collegamento alla directory superiore mostrato in una
sottodirectory, nascosto alla radice del sito servito (come da
decisione concordata); (7) generazione del listing sotto concorrenza
(5 richieste parallele) senza errori; (8) compilazione pulita da zero
del pacchetto completo, senza warning.

## 12. Criteri di accettazione per ogni fase

Una fase si considera completata quando:

- Il codice compila senza warning con i flag indicati al punto 2.
- Il comportamento osservato è coerente con quanto specificato in questo
  documento per quella fase.
- Non introduce regressioni nelle fasi precedenti già approvate.
- È stata esplicitamente approvata dall'utente prima di procedere alla
  fase successiva.

## 13. Stato di avanzamento

| Fase | Descrizione                          | Stato        |
|------|----------------------------------------|--------------|
| 0    | Specifiche e interfaccia CLI            | ✅ Completata |
| 1    | Setup socket TCP                        | ✅ Completata |
| 2    | Ciclo accept e connessione singola       | ✅ Completata |
| 3    | Parsing richiesta HTTP                   | ✅ Completata |
| 4    | Risoluzione path e sicurezza filesystem   | ✅ Completata |
| 5    | Generazione risposta HTTP                 | ✅ Completata |
| 6    | Logging                                  | ✅ Completata |
| 7    | Concorrenza                              | ✅ Completata |
| 8    | Robustezza e rifinitura                   | ✅ Completata |

Tutte le fasi pianificate sono completate: il progetto è stato rilasciato
come **1.0** (vedi §14 per la checklist di collaudo) ed esteso con il
**directory listing** nella **1.1** (vedi §15).

Questo file va aggiornato al termine di ogni fase approvata.

## 14. Checklist di collaudo — rilascio 1.0

Checklist eseguita (e superata) prima del rilascio, riproducibile
manualmente con `curl`/`nc`/browser per una verifica indipendente.

**Build**
- [x] `make clean && make` senza warning con i flag di §2
- [x] `./chttpd --version` → `chttpd 1.0`
- [x] `./chttpd --help` → uso ed exit code 0
- [x] Validazione argomenti: porta non numerica/fuori range, directory
      inesistente, opzione sconosciuta → exit code 2 con messaggio chiaro

**Funzionalità base (GET/HEAD, file statici)**
- [x] `GET` su file esistente di alcuni tipi MIME (`.html`, `.json`,
      `.png` o altro binario) → 200, `Content-Type` corretto,
      `Content-Length` esatto, corpo integro (confronto MD5 per i binari)
- [x] `HEAD` → stessi header di `GET`, nessun corpo
- [x] File di dimensioni superiori a `IO_BLOCK_SIZE` (8 KB) → trasferito
      correttamente, integrità MD5 verificata
- [x] Directory con `index.html`, richiesta con `/` finale → servito
      l'index
- [x] Directory senza `/` finale → 301 con `Location` corretta, seguito
      da un client fino al 200 finale
- [x] Directory senza `index.html` → 403

**Errori e casi limite**
- [x] Risorsa inesistente → 404 con corpo HTML
- [x] Metodo non supportato (es. `POST`) → 501 con corpo HTML
- [x] Request-line malformata (via `nc`, es. senza versione HTTP) → 400
- [x] Header senza `:` → 400
- [x] Versione HTTP non riconosciuta → 400
- [x] Percent-encoding malformato (`%zz`) → 400
- [x] Percent-encoding legittimo → risolto correttamente

**Sicurezza filesystem**
- [x] Path traversal lessicale (`../../../etc/passwd`, testato bypassando
      la normalizzazione client-side con `curl --path-as-is` o `nc`) → 403
- [x] Path traversal con percent-encoding (`%2e%2e/...`) → 403
- [x] Escape tramite symlink verso file esterno alla radice → 403

**Logging**
- [x] Modalità default → solo righe CLF su stdout, formato conforme a §6
- [x] Flag `-v`/`--verbose-log` → dump diagnostico aggiuntivo, CLF
      comunque presente
- [x] `bytes` nel log coincide con `Content-Length` effettivo

**Concorrenza (Fase 7)**
- [x] Una connessione volutamente bloccata (mai completata) non impedisce
      a una richiesta concorrente di essere servita in tempi normali
- [x] Più richieste di tipo diverso lanciate in parallelo → tutte con
      l'esito atteso, nessuna regressione

**Robustezza (Fase 8)**
- [x] Connessione con richiesta iniziata ma mai completata, oltre il
      timeout di lettura → 408 con corpo HTML, loggata
- [x] Connessione senza alcun dato inviato, oltre il timeout → chiusa
      silenziosamente, non compare nel log
- [x] Limite di connessioni concorrenti raggiunto → 503 immediato;
      ripristino automatico non appena si liberano slot
- [x] `SIGINT`/`SIGTERM` con connessioni attive → il processo attende il
      drenaggio (entro `SHUTDOWN_DRAIN_TIMEOUT_SECONDS`) prima di uscire
- [x] Header `Server: chttpd/1.1` presente in tutte le risposte

**Portabilità**
- [x] Linux (ambiente di sviluppo primario) — verificato
- [x] macOS — verificato dall'utente
- [x] Windows 11 via MSYS2 (ambiente **MSYS**, non MinGW64/UCRT64) —
      verificato dall'utente, vedi nota in §2

Tutte le voci sopra sono state eseguite e superate nel corso delle Fasi
1-8 (vedi le note implementative di ciascuna fase per il dettaglio dei
test effettuati); questa sezione le riassume come checklist di
riferimento per eventuali collaudi futuri (es. dopo modifiche al codice).

## 15. Directory listing (v1.1)

Funzionalità aggiuntiva rispetto al piano originale delle Fasi 0-8,
richiesta per allineare `chttpd` a una capacità di `http.server` non
ancora coperta: la visualizzazione del contenuto di una directory priva
di `index.html`, invece della sola alternativa `403 Forbidden`.

**Attivazione:** flag `-l`/`--list-dir` (§3), **disattivo di default**
per non cambiare il comportamento pregresso di chi non lo richiede
esplicitamente. `index.html`, se presente, ha sempre la priorità: non
viene mai generato un listing per una directory che lo contiene, anche
con `-l` attivo.

**Formato della pagina:** HTML con CSS incorporato (nessuna richiesta
aggiuntiva al server per fogli di stile esterni), in stile "index of"
con icone per tipo di voce (cartella, immagine, archivio, documento
generico), basato su un mockup fornito dall'utente e riprodotto in
`dirlist.c` tramite il modulo `http_response.c` (funzione
`http_send_html()`, che imposta `Content-Type: text/html; charset=utf-8`
e un `Content-Length` esatto). Colonne: Nome, Ultima Modifica (UTC, in
formato ISO 8601 `AAAA-MM-GG HH:MM` — deliberatamente non nel fuso
orario locale né in una lingua specifica, per evitare dipendenze dalla
locale di sistema, coerentemente con il resto del progetto), Dimensione
(formattata in modo leggibile: B/KB/MB/GB).

**Ordinamento:** sotto-directory prima, poi file, entrambi i gruppi in
ordine alfabetico case-insensitive (scelta del progetto, diversa
dall'ordinamento puramente alfabetico misto di `http.server`).

**Collegamento alla directory superiore (`..`):** mostrato sempre,
tranne quando la directory corrente è la radice del sito servito (path
richiesto `/`), per evitare un collegamento che non condurrebbe a nulla
di utile.

**Sicurezza:**
- I nomi delle voci vengono **HTML-escaped** (`&`, `<`, `>`, `"`, `'`)
  prima dell'inserimento nella pagina, per prevenire un'iniezione
  HTML/XSS riflessa tramite un nome di file "malizioso" creato ad arte
  sul filesystem servito.
- I collegamenti (`href`) vengono **percent-encoded** byte per byte
  (preservando solo lettere, cifre e `- _ . ~`), il che gestisce
  correttamente anche nomi con spazi o caratteri non-ASCII: essendo
  l'escaping a livello di byte, è automaticamente sicuro anche con nomi
  UTF-8 multi-byte (i byte di continuazione UTF-8, nell'intervallo
  0x80-0xBF, non collidono mai con i caratteri ASCII speciali gestiti
  dall'escaping HTML).
- Il listing mostra solo i **nomi** delle voci (letti direttamente dalla
  directory tramite `opendir()`/`readdir()`): cliccare su una voce
  genera comunque una normale richiesta HTTP che attraversa l'intera
  pipeline di sicurezza già esistente (Fase 4). Un symlink presente
  nella directory e puntante fuori dalla radice **compare nell'elenco**
  (il listing non risolve i symlink per decidere cosa mostrare, si
  limita a leggere la voce), ma cliccandolo si ottiene comunque `403`,
  esattamente come senza il listing attivo: nessuna regressione sulla
  protezione anti-escape.
- Una directory elencabile richiede sia `R_OK` (lettura) sia `X_OK`
  (attraversamento, semantica POSIX standard per le directory) — a
  differenza dei file regolari, per cui è sufficiente `R_OK`.
- Le voci il cui `stat()` fallisce (es. symlink rotti che puntano a un
  target inesistente) sono **omesse silenziosamente** dal listing,
  invece di interrompere la generazione della pagina o mostrare
  informazioni incomplete/fuorvianti.

**Limiti noti (non implementati, fuori dall'ambito della v1.1):**
- Nessuna paginazione: una directory con un numero molto elevato di
  voci produce una pagina HTML proporzionalmente grande (il buffer
  interno cresce dinamicamente via `realloc()`, quindi non c'è un limite
  fisso, ma per directory con centinaia di migliaia di voci le
  prestazioni non sono state valutate).
- Nessuna opzione di ordinamento alternativo (per data o dimensione,
  come talvolta offerto da altri server) né di ricerca/filtro.
- Il titolo della pagina (`<h1>`) mostra il path così come richiesto dal
  client (`req.path`, non decodificato da percent-encoding): un URL con
  sequenze `%XX` comparirà quindi codificato nel titolo, anche se la
  richiesta è stata risolta correttamente. Scelta deliberata per
  evitare di esporre un'ulteriore funzione di decodifica tra moduli;
  non incide sulla funzionalità né sulla sicurezza, solo sulla resa
  estetica in casi non comuni.
