#ifndef DUMPHEADER
#define DUMPHEADER

#include <stdio.h>

// Maximum path name length on linux systems
#define MAX_FILENAME 4096

// The maximum number of bytes we are going to read at once
// if the file is bigger than this it will be read in chunks
// since we malloc for the entire file when smaller than this lets not make this value too unreasonable
#define MAX_READ_SIZE (64 * 1024)
// How many bytes will be shown per line on the printout
#define BYTESPERLINE 8

struct fileData {
	FILE *fp;
	size_t size;
};

int openFile(struct fileData *returnData, char *name);
int dumpFile(struct fileData *fd, void (*callback)(char *));

#endif
