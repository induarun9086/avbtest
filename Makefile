CC=$(CROSS_COMPILE)gcc

all: avbtest.c
	$(CC) -I /usr/include/alsa/ -pthread /usr/lib/arm-linux-gnueabihf/libasound.so -o avbtest avbtest.c

.PHONY: clean

clean: 
	rm avbtest
