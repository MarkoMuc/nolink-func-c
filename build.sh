#!/bin/bash

gcc -c -o obj/obj.o obj/obj.c
gcc -o bin/loader src/loader.c
./bin/loader
