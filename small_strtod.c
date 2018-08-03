#include <stdint.h>
#include <string.h>

// return # of digits
static ptrdiff_t readDecimalDigits(const char* src, uint64_t* pVal, uint64_t maxVal, ptrdiff_t* pnUsed, int* sticky)
{
  uint64_t val = *pVal;
  ptrdiff_t nd, nUsed = 0;
  unsigned lsb = 0;
  for (nd = 0; ; ++nd) {
    unsigned c = src[nd];
    unsigned dig = c - '0';
    if (dig > 9)
      break;
    if (val <= maxVal) {
      val = val*10+dig;
      ++nUsed;
    } else {
      lsb |= dig;
    }
  }
  *pVal = val;
  *sticky |= (lsb != 0); // for tie breaker
  *pnUsed = nUsed;
  return nd;
}

static const char *skipWhiteSpaces(const char *str)
{ // skip leading white spaces
  while (*str && *str <= ' ') ++str;
  return str;
}

// return bit pattern of IEEE binary64
static uint64_t to_double(uint64_t mant, int exp)
{
  int EXP_BIAS = 1023;
  exp += EXP_BIAS;
  if (exp >= 2047)
    return (uint64_t)2047 << 52; // Inf
  if (exp < 1) {
    return 0;
  }
  // normal range
  mant += mant; // remove MS bit
  uint64_t ret = ((uint64_t)exp << 52) + (mant >> 12);
  unsigned rem = mant & ((1u<<12)-1);
  rem |= (ret & 1); // to nearest even
  ret += (rem > (1u<<11));
  return ret;
}

double small_strtod(const char* str, char** endptr)
{
  const char* p = skipWhiteSpaces(str);
  int neg = 0;
  switch (p[0]) {
    case '+': ++p;          break;
    case '-': ++p; neg = 1; break;
    default:                break;
  }

  uint64_t mant = 0;
  int sticky=0;
  ptrdiff_t nUsedInt;
  ptrdiff_t ni = readDecimalDigits(p, &mant, (UINT64_MAX-9)/10,  &nUsedInt, &sticky);

  ptrdiff_t nf = 0;
  ptrdiff_t nUsedFract = 0;
  if (p[ni]=='.')
    nf = readDecimalDigits(&p[ni+1], &mant, (UINT64_MAX-9)/10, &nUsedFract, &sticky);

  uint64_t uret = 0;
  const char* endptrval = str;
  if (ni == 0 && nf == 0) {  // conversion failed
    neg = 0;
    goto done;
  }

  ptrdiff_t exp = ni - nUsedInt - nUsedFract;
  ptrdiff_t nm  = ni + nf + (p[ni]=='.');
  endptrval = &p[nm];
  const int MAX_EXP =  310;
  const int MIN_EXP = -345;
  if (endptrval[0] == 'e' || endptrval[0] == 'E') {
    p = endptrval + 1;
    int nege = 0;
    switch (p[0]) {
      case '+': ++p;           break;
      case '-': ++p; nege = 1; break;
      default:                 break;
    }

    uint64_t absExpVal = 0;
    ptrdiff_t dummy1;
    int dummy2;
    ptrdiff_t ne = readDecimalDigits(p, &absExpVal,
      nege == 0 ? (uint64_t)MAX_EXP - exp : (uint64_t)-MIN_EXP + exp,
      &dummy1, &dummy2);
    if (ne != 0) {
      // exponent present
      endptrval = p + ne;
      exp = nege==0 ? exp + absExpVal : exp - absExpVal;
    }
  }

  if (mant != 0) {
    if (exp <= MIN_EXP) {
      goto done;
    }
    if (exp >= MAX_EXP) {
      uret = (uint64_t)2047 << 52; // Inf
      goto done;
    }

    // normalize mantissa
    int lz = __builtin_clzll(mant);
    uret = mant << lz;
    int bine = 63-lz;
    if (exp != 0) {
      // scale uret*2**bine by 10**exp
      const uint64_t MSB = (uint64_t)1 << 63;

      uint32_t lret = 0;
      for (; exp < 0; exp += 28) {
        // multiply by 1E-28
        const uint32_t MULx_H = 0x9E12835F;
        const uint32_t MULx_L = 0x3FCD7C9E;
        uint64_t w2 = uret >> 32;
        uint64_t w1 = (uint32_t)uret;

        uint64_t delta = (w2 * MULx_H)
          + ((w2 * MULx_L) >> 32)
          + ((w1 * MULx_H) >> 32);
        uret -= (delta >> 6);
        uint32_t delta_l = (uint32_t)delta << 26;
        uret -= (delta_l >= lret);
        lret -= delta_l;

        bine -= 93;
        if ((uret & MSB)==0) { // underflow
          bine -= 1;
          uret += uret + (lret >> 31);
          lret += lret;
        }
      }

      for (; exp > 2; exp -= 3) {
        // multiply by 1000
        uint64_t udec1 = (uret >> 6);
        uint64_t udec2 = (uret >> 7);
        uint32_t ldec1 = (lret >> 6) | ((uret & 0x3F) << 26);
        uint32_t ldec2 = (lret >> 7) | ((uret & 0x7F) << 25);
        uret -= udec1;
        uret -= udec2;
        uret -= (ldec1 > lret);
        lret -= ldec1;
        uret -= (ldec2 > lret);
        lret -= ldec2;
        bine += 10;
        if ((uret & MSB)==0) { // underflow
          bine -= 1;
          uret += uret + (lret >> 31);
          lret += lret;
        }
      }

      for (; exp > 0; --exp) {
        // multiply by 10
        uint64_t uinc = (uret >> 2);
        uint32_t linc = (lret >> 2) | ((uret & 3) << 30);
        lret += linc;
        uret += (lret < linc);
        uret += uinc;
        bine += 3;
        if ((uret & MSB)==0) { // overflow
          bine += 1;
          lret = (lret >> 1) | ((uret & 1) << 31);
          uret = (uret >> 1) | MSB;
        }
      }

      uret |= (lret >> 31);
    }
    uret = to_double(uret | sticky, bine);
  }

  done:
  if (endptr)
    *endptr = (char*)endptrval;

  uret |= (uint64_t)neg << 63; // set sign bit
  double dret;
  memcpy(&dret, &uret, sizeof(dret));

  return dret;
}
