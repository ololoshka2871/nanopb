# Include the nanopb provided Makefile rules
include ../../extra/nanopb.mk

# Compiler flags to enable all warnings & debug info
CFLAGS = -ansi -Wall -Werror -g -O0
CFLAGS += -I$(NANOPB_DIR)
CFLAGS += -std=gnu99
LDFLAGS += -lprofiler

COBJS = fileproto.pb.o client.o common.o

all: client

.SUFFIXES:

clean:
	rm -f server client fileproto.pb.c fileproto.pb.h

#%: %.c common.c fileproto.pb.c
#	$(CC) $(CFLAGS) -o $@ $^ $(NANOPB_CORE)

$(COBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $^ 
	
client: $(COBJS)
	$(CC) $^ -o $@ $(NANOPB_CORE) $(LDFLAGS)