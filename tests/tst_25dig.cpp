#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <quadmath.h>

extern "C" double small_strtod(const char* str, char** endptr);

int main(int argz, char** argv)
{
  int minExp = -323;
  int maxExp =  309;
  int nIntArgs = 0;
  int nDig = 25;
  bool exactNdig = false;
  for (int arg_i = 1; arg_i < argz; ++arg_i) {
    char* arg = argv[arg_i];
    char* endp;
    if (strcmp(arg, "-e")== 0) {
      exactNdig = true;
    } else if (arg[0]=='-' && arg[1]!=0 && arg[2]=='=') {
      int val = strtol(&arg[3], &endp, 0);
      if (endp != &arg[3]) {
        switch (arg[1]) {
          case 'd':
            if (val < 6 || val > 25) {
              fprintf(stderr, "Number of digit %d out of range [6..25]\n", val);
              return 1;
            }
            nDig = val;
            break;
          default:
            fprintf(stderr, "Unknown option '%s'\n", arg);
            return 1;
        }
      }
    } else {
      int val = strtol(arg, &endp, 0);
      if (endp != arg) {
        if (nIntArgs < 2) {
          if (nIntArgs==0)
            minExp = val;
          maxExp = val;
        }
        ++nIntArgs;
      }
    }
  }

  int ndigMin   = exactNdig ? nDig : 1;
  const uint32_t incrFactor = uint32_t(round(ldexp(pow(10, (nDig-ndigMin+1)*1e-5)-1, 32)));
  const double mant0 = floor(nextafter(pow(10, ndigMin-1), DBL_MAX));
  printf("Running test with %d to %d digits. incrFactor=%u.\n", ndigMin, nDig, incrFactor);

  double minErr = 0;
  double maxErr = 0;
  int tot = 0;
  int rnd = 0;
  const uint64_t INF_PATTERN = (uint64_t)2047 << 52;
  for (int exp = minExp; exp <= maxExp; ++exp) {
    double expMinErr = 0;
    double expMaxErr = 0;
    unsigned __int128 scaledMant = mant0 * (1 << 20);
    const uint64_t splitFactor = 1E12;
    for (int mant_i = 0; mant_i < 100000; ++mant_i) {
      unsigned __int128 mant = scaledMant >> 20;
      uint64_t mant_h = mant / splitFactor;
      uint64_t mant_l = mant % splitFactor;
      char inpbuf[80];
      if (mant_h != 0)
        sprintf(inpbuf, ".%llu%012llue%d", mant_h, mant_l, exp);
      else
        sprintf(inpbuf, ".%llue%d", mant_l, exp);
      char* endp0;
      double val0 = strtod(inpbuf, &endp0);
      char* endp1;
      double val1 = small_strtod(inpbuf, &endp1);
      uint64_t uval0, uval1;
      memcpy(&uval0, &val0, sizeof(uval0));
      memcpy(&uval1, &val1, sizeof(uval1));
      tot += (uval0 != 0) && (uval0 != INF_PATTERN) ;
      if (uval0 != uval1 || endp0 != endp1) {
        if  (endp0 != endp1 || (uval1 != uval0-1 && uval1 != uval0+1)) {
          printf(
           "src %s fail\n"
           "ref %.21e %d '%s'\n"
           "res %.21e %d '%s' %s%s\n"
           , inpbuf
           , val0, (int)(endp0 - inpbuf), endp0
           , val1, (int)(endp1 - inpbuf), endp1
           , endp1==endp0 ? "." : "!"
           , uval1==uval0 ? "." : "!"
          );
          return 1;
        } else {
          // incorrect rounding
          double ulp = fabs(val0 - val1);
          double err = double((val1-strtoflt128(inpbuf, 0))/ulp);
          if (err < expMinErr) {
            expMinErr = err;
          }
          if (err > expMaxErr) {
            expMaxErr = err;
          }
          bool prt = false;
          if (err < minErr) {
            minErr = err;
            prt = true;
          }
          if (err > maxErr) {
            maxErr = err;
            prt = true;
          }
          if (prt)
            printf(
             "src %s. err = %+.10f ULP\n"
             , inpbuf
             , err
            );
          ++rnd;
        }
      }

      scaledMant += (scaledMant*incrFactor) >> 32;
    }
    // printf("%e\n", double(scaledMant >> 20));
    printf("exp=%6d err [%- 13.*f..%c%-12.*f] ULP\n"
      , exp
      , expMinErr==0 ? 0 : 10, expMinErr
      , expMaxErr==0 ? ' ' : '+',  expMaxErr==0 ? 0 : 10, expMaxErr
      );
    fflush(stdout);
  }
  printf("o.k. %d rounding errors out of %d. %.3e percents. err [%+.10f..%+.10f] ULP.\n", rnd, tot, 1e2*rnd/tot, minErr, maxErr);

  return 0;
}
