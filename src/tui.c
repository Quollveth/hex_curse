#include "tui.h"
#include <ncurses.h>

WINDOW *hexWindow, *textWindow, *statusBar;

#define WINDOWGAP 1 // Gap between windows -> columns
#define BARHEIGHT 2 // Height of the status bar -> lines

inline static void refreshAll() {
	wrefresh(hexWindow);
	wrefresh(textWindow);
	wrefresh(statusBar);
}

inline static void drawBorder(WINDOW *win) { wborder(win, 0, 0, 0, 0, 0, 0, 0, 0); }

WINDOW *createWindow(int height, int width, int yPos, int xPos, bool border) {
	WINDOW *temp = newwin(height, width, yPos, xPos);

	if (border) {
		drawBorder(temp);
	}

	wrefresh(temp);

	return temp;
}

int initUI(void) {
	initscr();
	noecho();
	raw();
	noqiflush();
	keypad(stdscr, TRUE);
	refresh();

	int halfCols = (COLS - WINDOWGAP) / 2;

	hexWindow = createWindow(
		LINES - BARHEIGHT, // nlines
		halfCols,		   // ncols
		0,				   // y
		0,				   // x
		TRUE			   // border
	);
	if (hexWindow == NULL) return ERR;

	textWindow = createWindow(
		LINES - BARHEIGHT,	  // nlines
		halfCols,			  // ncols
		0,					  // y
		halfCols + WINDOWGAP, // x
		TRUE				  // border
	);
	if (textWindow == NULL) return ERR;

	statusBar = createWindow(
		BARHEIGHT,		   // nlines
		COLS,			   // ncols
		LINES - BARHEIGHT, // y
		0,				   // x
		FALSE			   // border
	);
	if (statusBar == NULL) return ERR;

	return OK;
}

void endUI(void) { endwin(); }
