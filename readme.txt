Purpose: 
Develop low code footprint implementation of C library function strtod.

Motivation: a "small" variant of newlib library (which appears to have very
little in common with normal newlib) supplied with Intel/Alters Nios2 SDK
contains no strtod. But I want one.

Approximate specification.

Precision:
No worse than 1 ULP, but not necessarily correctly rounded.
But I want correct rounding most of the time.
And I want correct rounding in "obvious" cases, like mantissa consists of few
significant digits (10 or less) and magnitude of exponent is small (< 17).

Speed:
Not "fast at all cost". Generally, small code and tables footprint is of higher
priority than speed. But not horribly slow either.
The speed is optimized for CPUs that are similar to "full-featured" variant of
Nios2 core without FPU, i.e. for CPU that is natively 32-bit with decent speed
of shifter, CLZ, 32x32bit MULU and MULUH (exposed through uint64_t multiplication).
But "double' is emulated and much slower than uint64_t/int64_t.

Portability:
Not too portable.
The code will be written mostly in C99 with use of very few Gnu extension
(__builtin_clz and probably nothing else), but it will assume "idiomatic"
data types: sizeof(uint8_t)=1, sizeof(uint32_t)=4, sizeof(uint64_t)=8,
sizeof(double)=8, double= IEEE-754 binary64.
