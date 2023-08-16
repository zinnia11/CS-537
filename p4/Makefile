CC = gcc
CFLAGS  = -g -Wall

run: libmfs.so server mkfs main
	./mkfs -f test.img

libmfs.so: libmfs.o udp.o 
	gcc -shared -Wl,-soname,libmfs.so -o libmfs.so libmfs.o udp.o -lc	

libmfs.o: libmfs.c message.h mfs.h
	gcc -fPIC -g -c -Wall libmfs.c udp.c

server: server.c mkfs.o mfs.h udp.o udp.c udp.h message.h
	$(CC) $(CFLAGS) -o server server.c udp.c 

udp.o: udp.c udp.h
	$(CC) $(CFLAGS) -c udp.c 

mkfs: mkfs.o
	$(CC) $(CFLAGS) -o mkfs mkfs.c

mkfs.o: mkfs.c ufs.h
	$(CC) $(CFLAGS) -c mkfs.c 

main: main.c mfs.h
	$(CC) $(CCFLAGS) -o main main.c udp.c -L. -lmfs

mfscli: 
	gcc mfscli.c -o mfscli -L. -lmfs

clean:
	rm -f server mkfs main libmfs.so *.o