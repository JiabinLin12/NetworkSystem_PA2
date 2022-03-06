CC = gcc
CFLAGS = -g -Wall -Werror -lrt -pthread

all: touch httpechosrv

httpechosrv: httpechosrv.c
	$(CC) $(CFLAGS) httpechosrv.c -o httpechosrv
	clear
	./httpechosrv 8888
	
touch:
	touch httpechosrv.c
clean:
	rm httpechosrv

