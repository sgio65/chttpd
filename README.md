# chttpd 1.1

Un semplice server HTTP per file statici, scritto in **ANSI C** con sole
chiamate di libreria standard e POSIX (inclusi POSIX Threads).
Funzionalmente ispirato al modulo Python `http.server`.

Per le specifiche tecniche complete, i vincoli di progetto, il design
delle singole fasi e la checklist di collaudo, vedere
[`AGENTS.md`](./AGENTS.md).

## Caratteristiche

- Metodi `GET` e `HEAD` per file statici, con rilevamento del
  `Content-Type` da una tabella MIME minima
- Directory index (`index.html`), redirect 301 sulle directory senza
  `/` finale
- **Directory listing HTML opzionale** (`-l`/`--list-dir`, v1.1) per le
  directory senza `index.html`, con icone, ordinamento
  directory-poi-file, ed escaping/encoding sicuro dei nomi (vedi
  `AGENTS.md` §15)
- Protezione da path traversal (lessicale, con percent-encoding, e
  tramite symlink)
- Concorrenza tramite thread POSIX (un thread per connessione)
- Timeout di lettura/scrittura sui socket e limite di connessioni
  concorrenti, con arresto pulito (drenaggio) su `SIGINT`/`SIGTERM`
- Logging in formato Common Log Format, con modalità diagnostica
  dettagliata opzionale (`-v`/`--verbose-log`)
- Nessuna dipendenza esterna: solo libreria standard C89 e chiamate POSIX

## Compilazione

```sh
make
```

Richiede un compilatore C con supporto POSIX Threads (`-pthread`).
Testato su Linux e macOS. Su **Windows 11**, compila tramite **MSYS2**
usando l'ambiente **MSYS** (terminale "MSYS2 MSYS", non
"MinGW64"/"UCRT64" — questi ultimi non forniscono gli header POSIX
necessari): `pacman -S gcc make`, poi `make` come sopra. Vedi
`AGENTS.md` §2 per i dettagli.

## Uso

```sh
./chttpd [porta] [-b INDIRIZZO] [-d DIRECTORY] [-v] [-l] [--version] [-h]
```

Esempi:

```sh
# Serve la directory corrente sulla porta 8000
./chttpd

# Serve sulla porta 8080
./chttpd 8080

# Serve una directory specifica, ascoltando solo su localhost
./chttpd 8000 -b 127.0.0.1 -d /var/www/html

# Con listing HTML delle directory senza index.html
./chttpd -l

# Con log diagnostico dettagliato per ogni richiesta
./chttpd -v

# Versione
./chttpd --version
```

Elenco completo delle opzioni: `./chttpd --help`.

## Stato del progetto

Sviluppo completato per fasi incrementali, ciascuna approvata
singolarmente e documentata in `AGENTS.md` (specifiche, note
implementative, verifiche effettuate). Tutte le fasi (0-8) sono
completate — rilascio **1.0** — con il directory listing aggiunto nella
**1.1** (`AGENTS.md` §15). Vedi la checklist di collaudo in
`AGENTS.md` §14.

