#!/bin/bash

gcc avbtest.c -I /usr/include/alsa/ /usr/lib/arm-linux-gnueabihf/libasound.so -o avbtest
