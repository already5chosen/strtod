#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <quadmath.h>

extern "C" double small_strtod(const char* str, char** endptr);

static void body(char* str)
{
  char* endp_ref;
  __float128 val_ref = strtoflt128(str, &endp_ref);
  double val_refd = val_ref;
  char* endp0;
  double val0 = strtod(str, &endp0);
  char* endp1;
  double val1 = small_strtod(str, &endp1);

  char diff0_buf[100]={0};
  char diff1_buf[100]={0};
  if (val0 != val1) {
    double ulp = fabs(nextafter(val1, val0)-val1);
    snprintf(diff0_buf, sizeof(diff0_buf), " %+f ULP", double((val0-val_ref)/ulp));
    snprintf(diff1_buf, sizeof(diff1_buf), " %+f ULP", double((val1-val_ref)/ulp));
  }
  printf(
   "strtoflt128  : %29.21e %d '%s'\n"
   "strtod       : %29.21e %d '%s' %s%s%s\n"
   "small_strtod : %29.21e %d '%s' %s%s%s\n"
   , val_refd, (int)(endp_ref - str), endp_ref
   , val0,     (int)(endp0 - str), endp0
   , endp0==endp_ref ? "." : "!"
   , val0 ==val_refd ? "." : "!"
   , diff0_buf
   , val1, (int)(endp1 - str), endp1
   , endp1==endp_ref ? "." : "!"
   , val1 ==val_refd ? "." : "!"
   , diff1_buf
   );
}

int main(int argz, char** argv)
{
  if (argz < 2) {
    printf("%s",
      "tstm - manual test of small_strtod\n"
      "Usage\n"
      "tstm string-to-convert\n"
    );
    return 1;
  }

  char* str = argv[1];
  body(str);

  return 0;
}
