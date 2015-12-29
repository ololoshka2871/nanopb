#ifndef _PB_EXAMPLE_COMMON_H_
#define _PB_EXAMPLE_COMMON_H_

#include <pb.h>

pb_ostream_t pb_ostream_from_file(FILE* f);
pb_istream_t pb_istream_from_file(FILE* f);

#endif
