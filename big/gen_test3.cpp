#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <random>

#include <gmp.h>

static uint64_t mulu(uint64_t x, uint64_t y) {
  return uint64_t(((unsigned __int128)x * y) >> 64);
}

static uint64_t d2u(double x) {
  uint64_t y;
  memcpy(&y, &x, sizeof(y));
  return y;
}

#if 0
static double u2d(uint64_t x) {
  double y;
  memcpy(&y, &x, sizeof(y));
  return y;
}
#endif

static int body(long nItems, double fMin, double fMax, int seed, double dev);
static void MakeTables();

static const char UsageStr[] =
"gen_test3 - produce test vector consisting of \"evil\" decimal strings\n"
" exactly at midpoints between representable binary64 numbers or, optionally,\n"
" close to a midpoint\n"
"Usage:\n"
"gen_test3 [-c=count] [-fmin=nnn] [-fmax=xxx] [-dev=ddd] [-s=seed] [-?] [?]\n"
"where\n"
"count - [optional] number of items to generate\n"
"nnn   - [optional] lower edge of the range of absolute values of generated number. Default=0\n"
"xxx   - [optional] upper edge of the range of absolute values of generated number. Default=DBL_MAX\n"
"ddd   - [optional] deviation factor. Range [0:1], Default=0.\n"
"        Non-zero ddd specifies that test point will be chosen from range (MPt-ULP*0.5*ddd:MPt+ULP*0.5*ddd)\n"
"seed  - [optional] PRNG seed. Default=1\n"
"-?, ? - show this message"
;

int main(int argz, char** argv)
{

  long nItems = 100000;
  double fMin = 0;
  double fMax = DBL_MAX;
  double dev  = 0;
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

    enum { O_C, O_FMIN, O_FMAX, O_DEV, O_S, O_UNK };
    static const char *optstr[] = { "c", "fmin", "fmax", "dev", "s" };
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
      case O_DEV:
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

          case O_DEV:
            if (v < 0 || v > 1.0) {
              fprintf(stderr, "Bad option '%s'. Please specify number in range [0:1].\n", arg);
              return 1;
            }
            dev = v;
            break;
        }
      } break;

      default:
        fprintf(stderr, "Unknown option '%s'.\n", arg);
        return 1;
    }
  }

  MakeTables();
  return body(nItems, fMin, fMax, seed, dev);
}

enum {
  POW5_TAB_LEN = 1200,
};

static mpz_t pow5_tab_z[POW5_TAB_LEN];
static void MakeTables()
{
  mpz_init_set_si(pow5_tab_z[0], 1);
  for (int i = 1; i < POW5_TAB_LEN; ++i) {
    mpz_init(pow5_tab_z[i]);
    mpz_mul_si(pow5_tab_z[i], pow5_tab_z[i-1], 5);
  }
}

static int body(long nItems, double fMin, double fMax, int seed, double dev)
{
  std::mt19937_64 gen;
  gen.seed(seed);

  mpz_t x, xDev;
  mpz_init(x);
  mpz_init(xDev);
  char mntStr[1000];
  const uint64_t uMin = d2u(fMin);
  const uint64_t uMax = d2u(fMax);
  const int      devScale = 53;
  const int64_t  devMax = int64_t(ldexp(dev, devScale));
  const uint64_t BIT63 = uint64_t(1) << 63;
  const uint64_t BIT52 = uint64_t(1) << 52;
  const uint64_t MSK52 = BIT52 - 1;
  for (long it = 0; it < nItems; ++it) {
    // generate random floating point in range [fMin:fmax] and random sign
    uint64_t rnd = gen();
    uint64_t u = mulu(rnd*2, uMax+1-uMin) + uMin;
    uint64_t sign = rnd & BIT63;

    int64_t iDev = 0;
    if (devMax != 0) // generate random integer in range (-dev*2**53:+dev*2**53)
      iDev = int64_t(mulu(gen(), devMax*2-1)) - (devMax-1);

    // split into mantissa and exponent
    int biasedExp = u >> 52;
    uint64_t mnt = u & MSK52;
    if (biasedExp > 0)
      mnt |= BIT52;
    else
      biasedExp = 1;

    mpz_set_d(x, double(mnt*2));
    mpz_add_ui(x, x, 1); // add 0.5 ULP
    if (iDev != 0) { // add scaled deviation
      mpz_set_d(xDev, double(iDev));
      mpz_mul_2exp(x, x, devScale);
      mpz_add(x, x, xDev);
    }

    // convert to decimal
    int binExp = biasedExp - 1023 - 53;
    int decExp = 0;
    if (iDev != 0)
      binExp -= devScale;
    if (binExp < 0) {
      mpz_mul(x, x, pow5_tab_z[-binExp]);
      decExp = binExp;
    } else if (binExp > 0) {
      mpz_mul_2exp(x, x, binExp);
    }

    mpz_get_str(mntStr, 10, x);

    const char *tieStr = "";
    if (iDev == 0) { // round to even
      uint64_t lsb = (u & 1);
      u += lsb;
      tieStr = lsb ? "+" : "-";
    } else if (iDev > 0) {
      u += 1;
    }

    // add sign
    u |= sign;

    printf("%s%016" PRIx64 " %s0.%se%d\n", tieStr, u, sign ? "-" : "", mntStr, decExp+int(strlen(mntStr)));
  }
  return 0;
}
