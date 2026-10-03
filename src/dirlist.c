/*
 * dirlist.c
 *
 * v1.1: lettura di una directory, ordinamento delle voci (directory
 * prima, poi file, entrambe in ordine alfabetico case-insensitive),
 * generazione di una pagina HTML in stile "index of" e invio come
 * risposta 200 OK tramite http_send_html() (http_response.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp (POSIX) */
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#include "dirlist.h"
#include "http_response.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DIRLIST_NAME_LEN 256

/* ---------------------------------------------------------------- *
 * strbuf: piccolo "string builder" con buffer dinamico (realloc),
 * necessario perché il numero di voci di una directory (e quindi la
 * dimensione della pagina HTML) non è noto a priori.
 * ---------------------------------------------------------------- */

struct strbuf {
    char *data;
    size_t len;
    size_t cap;
};

static int strbuf_init(struct strbuf *sb)
{
    sb->cap = 4096;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    if (sb->data == NULL) {
        return -1;
    }
    sb->data[0] = '\0';
    return 0;
}

static int strbuf_append(struct strbuf *sb, const char *s)
{
    size_t slen = strlen(s);
    size_t needed = sb->len + slen + 1;

    if (needed > sb->cap) {
        size_t new_cap = sb->cap;
        char *new_data;

        while (new_cap < needed) {
            new_cap *= 2;
        }
        new_data = realloc(sb->data, new_cap);
        if (new_data == NULL) {
            return -1;
        }
        sb->data = new_data;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, slen + 1); /* include il NUL finale */
    sb->len += slen;
    return 0;
}

static void strbuf_free(struct strbuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

/* ---------------------------------------------------------------- *
 * Escaping HTML e percent-encoding per gli href, byte per byte:
 * sicuro anche con nomi UTF-8, dato che i byte di continuazione UTF-8
 * (0x80-0xBF) non collidono mai con i caratteri ASCII speciali qui
 * gestiti (&, <, >, ", ').
 * ---------------------------------------------------------------- */

static int html_escape_append(struct strbuf *sb, const char *s)
{
    for (; *s != '\0'; s++) {
        const char *rep = NULL;
        char single[2];

        switch (*s) {
        case '&':  rep = "&amp;";  break;
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '"':  rep = "&quot;"; break;
        case '\'': rep = "&#39;";  break;
        default:   break;
        }
        if (rep != NULL) {
            if (strbuf_append(sb, rep) != 0) {
                return -1;
            }
        } else {
            single[0] = *s;
            single[1] = '\0';
            if (strbuf_append(sb, single) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int url_encode_append(struct strbuf *sb, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";

    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char) *s;

        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            char single[2];

            single[0] = (char) c;
            single[1] = '\0';
            if (strbuf_append(sb, single) != 0) {
                return -1;
            }
        } else {
            char enc[4];

            enc[0] = '%';
            enc[1] = hex[(c >> 4) & 0xF];
            enc[2] = hex[c & 0xF];
            enc[3] = '\0';
            if (strbuf_append(sb, enc) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- *
 * Formattazione dimensione e data.
 * ---------------------------------------------------------------- */

static void format_size(long size, char *buf, size_t buf_size)
{
    double s;
    const char *unit;

    if (size < 0) {
        size = 0;
    }
    s = (double) size;
    unit = "B";
    if (s >= 1024.0 * 1024.0 * 1024.0) {
        s /= 1024.0 * 1024.0 * 1024.0;
        unit = "GB";
    } else if (s >= 1024.0 * 1024.0) {
        s /= 1024.0 * 1024.0;
        unit = "MB";
    } else if (s >= 1024.0) {
        s /= 1024.0;
        unit = "KB";
    }
    if (strcmp(unit, "B") == 0) {
        snprintf(buf, buf_size, "%ld %s", (long) s, unit);
    } else {
        snprintf(buf, buf_size, "%.1f %s", s, unit);
    }
}

/*
 * Formato ISO 8601 (data e ora, UTC): "AAAA-MM-GG HH:MM". Si usa
 * sempre UTC (gmtime_r, variante thread-safe: più thread possono
 * generare listing in parallelo) per evitare dipendenze dalla locale
 * di sistema, coerentemente con il resto del progetto (log CLF,
 * header Date).
 */
static void format_mtime(time_t mtime, char *buf, size_t buf_size)
{
    struct tm tm_buf;
    struct tm *tm_info = gmtime_r(&mtime, &tm_buf);

    if (tm_info != NULL) {
        strftime(buf, buf_size, "%Y-%m-%d %H:%M", tm_info);
    } else {
        strncpy(buf, "-", buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
}

static const char *get_icon(const char *name, int is_dir)
{
    const char *dot;

    if (is_dir) {
        return "\xF0\x9F\x93\x81"; /* 📁 */
    }
    dot = strrchr(name, '.');
    if (dot != NULL) {
        char ext[8];
        size_t len = strlen(dot);
        size_t i;

        if (len < sizeof(ext)) {
            for (i = 0; i < len; i++) {
                ext[i] = (char) tolower((unsigned char) dot[i]);
            }
            ext[len] = '\0';
            if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0 ||
                strcmp(ext, ".svg") == 0) {
                return "\xF0\x9F\x96\xBC\xEF\xB8\x8F"; /* 🖼️ (con variation selector, come nel mockup originale) */
            }
            if (strcmp(ext, ".zip") == 0 || strcmp(ext, ".tar") == 0 ||
                strcmp(ext, ".gz") == 0 || strcmp(ext, ".bz2") == 0 ||
                strcmp(ext, ".xz") == 0 || strcmp(ext, ".7z") == 0) {
                return "\xF0\x9F\x93\xA6"; /* 📦 */
            }
        }
    }
    return "\xF0\x9F\x93\x84"; /* 📄 */
}

/* ---------------------------------------------------------------- *
 * Lettura ed ordinamento delle voci della directory.
 * ---------------------------------------------------------------- */

struct dirlist_entry {
    char name[DIRLIST_NAME_LEN];
    int is_dir;
    long size;
    time_t mtime;
};

static int compare_entries(const void *a, const void *b)
{
    const struct dirlist_entry *ea = (const struct dirlist_entry *) a;
    const struct dirlist_entry *eb = (const struct dirlist_entry *) b;

    if (ea->is_dir != eb->is_dir) {
        return eb->is_dir - ea->is_dir; /* le directory precedono i file */
    }
    return strcasecmp(ea->name, eb->name);
}

/*
 * read_directory_entries
 *
 * Legge 'dir_path' e popola un array allocato dinamicamente di
 * struct dirlist_entry (escludendo "." e ".."), ordinato secondo
 * compare_entries(). Le voci il cui stat() fallisce (es. symlink
 * rotti) sono omesse silenziosamente.
 *
 * Ritorna 0 in caso di successo (anche con zero voci: directory
 * vuota), -1 in caso di errore nell'apertura della directory o di
 * memoria esaurita. '*out_entries'/'*out_count' sono valorizzati solo
 * in caso di successo; il chiamante deve fare free(*out_entries).
 */
static int read_directory_entries(const char *dir_path,
                                   struct dirlist_entry **out_entries,
                                   size_t *out_count)
{
    DIR *dir;
    struct dirent *de;
    struct dirlist_entry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    dir = opendir(dir_path);
    if (dir == NULL) {
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        char entry_path[PATH_MAX];
        struct stat st;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, de->d_name)
                >= (int) sizeof(entry_path)) {
            continue; /* path risultante troppo lungo: voce ignorata */
        }
        if (stat(entry_path, &st) != 0) {
            continue; /* non accessibile (es. symlink rotto): omessa */
        }

        if (count == capacity) {
            size_t new_capacity = (capacity == 0) ? 32 : capacity * 2;
            struct dirlist_entry *new_entries =
                realloc(entries, new_capacity * sizeof(*entries));

            if (new_entries == NULL) {
                free(entries);
                closedir(dir);
                return -1;
            }
            entries = new_entries;
            capacity = new_capacity;
        }

        strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        entries[count].size = (long) st.st_size;
        entries[count].mtime = st.st_mtime;
        count++;
    }
    closedir(dir);

    if (count > 0) {
        qsort(entries, count, sizeof(*entries), compare_entries);
    }

    *out_entries = entries;
    *out_count = count;
    return 0;
}

/* ---------------------------------------------------------------- *
 * Costruzione della pagina HTML.
 * ---------------------------------------------------------------- */

/*
 * append_page_head
 *
 * Scrive l'intestazione della pagina (doctype, head, CSS incorporato,
 * apertura di <body> e del contenitore) su 'html'. Suddivisa in più
 * chiamate a strbuf_append() con letterali più corti: un singolo
 * letterale con l'intero markup concatenato (anche solo tramite
 * adiacenza di stringhe C) supererebbe il limite minimo di 509
 * caratteri che ISO C90 richiede ai compilatori di supportare,
 * generando un warning con "-ansi -pedantic" (stesso problema, e
 * stessa soluzione, già incontrato in Fase 6 per il messaggio di
 * usage in main.c).
 */
static int append_page_head(struct strbuf *html)
{
    if (strbuf_append(html,
            "<!DOCTYPE html>\n<html lang=\"it\">\n<head>\n"
            "<meta charset=\"UTF-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, "
            "initial-scale=1.0\">\n"
            "<title>Indice della Directory</title>\n<style>\n") != 0) {
        return -1;
    }
    if (strbuf_append(html,
            ":root{--bg-color:#f8f9fa;--text-color:#212529;"
            "--border-color:#dee2e6;--hover-color:#e9ecef;}\n"
            "body{font-family:-apple-system,BlinkMacSystemFont,"
            "\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif;"
            "background-color:var(--bg-color);color:var(--text-color);"
            "padding:20px;margin:0;}\n") != 0) {
        return -1;
    }
    if (strbuf_append(html,
            ".container{max-width:1000px;margin:0 auto;"
            "background:#ffffff;padding:20px;border-radius:8px;"
            "box-shadow:0 4px 6px rgba(0,0,0,0.05);}\n"
            "h1{font-size:1.5rem;margin-bottom:20px;"
            "border-bottom:2px solid var(--border-color);"
            "padding-bottom:10px;word-break:break-all;}\n") != 0) {
        return -1;
    }
    if (strbuf_append(html,
            ".icon{font-size:1.25rem;margin-right:10px;"
            "display:inline-block;vertical-align:middle;}\n"
            ".list-header,.file-item{display:grid;"
            "grid-template-columns:1fr 150px 150px;padding:10px;"
            "border-bottom:1px solid var(--border-color);"
            "align-items:center;}\n") != 0) {
        return -1;
    }
    if (strbuf_append(html,
            ".list-header{font-weight:bold;"
            "background-color:var(--bg-color);"
            "border-top:1px solid var(--border-color);}\n"
            ".file-item{text-decoration:none;color:inherit;}\n"
            ".file-item:hover{background-color:var(--hover-color);}\n"
            ".file-name{white-space:nowrap;overflow:hidden;"
            "text-overflow:ellipsis;}\n"
            ".file-size,.file-date{font-size:0.9rem;color:#6c757d;}\n") != 0) {
        return -1;
    }
    if (strbuf_append(html,
            "@media (max-width:600px){.list-header,.file-item{"
            "grid-template-columns:1fr;gap:5px;}"
            ".file-size,.file-date{font-size:0.8rem;padding-left:32px;}"
            ".list-header{display:none;}}\n"
            "</style>\n</head>\n<body>\n<div class=\"container\">\n") != 0) {
        return -1;
    }
    return 0;
}

static const char PAGE_TAIL[] =
    "</div>\n"
    "</body>\n"
    "</html>\n";

/*
 * append_entry_row
 *
 * Aggiunge a 'html' la riga per una singola voce (o per il link alla
 * directory superiore, se 'name' è NULL). 'href' è già pronto
 * (percent-encoded) da chi chiama.
 */
static int append_entry_row(struct strbuf *html, const char *href,
                             const char *icon, const char *display_name,
                             int is_dir, const char *date_str, const char *size_str)
{
    if (strbuf_append(html, "<a href=\"") != 0) return -1;
    if (strbuf_append(html, href) != 0) return -1;
    if (strbuf_append(html, "\" class=\"file-item\">\n"
                            "<span class=\"file-name\">"
                            "<span class=\"icon\">") != 0) return -1;
    if (strbuf_append(html, icon) != 0) return -1;
    if (strbuf_append(html, "</span>") != 0) return -1;
    if (html_escape_append(html, display_name) != 0) return -1;
    if (is_dir) {
        if (strbuf_append(html, "/") != 0) return -1;
    }
    if (strbuf_append(html, "</span>\n<div class=\"file-date\">") != 0) return -1;
    if (strbuf_append(html, date_str) != 0) return -1;
    if (strbuf_append(html, "</div>\n<div class=\"file-size\">") != 0) return -1;
    if (strbuf_append(html, size_str) != 0) return -1;
    if (strbuf_append(html, "</div>\n</a>\n") != 0) return -1;
    return 0;
}

long dirlist_send(int fd, enum http_method method, const char *dir_path,
                   const char *url_path)
{
    struct dirlist_entry *entries;
    size_t count;
    struct strbuf html;
    int show_parent;
    size_t i;
    long result;

    if (read_directory_entries(dir_path, &entries, &count) != 0) {
        return -1;
    }

    if (strbuf_init(&html) != 0) {
        free(entries);
        return -1;
    }

    show_parent = (strcmp(url_path, "/") != 0);

    if (append_page_head(&html) != 0 ||
        strbuf_append(&html, "<h1>Indice di: ") != 0 ||
        html_escape_append(&html, url_path) != 0 ||
        strbuf_append(&html,
            "</h1>\n"
            "<div class=\"list-header\">"
            "<div>Nome</div><div>Ultima Modifica (UTC)</div>"
            "<div>Dimensione</div></div>\n") != 0) {
        strbuf_free(&html);
        free(entries);
        return -1;
    }

    if (show_parent) {
        if (append_entry_row(&html, "../", "\xE2\xAE\xAA" /* ⮪ */,
                              "Directory superiore (..)", 0, "-", "-") != 0) {
            strbuf_free(&html);
            free(entries);
            return -1;
        }
    }

    for (i = 0; i < count; i++) {
        struct strbuf href;
        char date_str[32];
        char size_str[32];
        const char *icon = get_icon(entries[i].name, entries[i].is_dir);
        int rc;

        if (strbuf_init(&href) != 0) {
            strbuf_free(&html);
            free(entries);
            return -1;
        }
        rc = url_encode_append(&href, entries[i].name);
        if (rc == 0 && entries[i].is_dir) {
            rc = strbuf_append(&href, "/");
        }
        if (rc != 0) {
            strbuf_free(&href);
            strbuf_free(&html);
            free(entries);
            return -1;
        }

        format_mtime(entries[i].mtime, date_str, sizeof(date_str));
        if (entries[i].is_dir) {
            strncpy(size_str, "-", sizeof(size_str) - 1);
            size_str[sizeof(size_str) - 1] = '\0';
        } else {
            format_size(entries[i].size, size_str, sizeof(size_str));
        }

        rc = append_entry_row(&html, href.data, icon, entries[i].name,
                               entries[i].is_dir, date_str, size_str);
        strbuf_free(&href);
        if (rc != 0) {
            strbuf_free(&html);
            free(entries);
            return -1;
        }
    }

    free(entries);

    if (strbuf_append(&html, PAGE_TAIL) != 0) {
        strbuf_free(&html);
        return -1;
    }

    result = http_send_html(fd, method, html.data, html.len);
    strbuf_free(&html);

    return result;
}
