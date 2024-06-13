all: tars.so

tars.so: libtars.c
	gcc $^ -o $@ -fPIC -shared -g -Wall

r: all
	lua run.lua

clean:
	rm tars.so

.PHONE: all r clean
