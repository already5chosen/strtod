CC = g++ -std=c++11 -O2 -Wall
cc = gcc -std=c99   -O2 -Wall

all: tstm tst5d n2_tstm n2_tst5d

small_strtod.o: ../small_strtod.c
	${cc} -c $< -o  $@

n2_small_strtod.o: ../n2_small_strtod.c
	${cc} -c $< -o  $@

tst_manual.o: tst_manual.cpp
	${CC} -c $<

tst_5dig.o: tst_5dig.cpp
	${CC} -c $<

tstm: tst_manual.o small_strtod.o
	${CC} $+ -lquadmath -o $@

tst5d: tst_5dig.o small_strtod.o
	${CC} $+ -lquadmath -o $@

n2_tstm: tst_manual.o n2_small_strtod.o
	${CC} $+ -lquadmath -o $@

n2_tst5d: tst_5dig.o n2_small_strtod.o
	${CC} $+ -lquadmath -o $@
