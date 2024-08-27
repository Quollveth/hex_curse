#include <curses.h>
#include <errno.h>
#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct {
	char **lines;
	size_t viewStart;
	unsigned int viewSize;
} screenData;

WINDOW *borderWin, *contentWin;

void cleanup(void) { endwin(); }

const unsigned int nLines = 100;
void placeholder() {
	const char theline[10] = "   Test\0";
	int lineSize = strlen(theline) + 1;

	screenData.lines = (char **)malloc(sizeof(char *) * nLines);

	for (unsigned int i = 0; i < nLines; i++) {
		screenData.lines[i] = (char *)malloc(sizeof(char) * lineSize);
		strcpy(screenData.lines[i], theline);
		screenData.lines[i][1] = (i % 10) + '0';
		screenData.lines[i][0] = (i / 10) + '0';
	}
}

void updateView() {
	werase(contentWin);
	// clang-format off
	for (
		size_t i = screenData.viewStart;
		i < screenData.viewStart + screenData.viewSize && i < nLines;
		i++
	) {
		// clang-format on
		wprintw(contentWin, "%s\n", screenData.lines[i]);
	}
	wrefresh(contentWin);
}

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

	//	size_t filesize = st.st_size;

skipfile:

	/*---------------------
		WINDOW MANAGING
	----------------------*/

	initscr();
	noecho();
	raw();
	noqiflush();
	keypad(stdscr, TRUE);

	screenData.viewStart = 20;
	screenData.viewSize = LINES - 2;

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
	updateView();

	int ch;
	while (TRUE) {
		updateView();

		ch = getch();
		if (ch == KEY_F(1)) {
			break;
		}

		if (ch == 'j') {
			screenData.viewStart++;
		}

		if (ch == 'k') {
			screenData.viewStart--;
		}
	}
}
