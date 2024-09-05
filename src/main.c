#include <curses.h>
#include <ncurses.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <tgmath.h>
#include <time.h>
#include <unistd.h>

enum editorKeys {
	EditorQuit = KEY_F(1),
	EditorCursorUp = 'k',
	EditorCursorDown = 'j',
	EditorCursorLeft = 'h',
	EditorCursorRight = 'l',
};

// -------- global state -----------

// Passed as commandline arguments
struct {
	struct termios originalTermios;
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
	unsigned int cols, lines;
} WindowWithBorder;

// editor -> where the editing happens
// view -> ascii representation of hex
// status -> shows information
WindowWithBorder *editorWindow, *viewWindow, *statusBar;

//
// TODO: remove hacks
void debugPrint(const char *fmt, ...) {
	wclear(statusBar->content);
	va_list argp;
	va_start(argp, fmt);
	vw_printw(statusBar->content, fmt, argp);
	va_end(argp);
	wrefresh(statusBar->content);
}
//

// -------- -------------- -----------

// we have oop at home
// -------- window methods -----------

void printToWindow(WindowWithBorder *window, const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	vw_printw(window->content, fmt, argp);
	va_end(argp);
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
	temp->lines = lines - 2;
	temp->cols = cols - 2;

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

// -------- -------------- -----------

// -------- initialization -----------

void cleanup(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &editorSettings.originalTermios); // reset the terminal
	destroyWindow(editorWindow);									   // this function already checks for null
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

	editorSettings.originalTermios = term;

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
	if (editorWindow == NULL) return ERR;

	viewWindow = createWindow(
		TRUE,	   // add border
		LINES - 1, // as tall as the window without status bar
		COLS / 2,  // half as wide as the window
		0,		   // start at the top
		COLS / 2   // start halfway at the window horizontally
	);
	if (viewWindow == NULL) {
		destroyWindow(editorWindow);
		return ERR;
	}

	statusBar = createWindow(
		FALSE,	   // no border
		1,		   // 2 lines tall
		COLS,	   // as wide as the window
		LINES - 1, // start at the end of main windows
		0		   // start at the left
	);
	if (statusBar == NULL) {
		destroyWindow(editorWindow);
		destroyWindow(viewWindow);
		return ERR;
	}

	// Setup global window data
	screenData.viewStart = 0;
	screenData.viewSize = LINES - 3; // 1 for the status bar and 2 for the border
	screenData.cursorY = 0;
	screenData.cursorX = 6; // 4 line numbers are always 4 characters + 2 spaces

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

	editorSettings.bytesPerLine = 32;

	return OK;
}

// -------- -------------- -----------

// -------- file handling -----------

int dumpFile(char *filename) {
	FILE *fp = fopen(filename, "rb");

	if (fp == NULL) return ERR;

	struct stat st;

	if (fstat(fileno(fp), &st) == ERR) return ERR;

	fileData.filesize = st.st_size;
	fileData.nLines = fileData.filesize / editorSettings.bytesPerLine;

	// -------- read file -----------

	fileData.fileData = (uint8_t *)malloc(fileData.filesize);
	if (fileData.fileData == NULL) {
		free(fileData.fileData);
		fileData.fileData = NULL;
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
void handleCommand(char ch) {
	switch (ch) {

	// -------- cursor movement -----------
	case EditorCursorUp:
		// if we're at the top scroll up the view
		if (screenData.cursorY == 0) {
			// if there's no more view to scroll do nothing
			if (screenData.viewStart == 0) {
				break;
			}
			// TODO: scroll view
			break;
		}
		screenData.cursorY--;
		break;
	case EditorCursorDown:
		// if we're at the bottom scroll down the view
		// 2 for the borders and 1 for the status bar and 1 for the off by one error
		if (screenData.cursorY == LINES - 4) {
			// if there's no more view to scroll do nothing
			if (screenData.viewStart + screenData.viewSize == fileData.nLines - 1) {
				break;
			}
			// TODO: scroll view
			break;
		}
		screenData.cursorY++;
		break;
	case EditorCursorLeft:
		// If we're at the left (not including line numbers) then stop
		// line numbers are always 4 characters followed by 2 spaces
		if (screenData.cursorX == 6) {
			break;
		}
		screenData.cursorX--;
		break;
	case EditorCursorRight:
		// if we're at the right stop
		// editor window is half the screen - 2 for the borders
		if (screenData.cursorX == (COLS / 2) - 2) {
			break;
		}
		screenData.cursorX++;
		break;

	// -------------------
	case 'u':
		screenData.viewStart -= editorSettings.bytesPerLine;
		break;
	case 'd':
		screenData.viewStart += editorSettings.bytesPerLine;
		break;

	// -------------------
	default:
		break;
	}

	debugPrint("view starts at %d and ends at %d", screenData.viewStart, screenData.viewStart + screenData.viewSize);

	wmove(editorWindow->content, screenData.cursorY, screenData.cursorX);
	wrefresh(editorWindow->content);
}

// -------- ------- -----------

// -------- UI -----------
void updateView() {
	// ensure we actually need to update
	static size_t prevStart = 0;
	static int prevSize = 0;

	if (screenData.viewStart == prevStart && screenData.viewSize == prevSize) return;

	prevStart = screenData.viewStart;
	prevSize = screenData.viewSize;

	// Wipe the window and start fresh
	wclear(editorWindow->content);
	wmove(editorWindow->content, 0, 0);

	size_t startingOffset = screenData.viewStart;

	// clang-format off
	
	

	//clang-format on
	wmove(editorWindow->content, screenData.cursorY, screenData.cursorX);
	wrefresh(editorWindow->content);
}
// -------- -- -----------

// -------- Entrypoint -----------

int main(int argc, char **argv) {
	parseArguments(argc, argv);

	initializeEditor();
	initUI();

	dumpFile(editorSettings.filename);
	updateView();

	handleCommand(0);
	int ch;
	while ((ch = getch()) != EditorQuit) {
		handleCommand(ch);
		updateView();
	}
}
