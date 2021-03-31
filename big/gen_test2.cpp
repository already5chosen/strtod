#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <random>

#include <gmp.h>

enum {
  N_DIGITS_MIN =    2,
  N_DIGITS_MAX =  800,
  DECEXP_MIN   = -325, // from 0.1e-322
  DECEXP_MAX   =  325, // to   1.0e308

  POW10_TAB_LEN = N_DIGITS_MAX-DECEXP_MIN+2,
};

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

static int body(int nDigits, long nItems, int  decexpMin, int  decexpMax, int seed);
static void MakeTables();

int main(int argz, char** argv)
{
  // { int e; double d = frexp(u2d(1), &e); printf("%f %d %.17e\n", d, e, u2d(1));}
  // { int e; double x = DBL_MAX; double d = frexp(x, &e); printf("%.20f %d %.17e\n", d, e, x);}
  if (argz < 2) {
    fprintf(stderr,
      "Usage:\n"
      "gen_test2 nDigits [-c=count] [-emin=nnn] [-emax=nnn]\n"
    );
    return 1;
  }

  int nDigits = 0;
  if (argz > 1) {
    char* endp;
    long v = strtol(argv[1], &endp, 0);
    if (v >= N_DIGITS_MIN && v <= N_DIGITS_MAX) {
      nDigits = v;
    } else {
      if (endp == argv[1])
        fprintf(stderr, "Bad nDigits parameter '%s'. Must be a number.\n", endp);
      else
        fprintf(stderr, "Bad nDigits parameter '%s'. Out of ramge [%d:%d].\n", endp, N_DIGITS_MIN, N_DIGITS_MAX);
      return 1;
    }
  }

  long nItems = 100000;
  int  decexpMin = DECEXP_MIN;
  int  decexpMax = DECEXP_MAX;
  for (int arg_i = 2; arg_i < argz; ++arg_i) {
    char* arg = argv[arg_i];
    if (arg[0] != '-') {
      fprintf(stderr, "Illegal parameter '%s'\n", arg);
      return 1;
    }
    char* eq = strchr(&arg[1], '=');
    if (eq==0) {
      fprintf(stderr, "Malformed option '%s'\n", arg);
      return 1;
    }

    char* endp;
    long v = strtol(eq+1, &endp, 0);
    if (endp==eq+1) {
      fprintf(stderr, "Bad option '%s'. '%s' is not a number.\n", arg, eq+1);
      return 1;
    }

    if        (0==strncmp(&arg[1], "c", eq-arg-1)) {
      if (v < 1 || v > 100000000) {
        fprintf(stderr, "Bad option '%s'. Please specify number in range [1:100000000].\n", arg);
        return 1;
      }
      nItems = v;
    } else if (0==strncmp(&arg[1], "emin", eq-arg-1)) {
      if (v < DECEXP_MIN || v > decexpMax) {
        fprintf(stderr, "Bad option '%s'. Please specify number in range [%d:%d].\n", arg, DECEXP_MIN, decexpMax);
        return 1;
      }
      decexpMin = v;
    } else if (0==strncmp(&arg[1], "emax", eq-arg-1)) {
      if (v < decexpMin || v > DECEXP_MAX) {
        fprintf(stderr, "Bad option '%s'. Please specify number in range [%d:%d].\n", arg, decexpMin, DECEXP_MAX);
        return 1;
      }
      decexpMax = v;
    } else {
      fprintf(stderr, "Unknown option '%s'.\n", arg);
      return 1;
    }
  }

  MakeTables();
  return body(nDigits, nItems, decexpMin, decexpMax, 1);
}

static mpz_t  pow10_tab_z[POW10_TAB_LEN];
static double pow10i_tab_d[POW10_TAB_LEN]; // 2**((1741647u*i)>>19)/10**k rounded toward 0
static const unsigned pow10_tab_u[] = {
    1,         10,           100,
    1000,      10*1000,      100*1000,
    1000*1000, 10*1000*1000, 100*1000*1000,
    1000*1000*1000,
};
static mpz_t dblMaxLimit;

static void MakeTables()
{
  mpz_init_set_si(pow10_tab_z[0], 1);
  for (int i = 1; i < POW10_TAB_LEN; ++i) {
    mpz_init(pow10_tab_z[i]);
    mpz_mul_si(pow10_tab_z[i], pow10_tab_z[i-1], 10);
  }

  mpz_t x;
  mpz_init(x);
  for (int i = 0; i < POW10_TAB_LEN; ++i) {
    mpz_set_si(x, 1);
    mpz_mul_2exp(x, x, 100 + ((1741647*i)>>19));
    mpz_tdiv_q(x, x, pow10_tab_z[i]);
    pow10i_tab_d[i] = ldexp(mpz_get_d(x), -100);
    // printf("%10.5f\n", pow10i_tab_d[i]);
  }

  // set dblMaxLimit to DBL_MAX + 0.5 ULP(DBL_MAX)
  mpz_init_set_si(dblMaxLimit, 1);
  mpz_mul_2exp(dblMaxLimit, dblMaxLimit, 54);      // = 2**54
  mpz_sub_ui  (dblMaxLimit, dblMaxLimit, 1);       // = 2**54-1
  mpz_mul_2exp(dblMaxLimit, dblMaxLimit, 1024-54); // = ((2**54-1)/2**54)*2**1024
}
// int prt=0;
static int core_cmp(mpz_t x, mpz_t zTmp1, mpz_t zTmp2, int decpow, double d, int dScale, bool inc)
{
  // if (prt)fprintf(stderr, "1.0: %.17e %d %.17f\n", d, dScale, ldexp(d, dScale));
  mpz_set_d(zTmp1, ldexp(d, dScale));
  if (inc)
    mpz_add_ui(zTmp1, zTmp1, 1);
  // printf("1.1: %s %d\n", mpz_get_str(NULL, 10, zTmp1), decpow);

  if (decpow < 0)
    mpz_mul(zTmp1, zTmp1, pow10_tab_z[-decpow]);
  // printf("1.2: %s\n", mpz_get_str(NULL, 10, zTmp1));

  if (dScale < 0)
    mpz_mul_2exp(zTmp1, zTmp1, -dScale);
  // printf("1.3: %s\n", mpz_get_str(NULL, 10, zTmp1));

  // printf("1.4: %s\n", mpz_get_str(NULL, 10, x));
  if (dScale <= 0)
    return mpz_cmp(x, zTmp1);

  // dScale > 0
  mpz_mul_2exp(zTmp2, x, dScale);
  // printf("1.5: %s\n", mpz_get_str(NULL, 10, zTmp2));
  return mpz_cmp(zTmp2, zTmp1);
}

static double calc_d(mpz_t x, mpz_t zTmp1, mpz_t zTmp2, int nDigits, int decexp, const unsigned mntDigits[])
{
  if (decexp <= -324)
    return 0;

  // calculate mantissa
  int fullNd = (nDigits-1)/9;
  int lastNd = nDigits - fullNd*9;
  mpz_set_si(x, 0);
  for (int i = 0; i < fullNd; ++i) {
    mpz_mul_ui(x, x, 1000000000u);
    mpz_add_ui(x, x, mntDigits[i]);
  }
  mpz_mul_ui(x, x, pow10_tab_u[lastNd]);
  mpz_add_ui(x, x, mntDigits[fullNd]);

  if (mpz_cmp_ui(x, 0)==0)
    return 0;

  // scale by power of 10
  int decpow = decexp - nDigits;
  if (decpow > 0) {
    mpz_mul(x, x, pow10_tab_z[decpow]);
    decpow = 0;
  }

  if (decexp > 308) {
    // test for overflow
    if (decpow < 0) {
      mpz_mul(zTmp1, dblMaxLimit, pow10_tab_z[-decpow]);
      if (mpz_cmp(x, zTmp1) >= 0)
        return HUGE_VAL;
    } else if (mpz_cmp(x, dblMaxLimit) >= 0) {
      return HUGE_VAL;
    }
  }

  // test for underflow
  mpz_mul_2exp(zTmp1, x, 1075);                  // = x/dblMinLimit==2**-1075
  if (mpz_cmp(zTmp1, pow10_tab_z[-decpow]) <= 0) // x/dblMinLimit <= 10**(-decpow)
    return 0; // underflow

  // convert to FP, truncating toward zero
  long dExp;
  double dMnt = mpz_get_d_2exp(&dExp, x) * pow10i_tab_d[-decpow];
  dExp -= (-decpow*1741647)>>19;
  // if (prt)fprintf(stderr, "0.1: %.17f %ld %016llx\n", dMnt, dExp, d2u(dMnt));
  int e2;
  dMnt = frexp(dMnt, &e2);
  dExp += e2;
  // if (prt)fprintf(stderr, "0.2: %.17f %ld %016llx\n", dMnt, dExp, d2u(dMnt));

  if (dExp < -1073)
    return 4.9406564584124654e-324; // nextafter(0, 1), has to be that, because possibility of underflow already rejected

  if (dExp > 1024)
    return DBL_MAX; // has to be that, because possibility of overflow already rejected

  // dExp in [-1073:1024]
  double d0 = ldexp(dMnt, dExp);
  if (d0 <= DBL_MIN)
    d0 = nextafter(d0, 0); // subnormal can be rounded up. In order to be sure that our estimate is from below, lets reduce it by 1 ulp
  for (;;) {
    // if (prt)fprintf(stderr, "0.3: %.17e %016llx\n", d0, d2u(d0));
    if (d0 == DBL_MAX)
      return DBL_MAX;

    double d1 = nextafter(d0, DBL_MAX);
    // printf("4: %.17e %016llx\n", d1, d2u(d1));
    int ulpExp;
    frexp(d1-d0, &ulpExp);
    int dScale = 2 - ulpExp; // (d1-d0)* 2**dScale == 2

    int cond = core_cmp(x, zTmp1, zTmp2, decpow, d1, dScale, false);
    // printf("5: %d\n", cond);
    if (cond == 0)
      return d1;

    if (cond > 0) {
      d0 = d1;
      continue;
    }

    // d0 <= x*10**decpow < d1
    cond = core_cmp(x, zTmp1, zTmp2, decpow, d0, dScale, true); // compare vs midpoint==(d0+d1)/2
    // printf("6: %d\n", cond);

    if (cond < 0)
      return d0; // x*10**decpow < (d0+d1)/2

    if (cond > 0)
      return d1; // x*10**decpow > (d0+d1)/2

    // x*10**decpow == (d0+d1)/2
    return (d2u(d0) & 1)==0 ? d0 : d1; // tie broken to even
  }
}

static void binToDecStr(char* dst, int nd, unsigned x)
{
  dst[nd]=0;
  for (int k = 0; k < nd; ++k) {
    dst[nd-1-k] = (x % 10) + '0';
    x /= 10;
  }
}

static void mntToStr(char* dst, int fullNd, int lastNd, const unsigned mntDigits[])
{
  for (int i = 0; i < fullNd; ++i)
    binToDecStr(&dst[i*9], 9, mntDigits[i]);
  binToDecStr(&dst[fullNd*9], lastNd, mntDigits[fullNd]);
}

static int body(int nDigits, long nItems, int decexpMin, int  decexpMax, int seed)
{
  std::mt19937_64 gen;
  gen.seed(seed);

  mpz_t zTmp0, zTmp1, zTmp2;
  mpz_init(zTmp0);
  mpz_init(zTmp1);
  mpz_init(zTmp2);
  int fullNd = (nDigits-1)/9;
  int lastNd = nDigits - fullNd*9;
  unsigned mntDigits[N_DIGITS_MAX/9+1];
  char mntStr[N_DIGITS_MAX + 10];
  for (long it = 0; it < nItems; ++it) {
    // prt=it==78587;
    // generate random integer in range [0:10**nDigits)
    // 9 digits at time.
    // I'd like to do more digits, but don't know how to use 'long long' with GMP
    for (int i = 0; i < fullNd; ++i)
      mntDigits[i] = mulu(gen(), 1000000000u);            // [0:1e9-1]
    mntDigits[fullNd] = mulu(gen(), pow10_tab_u[lastNd]); // [0:10**lastNd-1]

    uint64_t urnd = gen();
    uint64_t sign = urnd >> 63;
    int decexp = int(mulu(urnd<<1, decexpMax+1-decexpMin)) + decexpMin;

    double d = calc_d(zTmp0, zTmp1, zTmp2, nDigits, decexp, mntDigits);
    mntToStr(mntStr, fullNd, lastNd, mntDigits);

    printf("%016llx %s0.%se%d\n", d2u(d) | (sign << 63), sign ? "-" : "", mntStr, decexp);
  }
  return 0;
}