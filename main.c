#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>

/* Left off at: https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html#arrow-keys */

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

char
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
    return c;
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
editor_move_cursor(char key)
{
    switch (key) {
        case 'h':
            E.cx--;
            break;
        case 'j':
            E.cy++;
            break;
        case 'k':
            E.cy--;
            break;
        case 'l':
            E.cx++;
            break;
    }
}

void
editor_process_keypress()
{
    char c = editor_read_key();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, CLR_SCR, CLR_SCR_LEN);
            write(STDOUT_FILENO, CUR_TOP_LEFT, CUR_TOP_LEFT_LEN);
            exit(0);
            break;
        case 'h':
        case 'j':
        case 'k':
        case 'l':
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
/*    int n = 0; */
    enable_raw();
    init_editor();
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }
    return 0;
}
