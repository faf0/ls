all: *.c
	cc -Wall -pedantic util.c print.c ls.c -o ls -lbsd

clean:
	rm ls
