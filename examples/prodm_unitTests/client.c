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

#include <google/profiler.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "fileproto.pb.h"
#include "common.h"

#define MAX_RETRYS 3

enum enError_Type {
	ERR_OK = 0, ERR_IO, ERR_UNKNOWN = 255
};

typedef enum enError_Type (*test_f)(FILE* f, int id, bool verbose);

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

static struct timespec TimePassedfrom(struct timespec *start) {
	struct timespec stop;
	clock_gettime(CLOCK_REALTIME, &stop);
	return timeDelta(start, &stop);
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
	fputc('\0', (FILE*) outstream->state);

	return true;
}

static enum enError_Type readAnsver(pb_istream_t* inputStream,
		const pb_field_t fields[], void *dest_struct) {
	if (!pb_decode(inputStream, fields, dest_struct)) {
		printf("Decode failed: %s\n", PB_GET_ERROR(inputStream));
		if (!strcmp(PB_GET_ERROR(inputStream), "io error"))
			return ERR_IO;
		return ERR_UNKNOWN;
	}
	return ERR_OK;
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
		printf("Incorrect response type: %d, mast be %d\n", response->Type,
				type);
		return false;
	}

	return true;
}

static enum enError_Type getConfirmation(FILE* f, int id) {
	enum enError_Type err;
	GenericAnsver response = { };

	pb_istream_t input = pb_istream_from_file(f);

	err = readAnsver(&input, GenericAnsver_fields, &response);
	if (err != ERR_OK)
		return err;

	if (!checkAnsver(&response, id, GenericAnsver_ResponseType_ACCEPT))
		return ERR_UNKNOWN;

	return ERR_OK;
}

static enum enError_Type GetSummary(Summary *summary, FILE* f, int id,
bool verbose, struct timespec *delta) {
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
		enum enError_Type err;

		err = readAnsver(&input, GenericAnsver_fields, &response);
		if (err != ERR_OK)
			return err;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_SUMMARY))
			return ERR_UNKNOWN;

		*delta = TimePassedfrom(&start);

		if (response.has_summary) {
			memcpy(summary, &response.summary, sizeof(Summary));

			if (verbose) {
				printf("Success! (%u sec %u msec)",
						(unsigned int) (delta->tv_sec),
						(unsigned int) (delta->tv_nsec / 1000000));

				if (response.has_timeStamp) {
					struct tm _tm;
					if (gmtime_r((const time_t*) &response.timeStamp, &_tm))
						printf(" processed in %d:%d.%d", _tm.tm_min, _tm.tm_sec,
								(int) (response.timeStamp.tv_nsec / 1000000));
				}
				putchar('\n');
			}

			return ERR_OK;
		} else {
			return ERR_UNKNOWN;
		}
	}
}

/* PING test */
enum enError_Type ping_test(FILE* f, int id, bool verbose) {
	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_PING;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return ERR_UNKNOWN;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);
		enum enError_Type err;

		err = readAnsver(&input, GenericAnsver_fields, &response);
		if (err != ERR_OK)
			return err;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_PONG))
			return ERR_UNKNOWN;

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

	return ERR_OK;
}

/* SUMMARY test */
enum enError_Type summary_test(FILE* f, int id, bool verbose) {
	Summary summary;
	struct timespec delta;

	enum enError_Type err = GetSummary(&summary, f, id, verbose, &delta);
	if (err != ERR_OK)
		return err;

	const char *parameter_names[] = { "Name:", "Version:", "Manufacturer:" };

	if (strcmp(summary.name, "Productomer")) {
		printf("Invalid name returned: %s", summary.name);
		return ERR_UNKNOWN;
	}

	if (strcmp(summary.manufacturer, "OOO SCTB Elpa")) {
		printf("Invalid manufacturer returned: %s", summary.manufacturer);
		return ERR_UNKNOWN;
	}

	if (strlen(summary.version) == 0) {
		printf("Version string empty");
		return ERR_UNKNOWN;
	}

	if (verbose) {
		printf("-> %s = %s\n", parameter_names[0], summary.name);
		printf("-> %s = %s\n", parameter_names[1], summary.version);
		printf("-> %s = %s\n", parameter_names[2], summary.manufacturer);
	}
	return ERR_OK;
}

enum enError_Type value_test_1(FILE* f, int id, bool verbose, ValueOf valueOf) {
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
			return ERR_UNKNOWN;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);
		enum enError_Type err;

		err = readAnsver(&input, GenericAnsver_fields, &response);
		if (err != ERR_OK)
			return err;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_VALUE))
			return ERR_UNKNOWN;

		struct timespec delta = TimePassedfrom(&start);

		if (response.has_value) {

			if (response.value.valueOf != valueOf) {
				printf("Incorrect response valueOf: %d, requested %d",
						response.value.valueOf, valueOf);
				return ERR_UNKNOWN;
			}

			if (verbose) {
				char buff[72];
				struct tm Timestamp_dt;

				printf("Success! (%u sec %u msec)\n",
						(unsigned int) delta.tv_sec,
						(unsigned int) (delta.tv_nsec / 1000000));

				if (gmtime_r((const time_t *) &response.value.timestamp.tv_sec,
						&Timestamp_dt) == NULL)
					return ERR_UNKNOWN;

				strftime(buff, sizeof(buff), "%H:%M:%S", &Timestamp_dt);
				printf("Value %d returns: %f at %s.%" PRIu64 "\n",
						ValueOf_TEMPERATURE_1, response.value.Value, buff,
						response.value.timestamp.tv_nsec);
			}

			return ERR_OK;
		}
		printf("Missing Value field");
		return ERR_UNKNOWN;
	}
}

/* VALUE test */
enum enError_Type value_test(FILE* f, int id, bool verbose) {

	enum enError_Type err;

	for (ValueOf valueof = ValueOf_TEMPERATURE_1; valueof <= ValueOf_F_T_2;
			++valueof) {
		if (verbose)
			printf("Trying value %d...\t", valueof);

		err = value_test_1(f, id, verbose, valueof);
		if (err != ERR_OK)
			return err;
	}
	return ERR_OK;
}

/* VALUES test */
enum enError_Type values_test(FILE* f, int id, bool verbose) {
	struct timespec start;
	{
		GenericRequest request = { };
		pb_ostream_t output = pb_ostream_from_file(f);
		request.ReqId = id;
		request.Type = GenericRequest_RequestType_GET_VALUES;

		fillTimestampStart(&start, &request);
		if (!sendRequest(&output, GenericRequest_fields, &request))
			return ERR_UNKNOWN;
	}

	{
		GenericAnsver response = { };

		pb_istream_t input = pb_istream_from_file(f);
		enum enError_Type err;

		err = readAnsver(&input, GenericAnsver_fields, &response);
		if (err != ERR_OK)
			return err;

		if (!checkAnsver(&response, id, GenericAnsver_ResponseType_VALUES))
			return ERR_UNKNOWN;

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
					return ERR_UNKNOWN;

				strftime(buff, sizeof(buff), "%H:%M:%S", &Timestamp_dt);
				printf("Values:\n\t%f\n\t%f\n\t%f\n\t%f\n\tat %s.%" PRIu64 "\n",
						response.values.Temperature1,
						response.values.Temperature2, response.values.Ft1,
						response.values.Ft2, buff,
						response.values.timestamp.tv_nsec);
			}

			return ERR_OK;
		}
		printf("Missing Values field");
		return ERR_UNKNOWN;
	}
}

static enum enError_Type set_control_test1(FILE* f, int id, bool verbose,
		const char pattern) {
	struct timespec start;
	enum enError_Type err;
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
			return ERR_UNKNOWN;
	}

	err = getConfirmation(f, id);
	if (err != ERR_OK)
		return err;

	{
		Summary summary;
		struct timespec delta;

		err = GetSummary(&summary, f, id + 1, verbose, &delta);
		if (err != ERR_OK)
			return err;

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
			printf("Incorrect control result: 0x%X, must be 0x%X\n", result,
					pattern);
			return ERR_UNKNOWN;
		}
	}

	return ERR_OK;
}

/* SET_CONTROL test */
enum enError_Type set_control_test(FILE* f, int id, bool verbose) {
	const char testTable[] = { 0, 1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 0
			| 1 << 2, 1 << 1 | 1 << 3, 1 << 0 | 1 << 2 | 1 << 1 | 1 << 3 };

	enum enError_Type err;
	int retrys = MAX_RETRYS;

	for (int i = 0; i < sizeof(testTable) / sizeof(char); ++i) {
		if (verbose)
			printf("Trying pattern 0x%1X\t", testTable[i]);
		while (true) {
			err = set_control_test1(f, id, verbose, testTable[i]);

			switch (err) {
			case ERR_OK:
				retrys = MAX_RETRYS;
				break;
			case ERR_IO:

				usleep(10000);
				fflush(f);

				retrys--;
				if (!retrys)
					return ERR_IO;

				continue;
			default:
				return err;
			}
			break;
		}
	}
	return ERR_OK;
}

/////////////////////////////////////////////////////////////////

static void set_Settings_Temperature1MesureTime(GenericRequest* r,
		uint16_t value) {
	r->has_setSettings = true;
	r->setSettings.has_Temperature1MesureTime = true;
	r->setSettings.Temperature1MesureTime = value;
}

static void set_Settings_Temperature2MesureTime(GenericRequest* r,
		uint16_t value) {
	r->has_setSettings = true;
	r->setSettings.has_Temperature2MesureTime = true;
	r->setSettings.Temperature2MesureTime = value;
}

static void set_Settings_CPU_Speed(GenericRequest* r, float value) {
	r->has_setSettings = true;
	r->setSettings.has_CpuSpeed = true;
	r->setSettings.CpuSpeed = value;
}

static void set_Settings_CoeffsT1(GenericRequest* r, const T_Coeffs *coeffs) {
	r->has_setSettings = true;
	r->setSettings.has_CoeffsT1 = true;
	r->setSettings.CoeffsT1 = *coeffs;
}

static void set_Settings_CoeffsT2(GenericRequest* r, const T_Coeffs *coeffs) {
	r->has_setSettings = true;
	r->setSettings.has_CoeffsT2 = true;
	r->setSettings.CoeffsT2 = *coeffs;
}

static void set_Settings_Clock(GenericRequest* r) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	// http://stackoverflow.com/questions/11127538/c-clock-gettime-and-daylight-savings
	struct tm tmv;
	localtime_r(&ts.tv_sec, &tmv);
	ts.tv_sec = mktime(&tmv);

	r->has_setSettings = true;
	r->setSettings.has_Clock = true;
	r->setSettings.Clock.tv_sec = ts.tv_sec;
	r->setSettings.Clock.tv_nsec = ts.tv_nsec;
}

/////////////////////////////////////////////////////////////////

static enum enError_Type verify_Settings_Temperature1MesureTime(
		Summary* summary, uint16_t value) {
	if (!summary->settings.has_Temperature1MesureTime) {
		printf("No field \"Temperature1MesureTime\" in ansver");
		return ERR_UNKNOWN;
	}
	if (summary->settings.Temperature1MesureTime != value) {
		printf("Incorrect value of \"Temperature1MesureTime\"");
		return ERR_UNKNOWN;
	}
	return ERR_OK;
}

static enum enError_Type verify_Settings_Temperature2MesureTime(
		Summary* summary, const uint16_t value) {
	if (!summary->settings.has_Temperature2MesureTime) {
		printf("No field \"Temperature2MesureTime\" in ansver");
		return ERR_UNKNOWN;
	}
	if (summary->settings.Temperature2MesureTime != value) {
		printf("Incorrect value of \"Temperature2MesureTime\"");
		return ERR_UNKNOWN;
	}
	return ERR_OK;
}

static enum enError_Type verify_Settings_CPU_Speed(Summary* summary,
		float value) {
	if (!summary->settings.has_CpuSpeed) {
		printf("No field \"CpuSpeed\" in ansver");
		return ERR_UNKNOWN;
	}
	if (summary->settings.CpuSpeed != value) {
		printf("Incorrect value of \"CpuSpeed\"");
		return ERR_UNKNOWN;
	}
	return ERR_OK;
}

static enum enError_Type verify_Settings_CoeffsT1(Summary* summary,
		const T_Coeffs* value) {
	if (!summary->settings.has_CoeffsT1) {
		printf("No field \"CoeffsT1\" in ansver");
		return ERR_UNKNOWN;
	}
	if (memcmp(&summary->settings.CoeffsT1, value, sizeof(T_Coeffs))) {
		printf("Incorrect value of \"CoeffsT1\"");
		return ERR_UNKNOWN;
	}
	return ERR_OK;
}

static enum enError_Type verify_Settings_CoeffsT2(Summary* summary,
		const T_Coeffs* value) {
	if (!summary->settings.has_CoeffsT2) {
		printf("No field \"CoeffsT2\" in ansver");
		return ERR_UNKNOWN;
	}
	if (memcmp(&summary->settings.CoeffsT2, value, sizeof(T_Coeffs))) {
		printf("Incorrect value of \"CoeffsT2\"");
		return ERR_UNKNOWN;
	}
	return ERR_OK;
}

static enum enError_Type verify_Settings_Clock(Summary* summary,
		TimeStamp * clockWas) {
	if (!summary->settings.has_Clock) {
		printf("No field \"Clock\" in ansver");
		return ERR_UNKNOWN;
	}
	struct timespec new_date = { summary->settings.Clock.tv_sec,
			summary->settings.Clock.tv_nsec };
	struct timespec old_date = { clockWas->tv_sec, clockWas->tv_nsec };
	struct timespec delta = timeDelta(&old_date, &new_date);

	if (delta.tv_sec < 2)
		return ERR_OK;
	else {
		struct tm _tm;
		if (gmtime_r(&delta.tv_sec, &_tm))
			printf("Incorrect value of device clock: %d.%d.%d %d:%d:%d.%lu",
					_tm.tm_year, _tm.tm_mon, _tm.tm_mday, _tm.tm_hour,
					_tm.tm_min, _tm.tm_sec, delta.tv_nsec);
		return ERR_UNKNOWN;
	}
}

/////////////////////////////////////////////////////////////////

static enum enError_Type test_settings1(FILE* f, int id, bool verbose,
		const Settings* value) {
	assert(f);
	assert(value);

	struct timespec start;
	GenericRequest request = { };
	pb_ostream_t output = pb_ostream_from_file(f);
	request.ReqId = id;
	request.Type = GenericRequest_RequestType_SET_SETTINGS;

	if (value->has_Clock) {
		set_Settings_Clock(&request);
	}
	if (value->has_CoeffsT1)
		set_Settings_CoeffsT1(&request, &value->CoeffsT1);
	if (value->has_CoeffsT2)
		set_Settings_CoeffsT2(&request, &value->CoeffsT2);
	if (value->has_CpuSpeed)
		set_Settings_CPU_Speed(&request, value->CpuSpeed);
	if (value->has_Temperature1MesureTime)
		set_Settings_Temperature1MesureTime(&request,
				value->Temperature1MesureTime);
	if (value->has_Temperature2MesureTime)
		set_Settings_Temperature2MesureTime(&request,
				value->Temperature2MesureTime);

	fillTimestampStart(&start, &request);
	if (!sendRequest(&output, GenericRequest_fields, &request))
		return ERR_UNKNOWN;

	enum enError_Type err;

	//usleep(10000);

	err = getConfirmation(f, id);
	if (err != ERR_OK) {
		// empty request test, mast return error
		if (err != ERR_IO && !request.has_setSettings) {
			if (verbose)
				printf(" --- OK\n");
			return ERR_OK;
		} else
			return err;
	}

	usleep(10000);

	{
		Summary summary;
		struct timespec delta;

		err = GetSummary(&summary, f, id << 2, verbose, &delta);
		if (err != ERR_OK)
			return err;

		if (value->has_Clock) {
			err = verify_Settings_Clock(&summary, &request.setSettings.Clock);
			if (err)
				return err;
		}
		if (value->has_CoeffsT1) {
			err = verify_Settings_CoeffsT1(&summary, &value->CoeffsT1);
			if (err)
				return err;
		}
		if (value->has_CoeffsT2) {
			err = verify_Settings_CoeffsT2(&summary, &value->CoeffsT2);
			if (err)
				return err;
		}
		if (value->has_CpuSpeed) {
			err = verify_Settings_CPU_Speed(&summary, value->CpuSpeed);
			if (err)
				return err;
		}
		if (value->has_Temperature1MesureTime) {
			err = verify_Settings_Temperature1MesureTime(&summary,
					value->Temperature1MesureTime);
			if (err)
				return err;
		}
		if (value->has_Temperature2MesureTime) {
			err = verify_Settings_Temperature2MesureTime(&summary,
					value->Temperature2MesureTime);
			if (err)
				return err;
		}
	}
	return ERR_OK;
}

#define temperature_MT1 1000
#define temperature_MT2 995
#define temperature_MT3 100

#define CPU_SPEED1 		.has_CpuSpeed = true, .CpuSpeed = 16000000.5
#define CPU_SPEED2 		.has_CpuSpeed = true, .CpuSpeed = 15999999.3
#define CPU_SPEED3 		.has_CpuSpeed = true, .CpuSpeed = 16000000

#define TCOEFFS1		{.T0 = 10, .C1 = 5, .C2 = 3.5, .C3 = 1e-7, .F0 = 32761.53}
#define TCOEFFS2		{.T0 = -23.5, .C1 = 0.1, .C2 = 6e-3, .C3 = 1.75e-8, .F0 = 32758.72}
#define TCOEFFS3		{.T0 = 0, .C1 = 1, .C2 = 0, .C3 = 0, .F0 = 0}

enum enError_Type SettingsSet_test(FILE* f, int id,
bool verbose) {

	static const Settings settings_test_Matrix[] = { { },
			{ .has_Clock = true, }, { .has_Temperature1MesureTime = true,
					.Temperature1MesureTime =
					temperature_MT1 }, { .has_Temperature2MesureTime = true,
					.Temperature2MesureTime =
					temperature_MT2 }, {
			CPU_SPEED1 }, { .has_CoeffsT1 = true, .CoeffsT1 =
			TCOEFFS1 }, { .has_CoeffsT2 =
			true, .CoeffsT2 =
			TCOEFFS2 },

			{ .has_CoeffsT1 = true, .Temperature1MesureTime =
			temperature_MT2, .has_CoeffsT2 =
			true, .Temperature1MesureTime =
			temperature_MT3, }, {
			CPU_SPEED1, .has_Clock = true }, { .has_CoeffsT1 = true, .CoeffsT1 =
			TCOEFFS2, .has_CoeffsT2 = true, .CoeffsT2 =
			TCOEFFS1 }, { .has_CoeffsT1 = true, .Temperature1MesureTime =
			temperature_MT3, .has_CoeffsT2 =
			true, .Temperature2MesureTime =
			temperature_MT3, .has_Clock =
			true,
			CPU_SPEED3, .has_CoeffsT1 = true, .CoeffsT1 =
			TCOEFFS3, .has_CoeffsT2 =
			true, .CoeffsT2 = TCOEFFS3, }, };

	enum enError_Type err;
	int retrys = MAX_RETRYS;
	int i;

	for (i = 0; i < sizeof(settings_test_Matrix) / sizeof(Settings); ++i) {
		if (verbose)
			printf("Settings set test #%d\t", i);
		while (true) {
			err = test_settings1(f, id + i, verbose, &settings_test_Matrix[i]);

			switch (err) {
			case ERR_OK:
				retrys = MAX_RETRYS;
				break;
			case ERR_IO:

				usleep(10000);
				fflush(f);

				retrys--;
				if (!retrys)
					return ERR_IO;

				continue;
			default:
				return err;
			}
			break;
		}
	}
	return ERR_OK;
}

static struct test tests[] = { { ping_test, "PING test" }, { summary_test,
		"SUMMARY test" }, { value_test, "VALUE test" }, { values_test,
		"VALUES test" }, { set_control_test, "SET_CONTROL test" }, {
		SettingsSet_test, "SET_SETTINGS" } };

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

	enum enError_Type err;
	int retrys = MAX_RETRYS;

	ProfilerStart("tests");

	for (i = 0; i < sizeof(tests) / sizeof(struct test); ++i) {
		printf("--- Running %s ", tests[i].desc);
		if (verbose)
			printf("---\n");
		while (true) {
			err = tests[i].routine(f, i, verbose);

			switch (err) {
			case ERR_OK:
				printf(" --- PASSED\n");
				retrys = MAX_RETRYS;
				break;
			case ERR_IO:
				retrys--;
				if (!retrys) {
					printf(" --- IO ERRORS, STOP ---");
					goto __FAIL;
				}

				usleep(10000);
				fflush(f);

				if (verbose)
					printf(" --- IO ERROR, retry (%d)\n", retrys);

				continue;
			default:
				printf(" --- FAILED (%d)\n", err);
				goto __FAIL;
			}
			break;
		}
	}
	__FAIL: ProfilerStop();

	putchar('\n');
	/* Close connection */
	fclose(f);

	return 0;
}
