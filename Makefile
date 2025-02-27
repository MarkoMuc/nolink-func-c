.PHONY: clean

run: bin/loader
	./bin/loader

bin/loader: src/loader.c bin/obj.o
	gcc -o bin/loader src/loader.c

bin/obj.o: obj/obj.c
	gcc -c -o bin/obj.o obj/obj.c

clean: bin/*
	rm -f bin/*
