#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Maximum path name length on linux systems
#define MAX_FILENAME 4096

// The maximum number of bytes we are going to read at once
// if the file is bigger than this it will be read in chunks
// since we malloc for the entire file when smaller than this lets not make this value too unreasonable
#define MAX_READ_SIZE (64 * 1024)
#define BYTESPERLINE 8

void dumpChunk(uint8_t *chunk, size_t chunkSize, size_t startingOffset) {
	size_t offset = startingOffset / 16;
	size_t charsPerLine = BYTESPERLINE * 2;

	for (size_t i = 0; i < chunkSize; i += charsPerLine) {
		// Line number
		printf("%07zx ", offset * charsPerLine);

		// Bytes
		size_t j;
		for (j = 0; j < charsPerLine && (i + j) < chunkSize; ++j) {
			printf("%02x", chunk[i + j]);
			if (j % 2 == 1) {
				printf(" ");
			}
		}

		// If we didn't print a full line, pad with spaces
		for (; j < charsPerLine; ++j) {
			printf("  ");
			if (j % 2 == 1) {
				printf(" ");
			}
		}

		printf("| ");

		// ASCII
		for (j = 0; j < charsPerLine && (i + j) < chunkSize; ++j) {
			if (isprint(chunk[i + j])) {
				printf("%c", chunk[i + j]);
			} else {
				printf(".");
			}
		}

		printf("\n");
		offset++;
	}
}
int dumpInChunks(FILE *file, size_t filesize) {
	// Read a MAX_READ chunk
	// Process it
	// Subtract from remaining file size
	// Repeat

	size_t remaining = filesize;
	size_t offset = 0;

	uint8_t *chunkData = (uint8_t *)malloc(MAX_READ_SIZE);
	if (chunkData == NULL) {
		fprintf(stderr, "Memory allocation for file data failed.\n");
		return 1;
	}

	while (remaining > 0) {
		size_t toRead = (remaining >= MAX_READ_SIZE) ? MAX_READ_SIZE : remaining;
		size_t bytesRead = fread(chunkData, 1, toRead, file);

		if (bytesRead != toRead) {
			if (feof(file)) {
				fprintf(stderr, "Unexpected end of file\n");
			}
			if (ferror(file)) {
				fprintf(stderr, "Error reading file: %s\n", strerror(errno));
			}
			free(chunkData);
			return 1;
		}

		dumpChunk(chunkData, bytesRead, offset);
		remaining -= bytesRead;
		offset += bytesRead;
	}

	free(chunkData);

	return 0;
}

int dumpWholeFile(FILE *file, size_t filesize) {
	// Read 1 filesize sized elements
	// Since fread returns the number of elements read it should be equal to 1

	uint8_t *fileData = (uint8_t *)malloc(filesize);
	if (fileData == NULL) {
		fprintf(stderr, "Memory allocation for file data failed.\n");
		return 1;
	}

	// Reading the entire file length should place us right before eof without actually hitting it
	int readReturn = fread(fileData, filesize, 1, file);

	if (readReturn != 1) {
		fprintf(stderr, "Error reading file: ");
		if (feof(file)) {
			fprintf(stderr, "Unexpected end of file\n");
		}
		if (ferror(file)) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		free(fileData);
		return 1;
	}

	dumpChunk(fileData, filesize, 0);
	free(fileData);

	return 0;
}

int main(int argc, char **argv) {
	// Verify usage

	if (argc == 1) {
		fprintf(stderr, "No file name provided!\n");
		return 1;
	}

	// Argument parsing
	// The program currently does not have any arguments, but they will be here
	// TODO: Arguments

	// Get the file name somewhere more convenient than argv[1]

	if (strlen(argv[1]) > MAX_FILENAME - 1) {
		fprintf(stderr, "Filename exceeds maximum length of %d characters.\n", MAX_FILENAME);
		return 1;
	}
	char filename[MAX_FILENAME];
	strcpy(filename, argv[1]);

	FILE *file = fopen(filename, "rb");
	if (file == NULL) {
		fprintf(stderr, "Error opening file %s: %s\n", filename, strerror(errno));
		return 1;
	}

	// Get file metadata and do nothing with it as only care about the size

	struct stat buf;

	int fd = fileno(file);
	if (fd == -1 || fstat(fd, &buf) != 0) {
		fprintf(stderr, "Error processing file %s: %s\n", filename, strerror(errno));
		fclose(file);
		return 1;
	}
	off_t filesize = buf.st_size;

	if (filesize == 0) {
		fprintf(stderr, "Error processing file %s: File is empty\n", filename);
		fclose(file);
		return 1;
	}

	int dumpResult;

	// If the file is smaller than our max read we can read it all at once
	if (filesize > MAX_READ_SIZE) {
		dumpResult = dumpInChunks(file, filesize);
	} else {
		dumpResult = dumpWholeFile(file, filesize);
	}

	fclose(file);
	return dumpResult;
}
