// #include <stdio.h>
#include <stdint.h>
#include <string.h>

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
    do {
      // shift mantisa to the right while folding together LS bits
      mant = (mant >> 1) | (mant & 1);
      ++exp;
    } while (exp <= 0);
    exp = 0;
  }

  const uint64_t MANT_MASK = (uint64_t)-1 >> 1;  // 63 LS bits
  const uint32_t REM_MSB   = 1u << 10;
  const uint32_t REM_MASK  = REM_MSB*2-1;        // 11 LS bits

  unsigned rem = mant & REM_MASK;
  uint64_t ret = (mant & MANT_MASK) >> 11; // remove MS bit and shift mantisa to lS bits
  rem |= (ret & 1); // tie breaks to nearest even
  ret |= ((uint64_t)exp << 52);
  ret += (rem > REM_MSB);
  return ret;
}

double
__attribute ((cold))
small_strtod(const char* str, char** endptr)
{
  const char* p = skipWhiteSpaces(str);
  int neg = 0;
  switch (p[0]) {
    case '+': ++p;          break;
    case '-': ++p; neg = 1; break;
    default:                break;
  }

  const char* endptrval = str;
  enum { PARSE_FRACT, PARSE_INT = 1, PARSE_EXP };
  int parseState = PARSE_INT;
  uint64_t  rdVal = 0;
  ptrdiff_t rdExp = 0, exp;
  uint64_t  maxVal = (UINT64_MAX-9)/10;
  int lsbits = 0, succ = 0;
  uint64_t uret = 0;
  uint64_t mant;
  int sticky;
  int nege;
  const int MAX_EXP =  310;
  const int MIN_EXP = -345;
  for (;;) {
    unsigned c = *p++;
    unsigned dig = c - '0';
    if (dig <= 9) {
      succ = 1;
      rdExp += parseState;
      if (rdVal <= maxVal) {
        rdVal =
          rdVal*10+dig;
        --rdExp;
      } else {
        lsbits |= dig;
      }
    } else {
      // non-digit
      if (parseState != PARSE_EXP) {
        if (parseState == PARSE_INT) {
          if (c == '.') {
            parseState = PARSE_FRACT;
            continue;
          }
        }
        // end of mantissa
        if (succ == 0) {  // conversion failed
          neg = 0;
          goto done;
        }
        // conversion succeed
        exp     = rdExp;
        mant    = rdVal;
        sticky  = (lsbits != 0);
        endptrval = p - 1;
        if (c == 'e' || c == 'E') {
          nege = 0;
          switch (p[0]) {
            case '+': ++p;           break;
            case '-': ++p; nege = 1; break;
            default:                 break;
          }
          succ  = 0;
          rdVal = 0;
          maxVal = (nege == 0) ?
            (uint64_t) MAX_EXP - exp :
            (uint64_t)-MIN_EXP + exp ;
          parseState = PARSE_EXP;
          continue;
        }
      } else {
        // parseState == PARSE_EXP
        if (succ != 0) {
          // exponent present
          endptrval = p - 1;
          exp = nege==0 ? exp + rdVal : exp - rdVal;
        }
      }
      break;
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

    uint64_t w1 = mant;
    uint64_t w0 = 0;
    int bine = 63;
    unsigned sexp = exp < 0;
    unsigned mexp = sexp ? -exp : exp;
    for (;;) {
      // normalize mantissa
      int lz = __builtin_clzll(w1);
      bine -= lz;
      w1 = (w1 << lz) | ((w0 >> 1) >> (63-lz));
      w0 = (w0 << lz);

      if (mexp == 0)
        break;

      // scale w2:w1:w0 * 2**bine by 10**exp
      typedef struct {
        uint32_t MULx_H;
        uint32_t MULx_M;
        uint8_t  MULx_L;
        uint8_t  decExp;
        int16_t  binExp;
      } dec_scale_tab_entry_t;
      static const dec_scale_tab_entry_t DecScaleTab[2][3] = {
        { // scale up
          { 0xFC6F7C40, 0x45812296, 0x4D, 31, 103,}, // (10**31/2**103)*2**72
          { 0xFA000000, 0x00000000, 0x00,  3,  10,}, // (10**3 /2**10 )*2**72
          { 0xA0000000, 0x00000000, 0x00,  1,   4,}, // (10**0 /2**4  )*2**72
        },
        { // scale down
          { 0xE45C10C4, 0x2A2B3B05, 0x8D, 44,-146,}, // (2**146/10**44)*2**72
          { 0xD6BF94D5, 0xE57A42BC, 0x3D,  7, -23,}, // (2**23 /10**7 )*2**72
          { 0xCCCCCCCC, 0xCCCCCCCC, 0xCD,  1,  -3,}, // (2**3  /10**1 )*2**72
        },
      };
      const dec_scale_tab_entry_t* pTab = &DecScaleTab[sexp][0];
      unsigned decExp;
      while ((decExp=pTab->decExp) > mexp)
        ++pTab;

      const uint32_t MULx_L = pTab->MULx_L;
      const uint32_t MULx_M = pTab->MULx_M;
      const uint32_t MULx_H = pTab->MULx_H;
      const uint64_t MULx_HM = ((uint64_t)MULx_H << 32) + MULx_M;
      const int      binExp = pTab->binExp;
      do {
        // multiply by 10**N, where N= (-44, -7, -1, 1, 3, 31)
        // w1*mh
        //    w1*ml
        //    w0*mh
        unsigned __int128 w1w0 = (unsigned __int128)w1 * MULx_HM;
        w1w0 += (w1 >>  8) * MULx_L;
        w1w0 += (w0 >> 32) * MULx_H;
        w0 = (uint64_t)w1w0;
        w1 = (uint64_t)(w1w0 >> 64);
        bine += binExp;
        mexp -= decExp;
      } while (mexp >= decExp);
    }
    // printf("%08x:%08x:%08x\n", w2, w1, w0);
    w1 |= ((w0>>54) != 0); // approximately 9-10 MS bits of w0 are good. The rest is garbage
    w1 |= sticky;
    uret = to_double(w1, bine);
  }

  done:
  if (endptr)
    *endptr = (char*)endptrval;

  uret |= (uint64_t)neg << 63; // set sign bit
  double dret;
  memcpy(&dret, &uret, sizeof(dret));

  return dret;
}
