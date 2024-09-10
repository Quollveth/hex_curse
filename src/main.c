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
	EditorPageUp = 21, // ctrl + u
	EditorPageDown = 4 // ctrl + d
};

// -------- global state -----------

struct {
	struct termios originalTermios;
	int bytesPerLine;	// default -> 8
	int lineNumberSize; // how many characters are in the line number, default -> 4
	int linesToScroll;	// how close (in lines) the cursor will get to the screen edge before scrolling
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
struct {
	size_t viewStart;
	int cursorX, cursorY;
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
// HACK: remove debugPrint
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
	screenData.cursorY = 0;
	screenData.cursorX = 6; // 4 line numbers are always 4 characters + 2 spaces

	return OK;
}

int parseArguments(int argc, char **argv) {
	if (argc == 1) {
		printf("No file provided.\n");
		return ERR;
	}
	int fileNameLen = strlen(argv[1]);
	editorSettings.filename = malloc(fileNameLen);
	strcpy(editorSettings.filename, argv[1]);

	editorSettings.bytesPerLine = 8;
	editorSettings.lineNumberSize = 4;
	editorSettings.linesToScroll = 8;

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
	int bytesPerPage = (editorSettings.bytesPerLine * 2) * editorWindow->lines;

	switch (ch) {
	// -------- cursor movement -----------
	case EditorCursorUp:
		// cursor can't go negative
		if (screenData.cursorY == 0 && screenData.viewStart == 0) break;

		// if we're at the top scroll up the view
		if (screenData.cursorY <= editorSettings.linesToScroll) {
			if (screenData.viewStart >= (unsigned int)editorSettings.bytesPerLine * 2) {
				// scroll by how many characters are in the line, 2 per byte
				screenData.viewStart -= editorSettings.bytesPerLine * 2;
				break;
			}
		}
		screenData.cursorY--;
		break;
	case EditorCursorDown:
		// casts to unsigned int to supress comparison of different sigdness warning
		// cursorY shouldn't be negative due to boundary checkss on cursor movement
		// a negative cursorY means it is above the start of the screen
		if ((unsigned int)screenData.cursorY == editorWindow->lines) break;

		// if we're at the bottom scroll down the view
		if ((unsigned int)screenData.cursorY >= editorWindow->lines - editorSettings.linesToScroll) {
			if ((screenData.viewStart + editorSettings.bytesPerLine) >=
				fileData.filesize - editorSettings.bytesPerLine) {
				// scroll by how many characters are in the line, 2 per byte
				screenData.viewStart += editorSettings.bytesPerLine * 2;
				break;
			}
		}
		screenData.cursorY++;
		break;
	case EditorCursorLeft:
		// If we're at the left (not including line numbers) then stop
		// we need to add 2 for the spaces
		if (screenData.cursorX == editorSettings.lineNumberSize + 2) {
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
	case EditorPageUp:
		if (screenData.viewStart <= (unsigned int)bytesPerPage) {
			screenData.viewStart = 0;
			break;
		}
		screenData.viewStart -= bytesPerPage;
		break;
	case EditorPageDown:
		if (screenData.viewStart + bytesPerPage >= fileData.filesize - editorSettings.bytesPerLine) {
			screenData.viewStart = fileData.filesize - editorSettings.bytesPerLine;
			break;
		}
		screenData.viewStart += bytesPerPage;
		break;
	}

	debugPrint("view: %lu cursor: %d", screenData.viewStart, screenData.cursorY);

	wmove(editorWindow->content, screenData.cursorY, screenData.cursorX);
	wrefresh(editorWindow->content);
}

// -------- ------- -----------

// -------- UI -----------
void updateView() {
	// ensure we actually need to update
	static size_t prevStart = -1;
	// initialize to -1 so it underflows and is always different from the starting value
	// ensuring the first time opening the program will always update

	if (screenData.viewStart == prevStart) return;

	prevStart = screenData.viewStart;

	// Wipe the window and start fresh
	wclear(editorWindow->content);
	wmove(editorWindow->content, 0, 0);

	size_t startingOffset = screenData.viewStart;
	size_t fileOffset = startingOffset;

	for (unsigned int line = 0; line < editorWindow->lines; line++) {
		// if more than one line beyond the end of the file do nothing
		if (fileOffset > fileData.filesize + editorSettings.bytesPerLine) {
			printToWindow(editorWindow, "~\n");
			continue;
		}

		// line numbers
		printToWindow(editorWindow, "%04x  ", fileOffset);

		// print the line data
		// bytes per line * 2 since it's 2 characters per byte
		for (int lineOffset = 0; lineOffset < editorSettings.bytesPerLine * 2; lineOffset++) {
			// if the file doesn't fill the screen, do not print
			if (line > fileData.nLines) {
				fileOffset++;
				continue;
			}
			// we are still in the file

			// print rest of empty line
			if (fileOffset > fileData.filesize) {
				printToWindow(editorWindow, " ");
				fileOffset++;
				continue;
			}

			printToWindow(editorWindow, "%02x", fileData.fileData[fileOffset]);

			// split every 2 bytes
			if (lineOffset % 2 != 0) {
				printToWindow(editorWindow, " ");
			}
			fileOffset++;
		}
		printToWindow(editorWindow, "\n");
	}

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
