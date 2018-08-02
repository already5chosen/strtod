CC = g++ -std=c++11 -O2 -Wall
cc = gcc -std=c99   -O2 -Wall

all: tstm

small_strtod.o: ../small_strtod.c
	${cc} -c $< -o  $@

tst_manual.o: tst_manual.cpp
	${CC} -c $<

tstm: tst_manual.o small_strtod.o
	${CC} $+ -lquadmath -o $@
