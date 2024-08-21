#include <curses.h>
#include <errno.h>
#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct {
	uint8_t **lines;
	size_t viewStart;
	int viewSize;
} screenData;

WINDOW *borderWin, *contentWin;

void cleanup(void) { endwin(); }

void placeholder() {
	const int lines = 50;
	const int lineSize = 10;

	screenData.lines = (uint8_t **)malloc(sizeof(uint8_t *) * lines);

	for (int i = 0; i < lines; i++) {
		screenData.lines[i] = (uint8_t *)malloc(sizeof(uint8_t) * lineSize);
		for (int j = 0; j < lineSize; j++) {
			screenData.lines[i][j] = 'A';
		}
	}
}

void updateView() {}

int main(int argc, char **argv) {
	int err;
	atexit(cleanup);

	goto skipfile;
	/*---------------------
		ARGUMENT PARSING
	---------------------*/

	if (argc == 1) {
		fprintf(stderr, "No file provided\n");
		return 1;
	}

	/*----------------
		FILE OPENING
	----------------*/

	FILE *file = fopen(argv[1], "rb");

	if (file == NULL) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 1;
	}

	struct stat st;

	err = fstat(file->_fileno, &st);
	if (err == ERR) return 1;

	size_t filesize = st.st_size;

skipfile:

	/*---------------------
		WINDOW MANAGING
	----------------------*/

	initscr();
	noecho();
	raw();
	noqiflush();
	keypad(stdscr, TRUE);

	borderWin = newwin(
		LINES, // nlines
		COLS,  // ncols
		0,	   // ypos
		0	   // xpos
	);
	contentWin = newwin(LINES - 2, COLS - 2, 1, 1);
	if (borderWin == NULL || contentWin == NULL) return 1;

	box(borderWin, 0, 0);

	refresh();
	wrefresh(borderWin);

	placeholder();

	int ch;
	while (TRUE) {
		ch = getch();
		if (ch == KEY_F(1)) {
			break;
		}
	}
}
