/*
 * main.c
 *
 * chttpd - semplice server HTTP per file statici (ANSI C + POSIX).
 *
 * Parsing della riga di comando (come da AGENTS.md), apertura del
 * socket TCP in ascolto, ciclo accept() con gestione concorrente delle
 * connessioni tramite pthread (Fase 7: un thread per connessione,
 * detached), timeout di lettura/scrittura e limite di connessioni
 * concorrenti (Fase 8), e arresto pulito alla ricezione di
 * SIGINT/SIGTERM con drenaggio (a tempo limitato) delle connessioni
 * ancora attive.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "server.h"
#include "conn.h"
#include "http_response.h"
#include "log.h"
#include "version.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_PORT 8000
#define DEFAULT_BIND "0.0.0.0"
#define DEFAULT_DIR "."
#define LISTEN_BACKLOG 128
#define BIND_ADDR_LEN 64
#define DIR_PATH_LEN 1024

/*
 * Costanti di robustezza introdotte in Fase 8. Non sono esposte come
 * opzioni della riga di comando per non allargare l'interfaccia CLI
 * definita in AGENTS.md §3 (che ricalca intenzionalmente quella di
 * http.server): sono valori di default ragionevoli per un server
 * didattico/dimostrativo, documentati in AGENTS.md §9/§10.
 */

/* Timeout di lettura della richiesta e di scrittura della risposta su
   ogni socket client: oltre questo tempo di inattività, read()/write()
   falliscono con EAGAIN/EWOULDBLOCK invece di bloccarsi indefinitamente
   (protegge da client lenti o inattivi che altrimenti occuperebbero un
   thread per sempre). */
#define REQUEST_READ_TIMEOUT_SECONDS  30
#define RESPONSE_WRITE_TIMEOUT_SECONDS 30

/* Numero massimo di connessioni gestite contemporaneamente da un
   thread: oltre questa soglia le nuove connessioni vengono rifiutate
   subito con 503 Service Unavailable, invece di generare un thread
   aggiuntivo indefinitamente (protegge da esaurimento delle risorse
   sotto carico eccessivo o in stile "slowloris"). */
#define MAX_CONCURRENT_CONNECTIONS 200

/* Tempo massimo di attesa, allo shutdown, per il completamento delle
   connessioni ancora attive prima di forzare comunque l'uscita (con
   un avviso su stderr). Volutamente superiore ai timeout di lettura/
   scrittura sopra, in modo che nella stragrande maggioranza dei casi
   le connessioni si concludano naturalmente entro questo margine. */
#define SHUTDOWN_DRAIN_TIMEOUT_SECONDS 35

struct config {
    int port;
    char bind_addr[BIND_ADDR_LEN];
    char directory[DIR_PATH_LEN];
    int verbose;
    int list_dir;
};

/* Flag impostato dal signal handler; sig_atomic_t per accesso sicuro. */
static volatile sig_atomic_t g_running = 1;

/*
 * Contatore delle connessioni attualmente gestite da un thread,
 * protetto da mutex. Usato per un arresto pulito (Fase 7): quando
 * arriva SIGINT/SIGTERM, il thread principale smette di accettare
 * nuove connessioni ma attende (tramite la condition variable) che
 * tutte quelle già in corso completino, prima di chiudere il socket
 * di ascolto e terminare il processo. Dalla Fase 8 questa attesa è a
 * tempo limitato (SHUTDOWN_DRAIN_TIMEOUT_SECONDS), per evitare che il
 * processo resti bloccato indefinitamente in casi anomali. Lo stesso
 * contatore viene anche letto (sotto lock) per applicare il limite
 * MAX_CONCURRENT_CONNECTIONS nel ciclo accept().
 */
static pthread_mutex_t g_active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_active_cond = PTHREAD_COND_INITIALIZER;
static long g_active_connections = 0;

static void active_connections_inc(void)
{
    pthread_mutex_lock(&g_active_mutex);
    g_active_connections++;
    pthread_mutex_unlock(&g_active_mutex);
}

static void active_connections_dec(void)
{
    pthread_mutex_lock(&g_active_mutex);
    g_active_connections--;
    if (g_active_connections == 0) {
        pthread_cond_signal(&g_active_cond);
    }
    pthread_mutex_unlock(&g_active_mutex);
}

/*
 * active_connections_wait_zero_timeout
 *
 * Attende che il contatore delle connessioni attive torni a zero,
 * fino a un massimo di 'timeout_seconds'. Ritorna 1 se il timeout è
 * scaduto con connessioni ancora attive (drenaggio incompleto), 0 se
 * tutte le connessioni sono terminate regolarmente entro il timeout.
 */
static int active_connections_wait_zero_timeout(int timeout_seconds)
{
    struct timespec deadline;
    int timed_out = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_seconds;

    pthread_mutex_lock(&g_active_mutex);
    while (g_active_connections > 0) {
        int rc = pthread_cond_timedwait(&g_active_cond, &g_active_mutex, &deadline);

        if (rc == ETIMEDOUT) {
            timed_out = (g_active_connections > 0);
            break;
        }
    }
    pthread_mutex_unlock(&g_active_mutex);

    return timed_out;
}

/*
 * conn_task
 *
 * Dati di una connessione accettata, allocati sull'heap (malloc) e
 * passati al thread worker: una connessione accettata nello stack
 * frame del ciclo accept() non può essere passata per indirizzo a un
 * thread che vivrà oltre l'iterazione corrente del ciclo, quindi ogni
 * connessione ha la propria copia indipendente. 'root_dir' punta
 * invece a cfg.directory in main(): è condiviso in sola lettura tra
 * tutti i thread (mai modificato dopo l'avvio), quindi non necessita
 * di essere copiato né protetto.
 */
struct conn_task {
    int client_fd;
    struct sockaddr_in client_addr;
    const char *root_dir;
    int verbose;
    int list_dir;
};

/*
 * conn_thread_main
 *
 * Funzione eseguita dal thread worker per una singola connessione:
 * delega tutto il lavoro a handle_connection() (Fasi 2-6, invariata),
 * poi libera il task e decrementa il contatore delle connessioni
 * attive. SIGINT/SIGTERM vengono bloccati anche qui per sicurezza
 * (il blocco avviene già temporaneamente nel thread principale
 * durante pthread_create(), che il nuovo thread eredita, ma un
 * blocco esplicito qui protegge anche da eventuali comportamenti
 * specifici della piattaforma sull'ereditarietà della maschera).
 */
static void *conn_thread_main(void *arg)
{
    struct conn_task *task = (struct conn_task *) arg;
    sigset_t block_set;

    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &block_set, NULL);

    handle_connection(task->client_fd, &task->client_addr,
                       task->root_dir, task->verbose, task->list_dir);

    free(task);
    active_connections_dec();
    return NULL;
}

static void on_signal(int signum)
{
    (void) signum;
    g_running = 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Uso: %s [porta] [-b|--bind INDIRIZZO] [-d|--directory DIR] "
        "[-v|--verbose-log] [-l|--list-dir] [-h|--help] [--version]\n"
        "\n"
        "  porta                  Porta TCP di ascolto (default: %d)\n"
        "  -b, --bind INDIRIZZO   Indirizzo IP locale su cui fare bind (default: %s)\n"
        "  -d, --directory DIR    Directory radice servita (default: %s)\n",
        prog, DEFAULT_PORT, DEFAULT_BIND, DEFAULT_DIR);
    fprintf(stderr,
        "  -v, --verbose-log      Stampa anche un dump diagnostico dettagliato\n"
        "                         per ogni richiesta (metodo, path, header, esito),\n"
        "                         in aggiunta al log standard\n"
        "  -l, --list-dir         Mostra il listing HTML di una directory priva\n"
        "                         di index.html, invece di rispondere 403\n"
        "                         (default: disattivo)\n"
        "  -h, --help             Mostra questo messaggio ed esce\n"
        "  --version              Mostra la versione ed esce\n");
}

/* Ritorna 1 se 'str' rappresenta un intero decimale valido, 0 altrimenti. */
static int is_decimal_number(const char *str)
{
    size_t i;

    if (str == NULL || str[0] == '\0') {
        return 0;
    }
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return 0;
        }
    }
    return 1;
}

/*
 * Esegue il parsing degli argomenti a riga di comando popolando 'cfg'.
 * In caso di errore stampa un messaggio su stderr e termina il processo
 * con exit code 2 (come specificato in AGENTS.md). Con -h/--help stampa
 * l'usage e termina con exit code 0.
 */
static void parse_args(int argc, char **argv, struct config *cfg)
{
    int i;
    int port_set = 0;
    struct stat st;

    cfg->port = DEFAULT_PORT;
    strncpy(cfg->bind_addr, DEFAULT_BIND, BIND_ADDR_LEN - 1);
    cfg->bind_addr[BIND_ADDR_LEN - 1] = '\0';
    strncpy(cfg->directory, DEFAULT_DIR, DIR_PATH_LEN - 1);
    cfg->directory[DIR_PATH_LEN - 1] = '\0';
    cfg->verbose = 0;
    cfg->list_dir = 0;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(arg, "--version") == 0) {
            printf("chttpd %s\n", CHTTPD_VERSION);
            exit(0);
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose-log") == 0) {
            cfg->verbose = 1;
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list-dir") == 0) {
            cfg->list_dir = 1;
        } else if (strcmp(arg, "-b") == 0 || strcmp(arg, "--bind") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "chttpd: manca il valore per %s\n", arg);
                print_usage(argv[0]);
                exit(2);
            }
            i++;
            strncpy(cfg->bind_addr, argv[i], BIND_ADDR_LEN - 1);
            cfg->bind_addr[BIND_ADDR_LEN - 1] = '\0';
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--directory") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "chttpd: manca il valore per %s\n", arg);
                print_usage(argv[0]);
                exit(2);
            }
            i++;
            strncpy(cfg->directory, argv[i], DIR_PATH_LEN - 1);
            cfg->directory[DIR_PATH_LEN - 1] = '\0';
        } else if (arg[0] == '-' && strcmp(arg, "-") != 0) {
            fprintf(stderr, "chttpd: opzione sconosciuta: %s\n", arg);
            print_usage(argv[0]);
            exit(2);
        } else {
            /* argomento posizionale: porta */
            if (port_set) {
                fprintf(stderr, "chttpd: argomento posizionale duplicato: %s\n", arg);
                print_usage(argv[0]);
                exit(2);
            }
            if (!is_decimal_number(arg)) {
                fprintf(stderr, "chttpd: porta non valida: %s\n", arg);
                exit(2);
            }
            cfg->port = atoi(arg);
            if (cfg->port < 1 || cfg->port > 65535) {
                fprintf(stderr, "chttpd: porta fuori range (1-65535): %s\n", arg);
                exit(2);
            }
            port_set = 1;
        }
    }

    /* Validazione della directory radice */
    if (stat(cfg->directory, &st) != 0) {
        fprintf(stderr, "chttpd: impossibile accedere alla directory '%s': %s\n",
                cfg->directory, strerror(errno));
        exit(2);
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "chttpd: '%s' non è una directory\n", cfg->directory);
        exit(2);
    }

    /*
     * La directory radice viene canonicalizzata (path assoluto, senza
     * '.', '..' o symlink) tramite realpath(). fsmap.c si basa su
     * questo path canonico come riferimento stabile per verificare
     * che ogni risorsa servita sia effettivamente contenuta nella
     * radice (protezione da path traversal e da escape via symlink,
     * vedi Fase 4 / AGENTS.md §7).
     */
    {
        char canon[PATH_MAX];

        if (realpath(cfg->directory, canon) == NULL) {
            fprintf(stderr, "chttpd: impossibile risolvere il percorso di '%s': %s\n",
                    cfg->directory, strerror(errno));
            exit(2);
        }
        strncpy(cfg->directory, canon, DIR_PATH_LEN - 1);
        cfg->directory[DIR_PATH_LEN - 1] = '\0';
    }
}

static void install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /*
     * SIGPIPE va ignorato: da questa fase il server può scrivere
     * risposte (400/501) su socket già chiusi dal client (es. client
     * che si disconnette prima di ricevere la risposta), il che
     * genererebbe altrimenti la terminazione del processo.
     */
    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char **argv)
{
    struct config cfg;
    int listen_fd;

    parse_args(argc, argv, &cfg);
    install_signal_handlers();

    listen_fd = server_listen(cfg.bind_addr, cfg.port, LISTEN_BACKLOG);
    if (listen_fd < 0) {
        fprintf(stderr, "chttpd: avvio del server fallito\n");
        return 1;
    }

    printf("Serving HTTP on %s port %d (http://%s:%d/) directory=%s "
           "verbose-log=%s list-dir=%s concurrency=pthread(max=%d) "
           "timeout=%ds/%ds ...\n",
           cfg.bind_addr, cfg.port, cfg.bind_addr, cfg.port, cfg.directory,
           cfg.verbose ? "on" : "off", cfg.list_dir ? "on" : "off",
           MAX_CONCURRENT_CONNECTIONS,
           REQUEST_READ_TIMEOUT_SECONDS, RESPONSE_WRITE_TIMEOUT_SECONDS);
    fflush(stdout);

    /*
     * Fase 7: ciclo accept() con gestione concorrente delle
     * connessioni tramite pthread (un thread "detached" per
     * connessione). accept() viene interrotta da SIGINT/SIGTERM
     * (EINTR) grazie a sigaction installata senza SA_RESTART sul
     * thread principale; il flag g_running determina se si tratta di
     * una richiesta di arresto oppure di un errore transitorio da
     * ignorare.
     *
     * Attorno a ogni pthread_create() i segnali SIGINT/SIGTERM
     * vengono temporaneamente bloccati sul thread principale: il
     * nuovo thread worker eredita la maschera bloccata (e la
     * ri-blocca esplicitamente in conn_thread_main() per sicurezza),
     * mentre il thread principale la ripristina subito dopo. Questo
     * garantisce che tali segnali vengano sempre e solo recapitati al
     * thread principale, così da interrompere in modo affidabile la
     * accept() bloccante in attesa — con più thread in gioco, un
     * segnale process-wide potrebbe altrimenti essere recapitato a un
     * thread worker qualunque, lasciando la accept() del thread
     * principale bloccata indefinitamente.
     *
     * Fase 8: ogni socket client accettato riceve timeout di lettura/
     * scrittura (SO_RCVTIMEO/SO_SNDTIMEO), il numero di connessioni
     * concorrenti è limitato (oltre soglia si risponde 503 subito), e
     * gli errori di accept() dovuti a esaurimento risorse di sistema
     * comportano una breve pausa per evitare un busy-loop.
     */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd;
        struct conn_task *task;
        pthread_t tid;
        sigset_t block_set;
        sigset_t old_set;
        int rc;
        long current_active;
        struct timeval tv;

        client_fd = accept(listen_fd, (struct sockaddr *) &client_addr,
                            &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue; /* il while verifica g_running */
            }
            perror("accept");
            if (errno == EMFILE || errno == ENFILE ||
                errno == ENOBUFS || errno == ENOMEM) {
                /*
                 * Esaurimento di risorse di sistema (troppi file
                 * descriptor aperti, memoria di rete esaurita, ecc.):
                 * senza una pausa, accept() fallirebbe di nuovo
                 * immediatamente in un ciclo stretto che consumerebbe
                 * il 100% di una CPU finché la situazione persiste.
                 * Una breve pausa dà tempo al sistema di liberare
                 * risorse (es. connessioni che si chiudono).
                 */
                struct timespec backoff;

                backoff.tv_sec = 0;
                backoff.tv_nsec = 100000000L; /* 100 ms */
                nanosleep(&backoff, NULL);
            }
            continue;
        }

        /*
         * Limite di connessioni concorrenti (Fase 8): oltre soglia,
         * la connessione viene rifiutata subito con 503 invece di
         * generare un thread aggiuntivo. Solo il thread principale
         * esegue questo controllo (nel ciclo accept(), mai in
         * parallelo con se stesso), quindi non c'è race condition
         * nonostante la lettura separata dall'incremento sottostante.
         */
        pthread_mutex_lock(&g_active_mutex);
        current_active = g_active_connections;
        pthread_mutex_unlock(&g_active_mutex);

        if (current_active >= MAX_CONCURRENT_CONNECTIONS) {
            char ip[INET_ADDRSTRLEN];
            long bytes;

            if (inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip)) == NULL) {
                strncpy(ip, "?", sizeof(ip));
            }
            bytes = http_send_error(client_fd, HTTP_METHOD_GET, 503,
                                     "Service Unavailable", NULL);
            log_request(ip, "-", 503, bytes);
            close(client_fd);
            continue;
        }

        /*
         * Timeout di lettura/scrittura sul socket client (Fase 8):
         * applicati qui, prima di passare la connessione al thread
         * worker, così ogni read()/write() successiva in
         * handle_connection() è automaticamente delimitata nel tempo.
         * Un fallimento di setsockopt() qui non è fatale: la
         * connessione viene comunque gestita, semplicemente senza
         * protezione da timeout (caso limite, non dovrebbe verificarsi
         * su sistemi POSIX conformi).
         */
        tv.tv_sec = REQUEST_READ_TIMEOUT_SECONDS;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        tv.tv_sec = RESPONSE_WRITE_TIMEOUT_SECONDS;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        task = malloc(sizeof(*task));
        if (task == NULL) {
            fprintf(stderr, "chttpd: memoria esaurita, connessione rifiutata\n");
            close(client_fd);
            continue;
        }
        task->client_fd = client_fd;
        task->client_addr = client_addr;
        task->root_dir = cfg.directory;
        task->verbose = cfg.verbose;
        task->list_dir = cfg.list_dir;

        active_connections_inc();

        sigemptyset(&block_set);
        sigaddset(&block_set, SIGINT);
        sigaddset(&block_set, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &block_set, &old_set);

        rc = pthread_create(&tid, NULL, conn_thread_main, task);

        pthread_sigmask(SIG_SETMASK, &old_set, NULL);

        if (rc != 0) {
            fprintf(stderr, "chttpd: creazione del thread fallita: %s\n", strerror(rc));
            close(client_fd);
            free(task);
            active_connections_dec();
            continue;
        }

        pthread_detach(tid);
    }

    printf("\nchttpd: arresto in corso, attendo il completamento delle connessioni "
           "attive (max %ds)...\n", SHUTDOWN_DRAIN_TIMEOUT_SECONDS);
    fflush(stdout);
    if (active_connections_wait_zero_timeout(SHUTDOWN_DRAIN_TIMEOUT_SECONDS)) {
        fprintf(stderr,
                "chttpd: timeout di arresto superato con connessioni ancora attive: "
                "uscita forzata\n");
    }
    server_close(listen_fd);

    return 0;
}
