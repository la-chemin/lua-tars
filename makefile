all: tars.so

tars.so: libtars.c
	g++ $^ -o $@ -fPIC -shared -g -Wall

r: all
	lua run.lua

clean:
	rm tars.so

.PHONE: all r clean
