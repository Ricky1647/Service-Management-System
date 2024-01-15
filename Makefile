all: service

service: service.c
	gcc -o service service.c

clean:
	rm service
