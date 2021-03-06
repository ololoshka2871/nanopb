/* Simple binding of nanopb streams to TCP sockets.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <sys/time.h>
#include <sys/select.h>

#include <stdio.h>


#include "common.h"

static bool write_callback(pb_ostream_t *stream, const uint8_t *buf, size_t count)
{
	FILE *file = (FILE*) stream->state;
	return fwrite(buf, 1, count, file) == count;
}

static bool read_callback(pb_istream_t *stream, uint8_t *buf, size_t count)
{
	FILE *file = (FILE*)stream->state;
	bool status;
	fd_set readfd;
	struct timeval timeout = {0, 1000};

	if (buf == NULL)
	{
	   while (count--)
	   {
		   timeout.tv_sec = 0;
		   timeout.tv_usec = 1000;
		   FD_ZERO(&readfd);
		   FD_SET(file->_fileno, &readfd);
		   if (select(file->_fileno + 1, &readfd, NULL, NULL, &timeout) < 0)
			   break;
		   else
			   fgetc(file);
	   }
	   return count == 0;
	}

	FD_ZERO(&readfd);
	FD_SET(file->_fileno, &readfd);
	if (select(file->_fileno + 1, &readfd, NULL, NULL, &timeout) < 0)
	{
		stream->bytes_left = 0;
		return false;
	}

	status = (fread(buf, 1, count, file) == count);

	return status;
}

pb_ostream_t pb_ostream_from_file(FILE* f)
{
    pb_ostream_t stream = {&write_callback, (void*)f, SIZE_MAX, 0};
    return stream;
}

pb_istream_t pb_istream_from_file(FILE* f)
{
    pb_istream_t stream = {&read_callback, (void*)f, SIZE_MAX};
    return stream;
}
