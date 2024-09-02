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

enum editorKeys {
	EditorQuit = KEY_F(1),
	EditorCursorUp = 'k',
	EditorCursorDown = 'j',
	EditorCursorLeft = 'l',
	EditorCursorRight = 'h',
};

// -------- global state -----------

// Passed as commandline arguments
struct {
	int bytesPerLine;
	char *filename;
} editorSettings;

// Contains dumped data of the file
// nLines is used to know how much we can show on the screen for scrolling purposes
struct {
	uint8_t *fileData;
	size_t filesize, nLines;
} fileData;

// Contains data about the screen
// viewStart is the position in the file where we start showing the lines
// viewSize is how much we show and depends on screen size
struct {
	size_t viewStart;
	int viewSize, cursorX, cursorY;
} screenData;

// Instead of offsetting based on the border characters when writing to a screen
// we just have two windows per window, one with the border and a smaller one inside
typedef struct {
	WINDOW *border, *content;
} WindowWithBorder;

// editor -> where the editing happens
// view -> ascii representation of hex
// status -> shows information
WindowWithBorder *editorWindow, *viewWindow, *statusBar;

// -------- -------------- -----------

// we have oop at home
// -------- window methods -----------

void printToWindow(WindowWithBorder *window, const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	vw_printw(window->content, fmt, argp);
	va_end(argp);
	wrefresh(window->content);
}

WindowWithBorder *createWindow(bool addBorder, int lines, int cols, int y, int x) {
	WindowWithBorder *temp = (WindowWithBorder *)malloc(sizeof(WindowWithBorder));
	if (temp == NULL) return NULL;
	temp->border = NULL;
	temp->content = NULL;

	WINDOW *contentW;
	if (!addBorder) {
		contentW = newwin(lines, cols, y, x);
		temp->content = contentW;
		return temp;
	}

	WINDOW *borderW;

	borderW = newwin(lines, cols, y, x);
	if (borderW == NULL) {
		free(temp);
		return NULL;
	}

	contentW = newwin(lines - 2, cols - 2, y + 1, x + 1);
	if (contentW == NULL) {
		delwin(borderW);
		return NULL;
	}

	box(borderW, 0, 0);

	wrefresh(borderW);

	temp->border = borderW;
	temp->content = contentW;

	return temp;
}

void destroyWindow(WindowWithBorder *window) {
	if (window == NULL) return;
	if (window->border != NULL) {
		delwin(window->border);
	}
	if (window->content != NULL) {
		delwin(window->content);
	}
	free(window);
}

void updateWindow(WindowWithBorder *window) {
	wrefresh(window->border);
	wrefresh(window->content);
}

// -------- -------------- -----------

// -------- initialization -----------

void cleanup(void) {
	destroyWindow(editorWindow); // this function already checks for null
	if (fileData.fileData != NULL) {
		free(fileData.fileData);
	}
	endwin();
}

void initializeEditor(void) {
	// We have a lot of global pointers, let's set them all to null
	fileData.fileData = NULL;
	editorWindow = NULL;
	// And someone has to free all of them later
	atexit(cleanup);
}

int initUI() {
	initscr();

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

	// -------- Windows -----------
	refresh();
	editorWindow = createWindow(
		TRUE,	   // add border
		LINES - 1, // as tall as the window without status bar
		COLS / 2,  // half as wide as the window
		0,		   // start at the top
		0		   // start at the left
	);

	viewWindow = createWindow(
		TRUE,	   // add border
		LINES - 1, // as tall as the window without status bar
		COLS / 2,  // half as wide as the window
		0,		   // start at the top
		COLS / 2   // start halfway at the window horizontally
	);

	statusBar = createWindow(
		FALSE,	   // no border
		1,		   // 2 lines tall
		COLS,	   // as wide as the window
		LINES - 1, // start at the end of main windows
		0		   // start at the left
	);

	printToWindow(statusBar, "Status bar works!!\n");

	// Setup global window data
	screenData.viewStart = 20;
	screenData.viewSize = LINES - 2;
	screenData.cursorY = 0;
	screenData.cursorX = 0;

	return OK;
}

int parseArguments(int argc, char **argv) {
	// TODO: Parse arguments
	if (argc == 1) {
		printf("No file provided.\n");
		return ERR;
	}
	int fileNameLen = strlen(argv[1]);
	editorSettings.filename = malloc(fileNameLen);
	strcpy(editorSettings.filename, argv[1]);
	return OK;
}

// -------- -------------- -----------

// -------- file handling -----------

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

// -------- -------------- -----------

// -------- editing -----------
void handleCommand(char ch);

// -------- -------------- -----------

int main(int argc, char **argv) {
	parseArguments(argc, argv);

	initializeEditor();
	initUI();

	updateWindow(editorWindow);

	editorSettings.bytesPerLine = 8;

	int ch;
	while ((ch = getch()) != EditorQuit)
		;
}
