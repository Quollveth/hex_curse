#include <curses.h>
#include <ncurses.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Compensate for the character border around
// While we can use the stats of the content window itself
// it's more convenient to have the global values be correct
#define WIN_Y LINES - 3
#define WIN_X COLS - 2

enum editorKeys {
	EditorQuit = KEY_F(1),
	EditorCursorUp = 'k',
	EditorCursorDown = 'j',
	EditorCursorLeft = 'l',
	EditorCursorRight = 'h',
};

struct {
	int bytesPerLine;
} editorSettings;

struct {
	uint8_t *fileData;
	size_t filesize, nLines;
} fileData;

struct {
	size_t viewStart;
	int viewSize, cursorX, cursorY;
} screenData;

WINDOW *borderWin, *contentWin;

void cleanup(void) {
	endwin();
	if (fileData.fileData != NULL) {
		free(fileData.fileData);
	}
}

void initializeEditor(void) {
	// We have a lot of global pointers, let's set them all to null
	borderWin = NULL;
	contentWin = NULL;
	fileData.fileData = NULL;
	// And someone has to free all of them later
	atexit(cleanup);
}

int initializeWindow() {
	// Initialize all windows
	initscr();
	borderWin = newwin(LINES, COLS, 0, 0);
	contentWin = newwin(LINES - 2, COLS - 2, 1, 1);
	if (borderWin == NULL || contentWin == NULL) return ERR;
	box(borderWin, 0, 0); // Border for the border window

	int err = OK;
	// Terminal settings
	noecho();			  // Don't echo characters back
	raw();				  // No line buffering
	keypad(stdscr, TRUE); // Function keys

	// Enable ctrl-z and ctrl-s keys
	struct termios term;
	err = tcgetattr(STDIN_FILENO, &term);
	if (err == ERR) return ERR;

	term.c_cc[VSTOP] = _POSIX_VDISABLE;
	term.c_cc[VSUSP] = _POSIX_VDISABLE;
	err = tcsetattr(STDIN_FILENO, TCSANOW, &term);
	if (err == ERR) return ERR;

	// Disable ctrl-c key
	if (signal(SIGINT, SIG_IGN) == SIG_ERR) return ERR;

	// Setup global window data
	screenData.viewStart = 20;
	screenData.viewSize = LINES - 2;
	screenData.cursorY = 0;
	screenData.cursorX = 0;

	// Refresh window and cursor
	refresh();
	wrefresh(borderWin);
	wmove(contentWin, screenData.cursorY, screenData.cursorX);
	wrefresh(contentWin);

	return OK;
}

int openFile(char *filename) {
	FILE *fp = fopen(filename, "rb");

	if (fp == NULL) return ERR;

	struct stat st;

	if (fstat(fileno(fp), &st) == ERR) return ERR;

	fileData.filesize = st.st_size;
	fileData.nLines = fileData.filesize / editorSettings.bytesPerLine;

	// -------- read file -----------

	fileData.fileData = (uint8_t *)malloc(fileData.filesize);
	// this memory will be freed at exit, doing it here will lead to a double free
	if (fileData.fileData == NULL) {
		fclose(fp);
		return ERR;
	}

	size_t bytesRead = fread(fileData.fileData, 1, fileData.filesize, fp);

	if (bytesRead != fileData.filesize) {
		// TODO: More specific errors
		if (feof(fp)) {
			// unexpected eof
			fclose(fp);
			return ERR;
		}
		// ferror
		fclose(fp);
		return ERR;
	}
	fclose(fp);
	return OK;
}

void handleCommand(char ch) {
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
			if (screenData.viewStart == fileData.nLines - 1) {
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
}

int main() {
	initializeEditor();
	initializeWindow();

	editorSettings.bytesPerLine = 8;

	int ch;
	while ((ch = getch()) != EditorQuit) {
		wmove(contentWin, screenData.cursorY, screenData.cursorX);
		wrefresh(contentWin);
	}
	cleanup();
}
