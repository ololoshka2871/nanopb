/*
 * latreader.c
 *
 *  Created on: 27 янв. 2016 г.
 *      Author: tolyan
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

static int awaitChar(FILE* f, int *c) {
	fd_set readfd;
	struct timeval timeout = { 100, 0 };

	FD_ZERO(&readfd);
	FD_SET(f->_fileno, &readfd);
	int r = select(f->_fileno + 1, &readfd, NULL, NULL, &timeout);
	if (r > 0) {
		fread(c, 1, 1, f);
		return 1;
	} else {
		return -1;
	}
}

static struct timespec getTimestamp() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts;
}

int main(int argc, char **argv) {
	FILE* f;
	char* dev = NULL;
	int c;

	if (argc > 1) {
		dev = argv[1];
	} else {
		printf("USAGE: %s <file>\n", argv[0]);
		return 0;
	}

	f = fopen(dev, "r");

	if (!f) {
		perror("dev open");
		return 1;
	}
	setvbuf(f, NULL, _IONBF, 0);

	while (awaitChar(f, &c) > 0) {
		struct timespec ts = getTimestamp();
		printf("%02X;%d;%lu\n", c, (int)ts.tv_sec, ts.tv_nsec);
	}

	/* Close connection */
	fclose(f);

	return 0;
}
