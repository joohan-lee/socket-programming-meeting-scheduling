CC = gcc
CFLAGS = -std=gnu11 -g

all: serverM serverA serverB client

serverM: serverM.c
	$(CC) $(CFLAGS) serverM.c -o serverM

serverA: serverA.c
	$(CC) $(CFLAGS) serverA.c -o serverA

serverB: serverB.c
	$(CC) $(CFLAGS) serverB.c -o serverB

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f serverM serverA serverB client
