CC = gcc

objects = hoge6tun.o
CFLAGS = -Wall -g
LDFLAGS = -lpthread

hoge6tun : $(objects)
	$(CC) -o hoge6tun $(objects) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $*.c

.PHONY : clean
clean :
	rm $(objects) 
