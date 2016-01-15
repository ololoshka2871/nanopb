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
#include <inttypes.h>
#include <assert.h>

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
	if ((end->tv_nsec - start->tv_nsec) < 0) {
		temp.tv_sec = end->tv_sec - start->tv_sec - 1;
		temp.tv_nsec = 1000000000 + end->tv_nsec - start->tv_nsec;
	} else {
		temp.tv_sec = end->tv_sec - start->tv_sec;
		temp.tv_nsec = end->tv_nsec - start->tv_nsec;
	}
	return temp;
}

static void fillTimestampStart(struct timespec* start, GenericRequest* request) {
	clock_gettime(CLOCK_REALTIME, start);
	request->timeStamp.tv_sec = start->tv_sec;
	request->timeStamp.tv_nsec = start->tv_nsec;
	request->has_timeStamp = true;
}

static bool sendRequest(pb_ostream_t* outstream, const pb_field_t fields[],
		const void *src_struct) {
	if (!pb_encode(outstream, fields, src_struct)) {
		printf("Error send request %s\n", PB_GET_ERROR(outstream));
		return false;
	}
	return true;
}

static bool readAnsver(pb_istream_t* inputStream, const pb_field_t fields[],
		void *dest_struct) {
	if (!pb_decode(inputStream, fields, dest_struct)) {
		printf("Decode failed: %s\n", PB_GET_ERROR(inputStream));
		return false;
	}
	return true;
}

static bool checkAnsver(GenericAnsver* response, int OrigId,
		GenericAnsver_ResponseType type) {
	if (response->status != GenericAnsver_Status_OK) {
		printf("Device reports error (%d)\n", response->status);
		return false;
	}
	if (response->ReqId != OrigId) {
		printf("Incorrect ansver id (%d != %d)\n", OrigId, response->ReqId);
		return false;
	}
	if (response->Type != type) {
		printf("Incorrect response type: %d, mast be %d\n", response->Type, type);
		return false;
	}

	return true;
}

static struct timespec TimePassedfrom(struct timespec *start) {
	struct timespec stop;
	clock_gettime(CLOCK_REALTIME, &stop);
	return timeDelta(start, &stop);
}

/* PING test */
bool ping_test(FILE* f, int id, bool verbose) {
	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_PING;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return false;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);

		if (!readAnsver(&input, GenericAnsver_fields, &response))
			return false;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_PONG))
			return false;

		struct timespec delta = TimePassedfrom(&start);

		if (verbose)
			printf("Success! (%u sec %u msec)", (unsigned int) delta.tv_sec,
					(unsigned int) (delta.tv_nsec / 1000000));

		if (response.has_timeStamp) {
			struct tm _tm;
			if (gmtime_r((const time_t*) &response.timeStamp, &_tm))
				if (verbose)
					printf(" processed in %d:%d.%d", _tm.tm_min, _tm.tm_sec,
							(int) (response.timeStamp.tv_nsec / 1000000));
		}
		if (verbose)
			putchar('\n');
	}

	return true;
}

static bool GetSummary(Summary *summary, FILE* f, int id, bool verbose,
		struct timespec *delta) {
	assert(summary);
	assert(f);
	assert(delta);

	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_GET_SUMMARY;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return false;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);

		if (!readAnsver(&input, GenericAnsver_fields, &response))
			return false;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_SUMMARY))
			return false;

		*delta = TimePassedfrom(&start);

		if (response.has_summary) {
			memcpy(summary, &response.summary, sizeof(Summary));

			if (verbose) {
				printf("Success! (%u sec %u msec)", (unsigned int) (delta->tv_sec),
						(unsigned int) (delta->tv_nsec / 1000000));

				if (response.has_timeStamp) {
					struct tm _tm;
					if (gmtime_r((const time_t*) &response.timeStamp, &_tm))
						printf(" processed in %d:%d.%d", _tm.tm_min, _tm.tm_sec,
								(int) (response.timeStamp.tv_nsec / 1000000));
				}
				putchar('\n');
			}

			return true;
		} else {
			return false;
		}
	}
}

/* SUMMARY test */
bool summary_test(FILE* f, int id, bool verbose) {
	Summary summary;
	struct timespec delta;

	GetSummary(&summary, f, id, verbose, &delta);
	const char *parameter_names[] = { "Name:", "Version:", "Manufacturer:" };

	if (strcmp(summary.name, "Productomer")) {
		printf("Invalid name returned: %s", summary.name);
		return false;
	}

	if (strcmp(summary.manufacturer, "OOO SCTB Elpa")) {
		printf("Invalid manufacturer returned: %s",
				summary.manufacturer);
		return false;
	}

	if (strlen(summary.version) == 0) {
		printf("Version string empty");
		return false;
	}

	if (verbose) {
		printf("-> %s = %s\n", parameter_names[0], summary.name);
		printf("-> %s = %s\n", parameter_names[1], summary.version);
		printf("-> %s = %s\n", parameter_names[2],
				summary.manufacturer);
	}
	return true;
}

bool value_test_1(FILE* f, int id, bool verbose, ValueOf valueOf) {
	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_GET_VALUE;
		request.getValue.valueOf = valueOf;
		request.has_getValue = true;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return false;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);

		if (!readAnsver(&input, GenericAnsver_fields, &response))
			return false;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_VALUE))
			return false;

		struct timespec delta = TimePassedfrom(&start);

		if (response.has_value) {

			if (response.value.valueOf != valueOf) {
				printf("Incorrect response valueOf: %d, requested %d",
						response.value.valueOf, valueOf);
				return false;
			}

			if (verbose) {
				char buff[72];
				struct tm Timestamp_dt;

				printf("Success! (%u sec %u msec)\n",
						(unsigned int) delta.tv_sec,
						(unsigned int) (delta.tv_nsec / 1000000));

				if (gmtime_r((const time_t *) &response.value.timestamp.tv_sec,
						&Timestamp_dt) == NULL)
					return false;

				strftime(buff, sizeof(buff), "%H:%M:%S", &Timestamp_dt);
				printf("Value %d returns: %f at %s.%" PRIu64 "\n",
						ValueOf_TEMPERATURE_1, response.value.Value, buff,
						response.value.timestamp.tv_nsec);
			}

			return true;
		}
		printf("Missing Value field");
		return false;
	}
}

/* VALUE test */
bool value_test(FILE* f, int id, bool verbose) {

	for (ValueOf valueof = ValueOf_TEMPERATURE_1; valueof <= ValueOf_F_T_2;
			++valueof) {
		if (verbose)
			printf("Trying value %d...\t", valueof);

		if (!value_test_1(f, id, verbose, valueof))
			return false;
	}
	return true;
}

/* VALUES test */
bool values_test(FILE* f, int id, bool verbose) {
	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_GET_VALUES;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return false;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);

		if (!readAnsver(&input, GenericAnsver_fields, &response))
			return false;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_VALUES))
			return false;

		struct timespec delta = TimePassedfrom(&start);

		if (response.has_values) {
			if (verbose) {
				char buff[72];
				struct tm Timestamp_dt;

				printf("Success! (%u sec %u msec)\n",
						(unsigned int) delta.tv_sec,
						(unsigned int) (delta.tv_nsec / 1000000));

				if (gmtime_r((const time_t *) &response.values.timestamp.tv_sec,
						&Timestamp_dt) == NULL)
					return false;

				strftime(buff, sizeof(buff), "%H:%M:%S", &Timestamp_dt);
				printf("Values:\n\t%f\n\t%f\n\t%f\n\t%f\n\tat %s.%" PRIu64 "\n",
						response.values.Temperature1,
						response.values.Temperature2, response.values.Ft1,
						response.values.Ft2, buff,
						response.values.timestamp.tv_nsec);
			}

			return true;
		}
		printf("Missing Values field");
		return false;
	}
}

static bool set_control_test1(FILE* f, int id, bool verbose, const char pattern) {
	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_SET_CONTROL;

		request.has_setControl = true;


		if (pattern & (1 << 0)) {
			request.setControl.Cooler1_state = true;
		}
		if (pattern & (1 << 1)) {
			request.setControl.Cooler2_state = true;
		}
		if (pattern & (1 << 2)) {
			request.setControl.Pelt1_state = true;
		}
		if (pattern & (1 << 3)) {
			request.setControl.Pelt2_state = true;
		}

		request.setControl.has_Cooler1_state = true;
		request.setControl.has_Cooler2_state = true;
		request.setControl.has_Pelt1_state = true;
		request.setControl.has_Pelt2_state = true;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return false;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);

		if (!readAnsver(&input, GenericAnsver_fields, &response))
			return false;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_ACCEPT))
			return false;
	}

	{
		Summary summary;
		struct timespec delta;

		GetSummary(&summary, f, id, verbose, &delta);

		uint8_t result = 0;

		if (summary.control.has_Cooler1_state)
			result |= summary.control.Cooler1_state ? (1 << 0) : 0;
		if (summary.control.has_Cooler2_state)
			result |= summary.control.Cooler2_state ? (1 << 1) : 0;
		if (summary.control.has_Pelt1_state)
			result |= summary.control.Pelt1_state ? (1 << 2) : 0;
		if (summary.control.has_Pelt2_state)
			result |= summary.control.Pelt2_state ? (1 << 3) : 0;

		if (result != pattern) {
			printf("Incorrect control result: 0x%X, must be 0x%X\n",
					result, pattern);
			return false;
		}
	}

	return true;
}

/* SET_CONTROL test */
bool set_control_test(FILE* f, int id, bool verbose) {
	const char testTable[] = { 0, 1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 0
			| 1 << 2, 1 << 1 | 1 << 3, 1 << 0 | 1 << 2 | 1 << 1 | 1 << 3 };

	for (int i = 0; i < sizeof(testTable); ++i) {
		if (verbose)
			printf("Trying pattern 0x%1X\t", testTable[i]);

		if (!set_control_test1(f, id, verbose, testTable[i]))
			return false;
	}
	return true;
}

static struct test tests[] = { { ping_test, "PING test" }, { summary_test,
		"SUMMARY test" }, { value_test, "VALUE test" }, { values_test,
		"VALUES test" }, { set_control_test, "SET_CONTROL test" } };

int main(int argc, char **argv) {
	FILE* f;
	char *dev = NULL;
	int i;
	bool verbose = false;

	if (argc > 1) {
		dev = argv[1];
		if (argc > 2 && !strcmp(argv[2], "-v")) {
			verbose = true;
		}
	} else {
		printf("USAGE: %s <file> [-v]\n", argv[0]);
		return 0;
	}

	f = fopen(dev, "w+");

	if (!f) {
		perror("dev open");
		return 1;
	}
	setvbuf(f, NULL, _IONBF, 0);

	for (i = 0; i < sizeof(tests) / sizeof(struct test); ++i) {
		printf("--- Running %s ", tests[i].desc);
		if (verbose)
			printf("---\n");
		if (tests[i].routine(f, i, verbose))
			printf("--- PASSED\n");
		else {
			printf("--- FAILED\n");
			break;
		}
	}

	/* Close connection */
	fclose(f);

	return 0;
}
