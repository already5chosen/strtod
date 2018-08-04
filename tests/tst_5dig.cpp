#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <quadmath.h>

extern "C" double small_strtod(const char* str, char** endptr);

int main()
{
  double minErr = 0;
  double maxErr = 0;
  int tot = 0;
  int rnd = 0;
  const uint64_t INF_PATTERN = (uint64_t)1023 << 52;
  for (int exp = -323; exp < 310; ++exp) {
    for (int mant = 0; mant < 100000; ++mant) {
      char inpbuf[80];
      sprintf(inpbuf, ".%de%d", mant, exp);
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
             "src %s. err = %+f ULP\n"
             , inpbuf
             , err
            );
          ++rnd;
        }
      }
    }
    printf("exp=%6d\n", exp); fflush(stdout);
  }
  printf("o.k. %d rounding errors out of %d. %.3e percents\n", rnd, tot, 1e2*rnd/tot);

  return 0;
}
