#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <termios.h>

/* Left off at: https://viewsourcecode.org/snaptoken/kilo/05.aTextEditor.html#save-as */

#define TAB_STOP 8
#define QUIT_TIMES 2

#define CTRL_KEY(k) ((k) & 0x1f)

#define ESC "\x1b"
#define ESC_CHAR '\x1b'

#define CLR_SCR             ESC "[2J"
#define CLR_ROW             ESC "[K"
#define CUR_TOP_LEFT        ESC "[H"
#define CUR_BOT_RIGHT       ESC "[999C" ESC "[999B"
#define GET_CUR_POS         ESC "[6n"
#define HIDE_CUR            ESC "[?25l"
#define SHOW_CUR            ESC "[?25h"
#define INV_COLOR           ESC "[7m"
#define NORMAL_COLOR        ESC "[m"

#define CLR_SCR_LEN         sizeof(CLR_SCR)-1
#define CLR_ROW_LEN         sizeof(CLR_ROW)-1
#define CUR_TOP_LEFT_LEN    sizeof(CUR_TOP_LEFT)-1
#define CUR_BOT_RIGHT_LEN   sizeof(CUR_BOT_RIGHT)-1
#define GET_CUR_POS_LEN     sizeof(GET_CUR_POS)-1
#define HIDE_CUR_LEN        sizeof(HIDE_CUR)-1
#define SHOW_CUR_LEN        sizeof(SHOW_CUR)-1
#define INV_COLOR_LEN       sizeof(INV_COLOR)-1
#define NORMAL_COLOR_LEN    sizeof(NORMAL_COLOR)-1

enum editor_key {
    KEY_BACKSPACE = 127,
    KEY_LEFT  = 1000,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_PG_UP,
    KEY_PG_DN,
    KEY_DEL,
    KEY_INSERT
/*    KEY_LEFT  = 'h', */
/*    KEY_RIGHT = 'l', */
/*    KEY_UP    = 'k', */
/*    KEY_DOWN  = 'j' */
};

typedef struct erow {
    int size;
    int rsize;
    char * chars;
    char * render;
} erow;

struct editor_config {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow * row;
    int dirty;
    char * filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};

struct editor_config E;

void
die(const char * s)
{
    write(STDOUT_FILENO, CLR_SCR, CLR_SCR_LEN);
    write(STDOUT_FILENO, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);

    perror(s);
    exit(1);
}

void
disable_raw()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void
enable_raw()
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disable_raw);

    raw = E.orig_termios;

    /* effect of disabling the following flags: */
    /* ICRNL: prevent Ctrl-M, which should be \r, from being converted to \n */
    /* IXON: disable software flow control (Ctrl-S and Ctrl-Q) */
    /* OPOST: disable output processing e.g. \n -> \r\n */
    /* ECHO: disable echoing */
    /* ICANON: characters are read immediately instead of waiting for \n */
    /* IEXTEN: disable Ctrl-V (special behavior on some systems) */
    /* ISIG: disable Ctrl-C and Ctrl-Z signals */

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

#if 0
void
echo_key()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    if (CTRL_KEY('q') == c) {
        exit(0);
    } else {
        char buf[10];
        sprintf(buf, "%d ", c);
        write(STDOUT_FILENO, buf, strlen(buf));
    }
}
#endif

int
editor_read_key()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        /* In Cygwin, when read() times out it returns -1 with an errno of
           EAGAIN, instead of just returning 0 like it’s supposed to. */
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == ESC_CHAR) {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return ESC_CHAR;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return ESC_CHAR;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return ESC_CHAR;
                if (seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return KEY_HOME;
                        case '2': return KEY_INSERT;
                        case '3': return KEY_DEL;
                        case '4': return KEY_END;
                        case '5': return KEY_PG_UP;
                        case '6': return KEY_PG_DN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'C': return KEY_RIGHT;
                    case 'D': return KEY_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
        return ESC_CHAR;
    } else {
        return c;
    }
}

int
get_cursor_position(int * rows, int * cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, GET_CUR_POS, 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != ESC_CHAR || buf[1] != '[')
        return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int
get_window_size(int * rows, int * cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, CUR_BOT_RIGHT, CUR_BOT_RIGHT_LEN) != CUR_BOT_RIGHT_LEN)
            return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

struct abuf {
    char * b;
    int len;
};

#define ABUF_INIT {NULL, 0};

void
ab_append(struct abuf * ab, const char * s, int len)
{
    char * new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void
abFree(struct abuf * ab)
{
    free(ab->b);
}

int
editor_row_cx_to_rx(erow * row, int cx)
{
    int rx = 0;
    int j;
    for (j=0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

void
editor_update_row(erow * row)
{
    int tabs = 0;
    int j, idx;

    for (j=0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOP-1) + 1);

    idx = 0;
    for (j=0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++]= row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void
editor_insert_row(int at, char * s, size_t len)
{
    if (at < 0 || at > E.numrows)
        return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editor_update_row(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void
editor_free_row(erow * row)
{
    free(row->render);
    free(row->chars);
}

void
editor_del_row(int at)
{
    if (at < 0 || at >= E.numrows)
        return;
    editor_free_row(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void
editor_row_insert_char(erow * row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_update_row(row);
    E.dirty++;
}

void
editor_row_append_string(erow * row, char * s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
    E.dirty++;
}

void
editor_row_del_char(erow * row, int at)
{
    if (at < 0 || at >= row->size)
        return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editor_update_row(row);
    E.dirty++;
}

void
editor_insert_char(int c)
{
    if (E.cy == E.numrows) {
        editor_insert_row(E.numrows, "", 0);
    }
    editor_row_insert_char(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void
editor_insert_new_line()
{
    if (E.cx == 0) {
        editor_insert_row(E.cy, "", 0);
    } else {
        erow * row = &E.row[E.cy];
        editor_insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy]; /* needed because editor_insert_row calls realloc! */
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editor_update_row(row);
    }
    E.cy++;
    E.cx = 0;
}

void
editor_del_char()
{
    erow * row;
    if (E.cy == E.numrows)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;
    row = &E.row[E.cy];
    if (E.cx > 0) {
        editor_row_del_char(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editor_row_append_string(&E.row[E.cy - 1], row->chars, row->size);
        editor_del_row(E.cy);
        E.cy--;
    }
}

void
editor_scroll()
{
    E.rx = E.cx;
    if (E.cy < E.numrows) {
        E.rx = editor_row_cx_to_rx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void
editor_draw_rows(struct abuf * ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y  + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                int padding;
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "ped editor -- version %s", "1.0");
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    ab_append(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    ab_append(ab, " ", 1);
                ab_append(ab, welcome, welcomelen);
            } else {
                ab_append(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            ab_append(ab, &E.row[filerow].render[E.coloff], len);
        }
        ab_append(ab, CLR_ROW, CLR_ROW_LEN);
        ab_append(ab, "\r\n", 2);
    }
}

void
editor_draw_status_bar(struct abuf * ab)
{
    char status[80];
    char rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
            E.filename ? E.filename : "[No Name]", E.numrows,
            E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
            E.cy + 1, E.numrows);

    if (len > E.screencols)
        len = E.screencols;

    ab_append(ab, status, len);

    ab_append(ab, INV_COLOR, INV_COLOR_LEN);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            ab_append(ab, rstatus, rlen);
            break;
        } else {
            ab_append(ab, " ", 1);
            len++;
        }
    }
    ab_append(ab, NORMAL_COLOR, NORMAL_COLOR_LEN);
    ab_append(ab, "\r\n", 2);
}

void
editor_draw_message_bar(struct abuf * ab)
{
    int msglen = strlen(E.statusmsg);
    ab_append(ab, CLR_ROW, CLR_ROW_LEN);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        ab_append(ab, E.statusmsg, msglen);
}

void
editor_set_status_message(const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void
editor_move_cursor(int key)
{
    int rowlen;
    erow * row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case KEY_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case KEY_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case KEY_UP:
            if (E.cy > 0)
                E.cy--;
            break;
        case KEY_DOWN:
            if (E.cy < E.numrows - 1)
                E.cy++;
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
        E.cx = rowlen;
}

#if 0
/* TODO: debug */
ssize_t
my_getline(char ** s, size_t * cap, FILE * stream)
{
    char buf[100];
    size_t len = 0;
    buf[99] = '\0';
    *cap = sizeof(buf);
    *s = malloc(*cap);
    (*s)[0] = '\0';
    while (1) {
        if (fgets(buf, sizeof(buf), stream) == NULL)
            break;
        strncpy(*s + len, buf, sizeof(buf));
        len = strlen(buf);
        if (len < sizeof(buf)-1)
            break;
        *cap *= 2;
        *s = realloc(*s, *cap);
        if (*s == NULL) {
            perror("getline");
            exit(EXIT_FAILURE);
        }

    }
    return len;
}
#endif

char *
editor_rows_to_string(int * buflen)
{
    int totlen = 0;
    int j;
    char * buf, * p;

    for (j=0; j<E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    buf = malloc(totlen);
    p = buf;

    for (j=0; j<E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void
editor_open(char * filename)
{
    char * line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    FILE * fp = fopen(filename, "r");
    free(E.filename);
    E.filename = strdup(filename);
    if (fp == NULL)
        die("fopen");

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
                (line[linelen-1] == '\n' ||
                 line[linelen-1] == '\r'))
            linelen--;
        editor_insert_row(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void
editor_save()
{
    int len;
    char * buf;
    int fd;
    if (E.filename == NULL)
        return;

    buf = editor_rows_to_string(&len);
    fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editor_set_status_message("%d bytes written to disk", len);
                E.dirty = 0;
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editor_set_status_message("Can't save! I/O error: %s", strerror(errno));
}

void
editor_refresh_screen()
{
    char buf[32];
    struct abuf ab = ABUF_INIT;

    editor_scroll();

    ab_append(&ab, HIDE_CUR, HIDE_CUR_LEN);
    ab_append(&ab, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    snprintf(buf, sizeof(buf), ESC "[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, SHOW_CUR, SHOW_CUR_LEN);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void
editor_process_keypress()
{
    static int quit_times = QUIT_TIMES;
    int c = editor_read_key();

    switch (c) {
        case '\r':
            editor_insert_new_line();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editor_set_status_message("WARNING: File has unsaved changes. "
                        "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, CLR_SCR, CLR_SCR_LEN);
            write(STDOUT_FILENO, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);
            exit(0);
            break;

        case CTRL_KEY('s'):
            editor_save();
            break;

        case KEY_PG_UP:
        case KEY_PG_DN:
            {
                int times;
                if (c == KEY_PG_UP) {
                    E.cy = E.rowoff;
                } else if (c == KEY_PG_DN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows)
                        E.cy = E.numrows;
                }
                times = E.screenrows;
                while (times--)
                    editor_move_cursor(c == KEY_PG_UP ? KEY_UP : KEY_DOWN);
            }
            break;

        case KEY_HOME:
            E.cx = 0;
            break;

        case KEY_END:
            E.cx = E.row[E.cy].size;
            break;

        case KEY_BACKSPACE:
        case CTRL_KEY('h'):
        case KEY_DEL:
            if (c == KEY_DEL)
                editor_move_cursor(KEY_RIGHT);
            editor_del_char();
            break;

        case KEY_LEFT:
        case KEY_DOWN:
        case KEY_UP:
        case KEY_RIGHT:
            editor_move_cursor(c);
            break;

        case CTRL_KEY('l'):
        case ESC_CHAR:
            break;

        default:
            editor_insert_char(c);
            break;
    }

    quit_times = QUIT_TIMES;
}

void
init_editor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    if (get_window_size(&E.screenrows, &E.screencols) == -1)
        die("get_window_size");
    E.screenrows -= 2;
}

int main(int argc, char * argv[])
{
    enable_raw();
    init_editor();
    if (argc >= 2) {
        editor_open(argv[1]);
    }
    editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit");
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
        /*echo_key();*/
    }
    return 0;
}
