#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <random>

static uint64_t mulu(uint64_t x, uint64_t y) {
  return uint64_t(((unsigned __int128)x * y) >> 64);
}

static uint64_t d2u(double x) {
  uint64_t y;
  memcpy(&y, &x, sizeof(y));
  return y;
}

static double u2d(uint64_t x) {
  double y;
  memcpy(&y, &x, sizeof(y));
  return y;
}

int main(int argz, char** argv)
{
  long len = 100000;
  if (argz > 1) {
    long v = strtol(argv[1], NULL, 0);
    if (v > 0 && v < 100000000)
      len = v;
  }

  std::mt19937_64 gen;
  gen.seed(1);

  const uint64_t BIT63 = uint64_t(1) << 63;
  // const uint64_t MSK63 = BIT63 - 1;
  const uint64_t RSCALE = d2u(DBL_MAX) + 1;
  for (long it = 0; it < len; ++it) {
    uint64_t urnd = gen();
    uint64_t ufin = mulu(urnd*2, RSCALE) | (urnd & BIT63); // transform to finite range
    double d = u2d(ufin);
    printf("%016llx %.17e\n", ufin, d);
  }
  return 0;
}

