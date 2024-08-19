#include "dump.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

int openFile(struct fileData *returnData, char *name) {
	if (strlen(name) > MAX_FILENAME - 1) {
		fprintf(stderr, "Filename exceeds maximum length of %d characters.\n", MAX_FILENAME);
		return 1;
	}

	FILE *file = fopen(name, "rb");
	if (file == NULL) {
		fprintf(stderr, "Error opening file %s: %s\n", name, strerror(errno));
		return 1;
	}

	// Get file metadata and do nothing with it as only care about the size

	struct stat buf;

	int fd = fileno(file);
	if (fd == -1 || fstat(fd, &buf) != 0) {
		fprintf(stderr, "Error processing file %s: %s\n", name, strerror(errno));
		fclose(file);
		return 1;
	}
	off_t filesize = buf.st_size;

	if (filesize == 0) {
		fprintf(stderr, "Error processing file %s: File is empty\n", name);
		fclose(file);
		return 1;
	}

	returnData->fp = file;
	returnData->size = filesize;

	return 0;
}

void dumpChunk(uint8_t *chunk, size_t chunkSize, size_t startingOffset, void (*callback)(char *)) {
	size_t offset = startingOffset / 16;
	size_t charsPerLine = BYTESPERLINE * 2;

	// For scenarios where we always print the same string (for formatting) we can pass it directly to the callback
	// function For scenatios where the printed string is dynamic (uses format) we use snprintf to pass it into the
	// proper buffer

	// Line number is always 7 digits + space + NULL
	char lineNumBuffer[9];
	// Each byte printed is just 2 characters, + NULL
	char bytesBuffer[3];
	// For ASCII characters
	char asciiBuffer[2];

	for (size_t i = 0; i < chunkSize; i += charsPerLine) {
		// Line number
		snprintf(lineNumBuffer, 9, "%07zx ", offset * charsPerLine);
		callback(lineNumBuffer);

		// Bytes
		size_t j;
		for (j = 0; j < charsPerLine && (i + j) < chunkSize; ++j) {

			snprintf(bytesBuffer, 3, "%02x", chunk[i + j]);
			callback(bytesBuffer);

			if (j % 2 == 1) {
				callback(" \0");
			}
		}

		// If we didn't print a full line, pad with spaces
		for (; j < charsPerLine; ++j) {
			callback("  \0");
			if (j % 2 == 1) {
				callback(" \0");
			}
		}

		callback("| \0");

		// ASCII
		for (j = 0; j < charsPerLine && (i + j) < chunkSize; ++j) {
			if (isprint(chunk[i + j])) {
				snprintf(asciiBuffer, 2, "%c", chunk[i + j]);
				callback(asciiBuffer);
			} else {
				callback(".\0");
			}
		}
		callback("\n\0");
		offset++;
	}
}

int dumpInChunks(FILE *file, size_t filesize, void (*callback)(char *)) {
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

		dumpChunk(chunkData, bytesRead, offset, callback);
		remaining -= bytesRead;
		offset += bytesRead;
	}

	free(chunkData);

	return 0;
}

int dumpWholeFile(FILE *file, size_t filesize, void (*callback)(char *)) {
	// Read 1 filesize sized elements
	// Since fread returns the number of elements read it should be equal to 1

	if (filesize > MAX_READ_SIZE) {
		fprintf(stderr, "Memory allocation for file failed: File too big to dump as single chunk");
		return 1;
	}

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

	dumpChunk(fileData, filesize, 0, callback);
	free(fileData);

	return 0;
}

int dumpFile(struct fileData *fd, void (*callback)(char *)) {
	// If the file is smaller than our max read we can read it all at once
	if (fd->size > MAX_READ_SIZE) {
		return dumpInChunks(fd->fp, fd->size, callback);
	} else {
		return dumpWholeFile(fd->fp, fd->size, callback);
	}
}
