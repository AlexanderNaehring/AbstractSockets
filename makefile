all: server client
server: ASLib.o server.c
	gcc -pthread server.c ASLib.o -o server
client: ASLib.o client.c
	gcc -pthread client.c ASLib.o -o client
ASLib.o: ASLib.c ASLib.h
	gcc -pthread ASLib.c -c
