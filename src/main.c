#include <curses.h>
#include <errno.h>
#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Compensate for the character border around
// While we can use the stats of the content window itself
// it's more convenient to have the global values be correct
#define WIN_Y LINES - 3
#define WIN_X COLS - 2

enum EditorKeys {
	EditorQuit = KEY_F(1),
	EditorCursorUp = 'k',
	EditorCursorDown = 'j',
	EditorCursorLeft = 'l',
	EditorCursorRight = 'h',
};

struct {
	char **lines;
	size_t viewStart;
	unsigned int viewSize;
	int cursorX, cursorY;
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
	static unsigned int lastViewSize = 0;
	static unsigned int lastViewStart = 0;

	if (screenData.viewSize == lastViewSize && screenData.viewStart == lastViewStart) {
		// No change from last time
		return;
	}

	lastViewStart = screenData.viewStart;
	lastViewSize = screenData.viewSize;

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
	screenData.cursorY = 0;
	screenData.cursorX = 0;

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

	wmove(contentWin, screenData.cursorY, screenData.cursorX);
	wrefresh(contentWin);

	int ch;
	while ((ch = getch()) != EditorQuit) {
		switch (ch) {
		case EditorCursorUp:
			// if at first line scroll up
			if (screenData.cursorY == 0) {
				// if we can't scroll up do nothing
				if (screenData.viewStart == 0) {
					break;
				}
				screenData.viewStart--;
				break;
			}
			screenData.cursorY--;
			break;
		case EditorCursorDown:
			// if at last line scroll down
			if (screenData.cursorY == WIN_Y) {
				// if we can't scroll down do nothing
				// TODO: change to not be a hardcoded value
				if (screenData.viewStart == nLines) {
					break;
				}
				screenData.viewStart++;
				break;
			}
			screenData.cursorY++;
			break;
		case EditorCursorLeft:
			if (screenData.cursorX == WIN_X) {
				break;
			}
			screenData.cursorX++;
			break;
		case EditorCursorRight:
			if (screenData.cursorX == 0) {
				break;
			}
			screenData.cursorX--;
			break;
		}
		updateView();
		wmove(contentWin, screenData.cursorY, screenData.cursorX);
		wrefresh(contentWin);
	}
	cleanup();
}
