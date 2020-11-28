#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>

/* Left off at: https://viewsourcecode.org/snaptoken/kilo/04.aTextViewer.html */

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

#define CLR_SCR_LEN         sizeof(CLR_SCR)-1
#define CLR_ROW_LEN         sizeof(CLR_ROW)-1
#define CUR_TOP_LEFT_LEN    sizeof(CUR_TOP_LEFT)-1
#define CUR_BOT_RIGHT_LEN   sizeof(CUR_BOT_RIGHT)-1
#define GET_CUR_POS_LEN     sizeof(GET_CUR_POS)-1
#define HIDE_CUR_LEN        sizeof(HIDE_CUR)-1
#define SHOW_CUR_LEN        sizeof(SHOW_CUR)-1

enum editor_key {
    KEY_LEFT  = 1000,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_PG_UP,
    KEY_PG_DN,
    KEY_DEL,
    KEY_INSERT,
//    KEY_LEFT  = 'h',
//    KEY_RIGHT = 'l',
//    KEY_UP    = 'k',
//    KEY_DOWN  = 'j'
};

struct editor_config {
    int cx, cy;
    int screenrows;
    int screencols;
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

void
editor_move_cursor(int key)
{
    switch (key) {
        case KEY_LEFT:
            if (E.cx > 0)
                E.cx--;
            break;
        case KEY_RIGHT:
            if (E.cx < E.screencols - 1)
                E.cx++;
            break;
        case KEY_UP:
            if (E.cy > 0)
                E.cy--;
            break;
        case KEY_DOWN:
            if (E.cy < E.screenrows - 1)
                E.cy++;
            break;
    }
}

void
editor_process_keypress()
{
    int c = editor_read_key();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, CLR_SCR, CLR_SCR_LEN);
            write(STDOUT_FILENO, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);
            exit(0);
            break;
        case KEY_PG_UP:
        case KEY_PG_DN:
            {
                int times = E.screenrows;
                while (times--)
                    editor_move_cursor(c == KEY_PG_UP ? KEY_UP : KEY_DOWN);
            }
            break;
        case KEY_HOME:
            E.cx = 0;
            break;

        case KEY_END:
            E.cx = E.screencols - 1;
            break;

        case KEY_LEFT:
        case KEY_DOWN:
        case KEY_UP:
        case KEY_RIGHT:
            editor_move_cursor(c);
            break;
    }
}

void
editor_draw_rows(struct abuf * ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++) {

        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", "1.0");
            if (welcomelen > E.screencols)
                welcomelen = E.screencols;
			int padding = (E.screencols - welcomelen) / 2;
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
        ab_append(ab, CLR_ROW, CLR_ROW_LEN);
        if (y < E.screenrows - 1)
            ab_append(ab, "\r\n", 2);
    }
}

void
editor_refresh_screen()
{
    struct abuf ab = ABUF_INIT;

    ab_append(&ab, HIDE_CUR, HIDE_CUR_LEN);
    ab_append(&ab, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);

    editor_draw_rows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), ESC "[%d;%dH", E.cy + 1, E.cx + 1);
    ab_append(&ab, buf, strlen(buf));

    //ab_append(&ab, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);
    ab_append(&ab, SHOW_CUR, SHOW_CUR_LEN);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void
init_editor()
{
    E.cx = 0;
    E.cy = 0;
    if (get_window_size(&E.screenrows, &E.screencols) == -1)
        die("get_window_size");
}

int main()
{
    enable_raw();
    init_editor();
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
        //echo_key();
    }
    return 0;
}
