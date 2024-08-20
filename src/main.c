#include "dump.h"
#include "tui.h"
#include <errno.h>
#include <locale.h>
#include <ncurses.h>
#include <string.h>

void printer(char *str) { printw("%s", str); }

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");

	if (argc == 1) {
		fprintf(stderr, "No file name provided!\n");
		return 1;
	}

	// TODO: Arguments

	if (initUI() != 0) {
		printf("Error initializing UI: %s", strerror(errno));
		return 1;
	}

	struct fileData fd;
	if (openFile(&fd, argv[1])) {
		// No error printing in here since openFile will print it's own errors to stderr
		return 1;
	}

	/* int dumpResult = dumpFile(&fd, &printer); */

	int ch;
	while (TRUE) {
		ch = getch();

		if (ch == KEY_F(1)) {
			break;
		}
	}

	fclose(fd.fp);
	endUI();

	return 0;
}
