all: run

run: main.cpp
	gcc $^ -o $@  -L/usr/local/lib -llua -std=c++11 -lstdc++ -lm -ldl -g

r: run
	./$<

.PHONE: all r
