CC = g++ -std=c++11 -O2 -Wall
cc = gcc -std=c99   -O2 -Wall

all: tstm tst5d tst25d n2_tstm n2_tst5d n2_tst25d 64b_tstm 64b_tst5d 64b_tst25d w3_tstm w3_tst5d w3_tst25d

small_strtod.o: ../small_strtod.c
	${cc} -c $< -o  $@

n2_small_strtod.o: ../n2_small_strtod.c
	${cc} -c $< -o  $@

64b_small_strtod.o: ../64b_small_strtod.c
	${cc} -c $< -o  $@

w3_small_strtod.o: ../w3_small_strtod.c
	${cc} -c $< -o  $@

tst_manual.o: tst_manual.cpp
	${CC} -c $<

tst_5dig.o: tst_5dig.cpp
	${CC} -c $<

tst_25dig.o: tst_25dig.cpp
	${CC} -c $<

tstm: tst_manual.o small_strtod.o
	${CC} $+ -lquadmath -o $@

tst5d: tst_5dig.o small_strtod.o
	${CC} $+ -lquadmath -o $@

tst25d: tst_25dig.o small_strtod.o
	${CC} $+ -lquadmath -o $@

n2_tstm: tst_manual.o n2_small_strtod.o
	${CC} $+ -lquadmath -o $@

n2_tst5d: tst_5dig.o n2_small_strtod.o
	${CC} $+ -lquadmath -o $@

n2_tst25d: tst_25dig.o n2_small_strtod.o
	${CC} $+ -lquadmath -o $@

64b_tstm: tst_manual.o 64b_small_strtod.o
	${CC} $+ -lquadmath -o $@

64b_tst5d: tst_5dig.o 64b_small_strtod.o
	${CC} $+ -lquadmath -o $@

64b_tst25d: tst_25dig.o 64b_small_strtod.o
	${CC} $+ -lquadmath -o $@

w3_tstm: tst_manual.o w3_small_strtod.o
	${CC} $+ -lquadmath -o $@

w3_tst5d: tst_5dig.o w3_small_strtod.o
	${CC} $+ -lquadmath -o $@

w3_tst25d: tst_25dig.o w3_small_strtod.o
	${CC} $+ -lquadmath -o $@
