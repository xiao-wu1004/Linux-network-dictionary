CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
SERVER_LIBS ?= -lsqlite3 -lcrypto

COMMON_OBJS = dict_common.o

.PHONY: all server client clean run-server run-client

all: server client

server: dict_server

client: dict_client

dict_server: dict_server.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(SERVER_LIBS)

dict_client: dict_client.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

dict_server.o: dict_server.c dict_common.h
	$(CC) $(CFLAGS) -c $< -o $@

dict_client.o: dict_client.c dict_common.h
	$(CC) $(CFLAGS) -c $< -o $@

dict_common.o: dict_common.c dict_common.h
	$(CC) $(CFLAGS) -c $< -o $@

run-server: dict_server
	./dict_server 127.0.0.1 8888

run-client: dict_client
	./dict_client 127.0.0.1 8888

clean:
	rm -f dict_server dict_client *.o my.db tests/tmp/server.log
