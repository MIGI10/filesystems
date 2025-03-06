all: fsutils
ext2.o: ext/ext2.c
	gcc -g -c -Wall -Wextra ext/ext2.c -o ext2.o
fat16.o: fat/fat16.c
	gcc -g -c -Wall -Wextra fat/fat16.c -o fat16.o
fsutils: fsutils.c ext2.o fat16.o
	gcc -g -Wall -Wextra fsutils.c ext2.o fat16.o -o fsutils
	rm -rf *.o