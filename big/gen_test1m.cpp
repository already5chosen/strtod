#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <random>

static const char UsageStr[] =
"gen_test1m - generate test vector consisting of canonical 17-digit\n"
"             representations of finite IEEE-754 binary64 numbers\n"
"Usage:\n"
"gen_test1 [-c=count] [-fmin=nnn] [-fmax=xxx] [-s=seed] [-?] [?]\n"
"where\n"
"where\n"
"count - [optional] number of items to generate. Default=100000\n"
"nnn   - [optional] lower edge of the range of absolute values of generated number. Default=0\n"
"xxx   - [optional] upper edge of the range of absolute values of generated number. Default=DBL_MAX\n"
"seed  - [optional] PRNG seed. Default=1\n"
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
  long nItems = 100000;
  double fMin = 0;
  double fMax = DBL_MAX;
  int  seed = 1;
  for (int arg_i = 1; arg_i < argz; ++arg_i) {
    char* arg = argv[arg_i];
    if (strcmp(arg, "?")==0) {
      fprintf(stderr, "%s", UsageStr);
      return 0;
    }
    if (arg[0] != '-') {
      fprintf(stderr, "Illegal parameter '%s'\n%s", arg, UsageStr);
      return 1;
    }
    if (arg[1] == '?') {
      fprintf(stderr, "%s", UsageStr);
      return 1;
    }

    char* eq = strchr(&arg[1], '=');
    if (eq==0) {
      fprintf(stderr, "Malformed option '%s'\n%s", arg, UsageStr);
      return 1;
    }

    enum { O_C, O_FMIN, O_FMAX, O_S, O_UNK };
    static const char *optstr[] = { "c", "fmin", "fmax", "s" };
    int op = 0;
    for (op = 0; op < O_UNK; ++op) {
      if (0==strncmp(&arg[1], optstr[op], eq-arg-1))
        break;
    }

    char* endp;
    switch (op) {
      case O_C:
      case O_S:
      {
        long v = strtol(eq+1, &endp, 0);
        if (endp==eq+1) {
          fprintf(stderr, "Bad option '%s'. '%s' is not an integer number.\n", arg, eq+1);
          return 1;
        }
        switch (op) {
          case O_C:
            if (v < 1 || v > 100000000) {
              fprintf(stderr, "Bad option '%s'. Please specify number in range [1:100000000].\n", arg);
              return 1;
            }
            nItems = v;
            break;
          case O_S:
            seed = v;
            break;
        }
      } break;

      case O_FMIN:
      case O_FMAX:
      {
        double v = strtod(eq+1, &endp);
        if (endp==eq+1) {
          fprintf(stderr, "Bad option '%s'. '%s' is not a floating point number.\n", arg, eq+1);
          return 1;
        }

        switch (op) {
          case O_FMIN:
            if (v < 0 || v > fMax) {
              fprintf(stderr, "Bad option '%s'. Please specify number in range [0:%.17e].\n", arg, fMax);
              return 1;
            }
            fMin = v;
            break;

          case O_FMAX:
            if (v < fMin || v > DBL_MAX) {
              fprintf(stderr, "Bad option '%s'. Please specify number in range [%.17e:DBL_MAX].\n", arg, fMin);
              return 1;
            }
            fMax = v;
            break;
        }
      } break;

      default:
        fprintf(stderr, "Unknown option '%s'.\n", arg);
        return 1;
    }
  }

  std::mt19937_64 gen;
  gen.seed(seed);

  const uint64_t BIT63 = uint64_t(1) << 63;
  // const uint64_t MSK63 = BIT63 - 1;
  const uint64_t uMin = d2u(fMin);
  const uint64_t uMax = d2u(fMax);
  const uint64_t RSCALE = uMax - uMin + 1;
  for (long it = 0; it < nItems; ++it) {
    uint64_t urnd = gen();
    uint64_t ufin = (mulu(urnd*2, RSCALE) + uMin) | (urnd & BIT63); // transform to finite range
    double d = u2d(ufin);
    printf("%016" PRIx64 " %.17e\n", ufin, d);
  }
  return 0;
}

