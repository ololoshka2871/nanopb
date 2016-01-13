/* This is a simple TCP client that connects to port 1234 and prints a list
 * of files in a given directory.
 *
 * It directly deserializes and serializes messages from network, minimizing
 * memory use.
 * 
 * For flexibility, this example is implemented using posix api.
 * In a real embedded system you would typically use some other kind of
 * a communication and filesystem layer.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "fileproto.pb.h"
#include "common.h"

typedef bool (*test_f)(FILE* f, int id, bool verbose);

struct test {
	test_f routine;
	char* desc;
};

struct timespec timeDelta(struct timespec *start, struct timespec *end) {
	struct timespec temp;
	if ((end->tv_nsec-start->tv_nsec)<0) {
		temp.tv_sec = end->tv_sec-start->tv_sec-1;
		temp.tv_nsec = 1000000000+end->tv_nsec-start->tv_nsec;
	} else {
		temp.tv_sec = end->tv_sec-start->tv_sec;
		temp.tv_nsec = end->tv_nsec-start->tv_nsec;
	}
	return temp;
}

/* PING test */
bool ping_test(FILE* f, int id, bool verbose)
{
	struct timespec start;
	{
		GenericRequest request = {};
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_PING;
		request.has_timeStamp = true;

		clock_gettime(CLOCK_REALTIME, &start);

		request.timeStamp.tv_sec = start.tv_sec;
		request.timeStamp.tv_nsec = start.tv_nsec;

		if (!pb_encode(&output, GenericRequest_fields, &request))
		{
			printf("Error send request %s\n", PB_GET_ERROR(&output));
			return false;
		}
	}

	{
		GenericAnsver response = {};

		pb_istream_t input = pb_istream_from_file(f);

		if (!pb_decode(&input, GenericAnsver_fields, &response))
		{
			printf("Decode failed: %s\n", PB_GET_ERROR(&input));
			return false;
		}

		if (response.ReqId != id)
		{
			printf("Incorrect ansver id (%d != %d)\n", id, response.ReqId);
			return false;
		}

		if (response.status != GenericAnsver_Status_OK)
		{
			printf("Device reports error (%d)\n", response.status);
			return false;
		}

		struct timespec stop;
		clock_gettime(CLOCK_REALTIME, &stop);
		struct timespec delta = timeDelta(&start, &stop);

		if (verbose)
			printf("Success! (%u sec %u msec)",
				(unsigned int)delta.tv_sec, (unsigned int)(delta.tv_nsec / 1000000));

		if (response.has_timeStamp)
		{
			struct tm _tm;
			if (gmtime_r((const time_t*) &response.timeStamp, &_tm))
				if (verbose)
					printf(" processed in %d:%d.%d", _tm.tm_min, _tm.tm_sec,
						(int)(response.timeStamp.tv_nsec / 1000000));
		}
		if (verbose)
			putchar('\n');
	}

    return true;
}

/* SUMMARY test */
bool summary_test(FILE* f, int id, bool verbose) {
	struct timespec start;
	{
		GenericRequest request = {};
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_GET_SUMMARY;
		request.has_timeStamp = true;

		clock_gettime(CLOCK_REALTIME, &start);

		request.timeStamp.tv_sec = start.tv_sec;
		request.timeStamp.tv_nsec = start.tv_nsec;

		if (!pb_encode(&output, GenericRequest_fields, &request))
		{
			printf("Error send request %s\n", PB_GET_ERROR(&output));
			return false;
		}
	}

	{
		GenericAnsver response = {};

		pb_istream_t input = pb_istream_from_file(f);

		if (!pb_decode(&input, GenericAnsver_fields, &response))
		{
			printf("Decode failed: %s\n", PB_GET_ERROR(&input));
			return false;
		}

		if (response.ReqId != id)
		{
			printf("Incorrect ansver id (%d != %d)\n", id, response.ReqId);
			return false;
		}

		if (response.status != GenericAnsver_Status_OK)
		{
			printf("Device reports error (%d)\n", response.status);
			return false;
		}

		struct timespec stop;
		clock_gettime(CLOCK_REALTIME, &stop);
		struct timespec delta = timeDelta(&start, &stop);

		if (response.has_summary)
		{
			const char *parameter_names[] = {
				"Name:",
				"Version:",
				"Manufacturer:"
			};

			if (strcmp(response.summary.name, "Productomer")) {
				printf("Invalid name returned: %s", response.summary.name);
				return false;
			}

			if (strcmp(response.summary.manufacturer, "OOO SCTB Elpa")) {
				printf("Invalid manufacturer returned: %s", response.summary.manufacturer);
				return false;
			}

			if (strlen(response.summary.version) == 0) {
				printf("Version string empty");
				return false;
			}

			if (verbose) {
				printf("-> %s = %s\n", parameter_names[0], response.summary.name);
				printf("-> %s = %s\n", parameter_names[1], response.summary.version);
				printf("-> %s = %s\n", parameter_names[2], response.summary.manufacturer);

				printf("Success! (%u sec %u msec)",
					(unsigned int)delta.tv_sec, (unsigned int)(delta.tv_nsec / 1000000));

				if (response.has_timeStamp)
				{
					struct tm _tm;
					if (gmtime_r((const time_t*) &response.timeStamp, &_tm))
						printf(" processed in %d:%d.%d", _tm.tm_min, _tm.tm_sec,
								(int)(response.timeStamp.tv_nsec / 1000000));
				}
				putchar('\n');
			}
			return true;
		} else {
			return false;
		}
	}
}

static struct test tests[] = {
	{ ping_test, "PING test" },
	{ summary_test, "SUMMARY test" }
};

int main(int argc, char **argv)
{
    FILE* f;
    char *dev = NULL;
    int i;
    bool verbose = false;
    
    if (argc > 1)
    {
        dev = argv[1];
        if (argc > 2 && !strcmp(argv[2], "-v")) {
        	verbose = true;
        }
    }
    else
    {
    	printf("USAGE: %s <file> [-v]\n", argv[0]);
    	return 0;
    }
    
    f = fopen(dev, "w+");

    if (!f)
    {
        perror("dev open");
        return 1;
    }
    setvbuf(f, NULL, _IONBF, 0);
    
    for (i = 0; i < sizeof(tests) / sizeof(struct test); ++i)
    {
    	printf("--- Running %s ", tests[i].desc);
    	if (verbose)
    		printf("---\n");
    	if (tests[i].routine(f, i, verbose))
    		printf("--- PASSED\n");
    	else
    		printf("--- FAILED\n");
    }
    
    /* Close connection */
    fclose(f);
    
    return 0;
}
