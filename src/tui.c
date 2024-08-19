#include "tui.h"

#include <stdio.h>
int initUI(void) {
	printf("Window initialized\n");
	return 0;
}

void endUI(void) { printf("Window terminated\n"); }
