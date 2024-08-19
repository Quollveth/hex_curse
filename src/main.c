#include "dump.h"
#include "tui.h"
#include <errno.h>
#include <string.h>

// Initializes a ncurses window with all required setup
// Returns success status and sets returnWin pointer

void printer(char *str) { printf("%s", str); }

int main(int argc, char **argv) {
	// Verify usage

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

	int dumpResult = dumpFile(&fd, &printer);

	getch();
	fclose(fd.fp);
	endUI();

	return dumpResult;
}
