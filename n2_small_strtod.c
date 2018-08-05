#include <stdio.h>
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
    if (exp < -52)
      return 0;

    // subnormal
    unsigned nFractBits = 12 - exp;
    uint64_t FractMSB = (uint64_t)1 << (nFractBits-1);
    uint64_t FractMsk = (uint64_t)-1 >> (64-nFractBits);
    uint64_t fract = mant & FractMsk;
    uint64_t ret  = (mant >> 1) >> (nFractBits-1);
    fract |= (ret & 1); // to nearest even
    ret += (fract > FractMSB);
    return ret;
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
      unsigned sexp = exp < 0;
      unsigned mexp = sexp ? -exp : exp;
      unsigned cnt1 = mexp / 87; mexp -= cnt1*87;
      unsigned cnt2 = mexp / 28; mexp -= cnt2*28;
      unsigned cnt3 = mexp / 3 ; mexp -= cnt3*3;
      if (mexp != 0 && sexp) {
        cnt3 += 1;
        mexp = 3 - mexp;
      }
      uint32_t cnts = cnt1 | (cnt2 << 8) | (cnt3 << 16) | (mexp << 24);
      // printf("exp=%d cnts=%08x uret=%016I64x bine=%d\n", (int)exp, cnts, uret, bine);

      uint32_t lret = 0;
      static const uint32_t MULx_tab[6][2] = {
        { 0x82D85E15, 0x2C1796B1 }, // (10**87/2**289 - 1)*2**69
        { 0x0FCF2864, 0x2BDB2905 }, // (1 - 2**289/10**87)*2**69
        { 0x25026110, 0x4FCE5E3E }, // (10**28/2**93 - 1)*2**69
        { 0x9FE6BE4F, 0x4F0941AF }, // (1 - 2**93/10**28)*2**69
        { 0x00000000, 0xC0000000 }, // (1 - 10**3/2**10)*2**69
        { 0x53F7CEDA, 0xC49BA5E3 }, // (2**10/10**3 - 1)*2**69
      };
      static const uint8_t sub_tab[3]  = { 0, 0, 1};
      static const int16_t dBin_tab[6] = { 289, -289, 93, -93, 10, -10};
      for (int factor_i = 0; factor_i < 3; ++factor_i, cnts >>= 8) {
        // multiply by 10**N, where N= +/- 87, 28, 3
        unsigned idx = factor_i*2 + sexp;
        const uint32_t MULx_L = MULx_tab[idx][0];
        const uint32_t MULx_H = MULx_tab[idx][1];
        const int      dBin = dBin_tab[idx];
        const unsigned sub  = sub_tab[factor_i] ^ sexp;
        for (unsigned nIt = cnts & 255; nIt != 0; --nIt) {
          uint32_t w2 = (uint32_t)(uret >> 32);
          uint32_t w1 = (uint32_t)uret;
          uint64_t delta =
               ((uint64_t)w2 * MULx_H)
            + (((uint64_t)w2 * MULx_L) >> 32)
            + (((uint64_t)w1 * MULx_H) >> 32);
          uint64_t delta_h = delta >> 5;
          uint32_t delta_l = (uint32_t)delta << 27;
          if (sub) {
            uret -= delta_h;
            uret -= (delta_l > lret);
            lret -= delta_l;
            if ((uret & MSB)==0) { // underflow
              bine -= 1;
              uret += uret + (lret >> 31);
              lret += lret;
            }
          } else {
            uret += delta_h;
            lret += delta_l;
            uret += (delta_l > lret);
            if ((uret & MSB)==0) { // overflow
              bine += 1;
              lret = (lret >> 1) | ((uret & 1) << 31);
              uret = (uret >> 1) | MSB;
            }
          }
          bine += dBin;
        }
      }
      // printf("exp=%d cnts=%08x uret=%016I64x bine=%d\n", (int)exp, cnts, uret, bine);

      for (unsigned nIt = cnts; nIt != 0; --nIt) {
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
      // printf("exp=%d cnts=%08x uret=%016I64x bine=%d\n", (int)exp, cnts, uret, bine);

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
