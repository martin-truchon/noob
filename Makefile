all: build run

build: ./src/noob.c
	gcc -Wall -std=c99 ./src/noob.c -o ./dist/noob

run:
	./dist/noob

clean:
	rm ./dist/noob
