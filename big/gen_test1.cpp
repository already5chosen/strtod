#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <random>

static const char UsageStr[] =
"gen_test1 - generate test vector consisting of canonical 17-digit\n"
"            representations of finite IEEE-754 binary64 numbers\n"
"Usage:\n"
"gen_test1 [?] [-?] [count]\n"
"where\n"
"count - [optional] number of items to generate. Range [1:100000000]. Default 100000.\n"
"-?, ? - show this message"
;

static uint64_t mulu(uint64_t x, uint64_t y) {
#ifndef _MSC_VER
  return uint64_t(((unsigned __int128)x * y) >> 64);
#else
  uint64_t ret;
  _umul128(x, y, &ret);
  return ret;
#endif
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
    if (strcmp(argv[1], "?")==0 || strcmp(argv[1], "-?")==0) {
      fprintf(stderr, "%s", UsageStr);
      return 0;
    }
    long v = strtol(argv[1], NULL, 0);
    if (v > 0 && v <= 100000000) {
      len = v;
    } else {
      fprintf(stderr, "Illegal count '%s'.\n%s", argv[1], UsageStr);
      return 1;
    }
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
    printf("%016" PRIx64 " %.17e\n", ufin, d);
  }
  return 0;
}

