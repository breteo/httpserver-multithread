CFLAGS=-Wall -Wextra  -Werror  -Wpedantic -Wshadow -g
CC=clang
OBJECTS=httpproxy.o queue.o
all: httpproxy

httpproxy.o	:	httpproxy.c
	$(CC) $(CFLAGS) -c httpproxy.c
queue.o	:	queue.h queue.c
	$(CC) $(CFLAGS) -c queue.c
httpproxy      :	httpproxy.o queue.o
	$(CC) -o httpproxy httpproxy.o queue.o
clean    :
	rm -f httpproxy httpproxy.o a.out queue.o
infer    :
	make clean; infer-capture -- make; infer-analyze -- make

