#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
// #include <stdlib.h>

enum {
  INPLEN_MAX = 100000, // maximal length of legal input string, not including leading whitespace characters and sign
  PARSE_DIG  = 17,
};

typedef struct {
  uint64_t    mnt;
  const char* nz0;    // first non-zero digit
  const char* nzlast; // last non-zero digit
  const char* eom;    // end of part of mantissa accumulated within mnt
  const char* dot;    // dot character. Recorded only when dot encountered after first non-zero digit
  int         dotI;
  int         eomI;   // end of part of mantissa accumulated within mnt
  int         decExp;
} parse_t;

static uint64_t quickCore(uint64_t mntL, uint64_t mntH, int decExp, bool* done);
static int compareSrcWithMidpoint(parse_t* src, uint64_t u); // return -1,0,+1 when source string respectively <, = or > of u2d(u)+0.5ULP

static double u2d(uint64_t x) {
  double y;
  memcpy(&y, &x, sizeof(y));
  return y;
}

static const char* parseTail(parse_t* dst, const char* str, int i)
{
  if (dst->dotI < 0) // there was no dot
    dst->dotI = i;   // implied dot after mantissa

  const char* ret = &str[i];
  switch (str[i]) {
    case 'e':
    case 'E':
      break; // exponent

    default:
      return ret; // done
  }

  // exponent
  ++i;
  // process sign
  char neg = str[i];
  switch (neg) {
    case '+':
    case '-':
      ++i;
      break;
    default:
      break;
  }

  if ((unsigned)(str[i]-'0') > 9)
    return ret; // no exponent, done

  // look for the first non-zero digit
  while (str[i] == '0') {
    if (i == INPLEN_MAX)
      return NULL; // input too long
    ++i;
  }

  // accumulate decExp
  int decExp = 0;
  do {
    unsigned char dig = *(unsigned char*)&str[i] - '0';
    if (dig > 9)
      break;
    decExp = decExp * 10 + dig;
    ++i;
  } while (decExp < INPLEN_MAX*2);
  if (neg=='-')
    decExp = -decExp;
  dst->decExp = decExp;

  // look for the end of exponent
  for (; i < INPLEN_MAX; ++i) {
    if (*(unsigned char*)&str[i] - '0' > 9)
      return &str[i]; // end found
  }

  return NULL; // input too long
}

static const char* parse(parse_t* dst, const char* str)
{
  int  i  = 0;
  char c0 = str[0];
  char dotC = '.';
  dst->dotI = -1; // no dot
  if ((unsigned)(c0-'0') > 9) {
    if (c0 != dotC)
      return NULL; // illegal input
    dst->dotI = 1; // record the next index, when dot found before the end of mnt
    dotC = '0';
    if ((unsigned)(str[1]-'0') > 9)
      return NULL; // illegal input
    i = 1;
  }
  // input is legal unless too long

  // look for the first non-zero digit
  for (;;) {
    while (str[i] == '0') {
      if (i == INPLEN_MAX)
        return NULL; // input too long
      ++i;
    }

    c0 = str[i];
    if ((unsigned)(c0-'0') <= 9)
      break; // non-zero digit found

    if (c0 != dotC)
      return parseTail(dst, str, i);

    // dot found
    dst->dotI = i+1; // record the next index, when dot found before the end of mnt
    dotC = '0';
    ++i;
    // continue
  }

  // accumulate mantissa
  dst->nz0 = &str[i];
  uint64_t mnt = 0;
  for (int k = 0; k < PARSE_DIG;) {
    unsigned char dig = *(unsigned char*)&str[i] - '0';
    if (dig <= 9) {
      mnt = mnt * 10 + dig;
      ++k;
    } else if (str[i] == dotC) {
      // dot found
      dst->dot = &str[i];
      dst->dotI = i+1; // record the next index, when dot found before the end of mnt
      dotC = '0';
    } else {
      break;
    }
    ++i;
  }
  dst->mnt  = mnt;
  dst->eomI = i;
  dst->eom  = &str[i];

  // look for the end of mantissa
  int nzlast_i = -1;
  for (; i < INPLEN_MAX; ++i) {
    c0 = str[i];
    if (c0 >= '0' && c0 <= '9') {
      if (c0 != '0')
        nzlast_i = i;
    } else if (c0 == dotC) {
      // dot found
      dst->dot = &str[i];
      dst->dotI = i;
      dotC = '0';
    } else {
      // end of mantissa found
      if (nzlast_i >= 0)
        dst->nzlast = &str[nzlast_i];
      return parseTail(dst, str, i);
    }
  }

  return NULL; // input too long
}

static const char* find_nzlast(const char* beg, const char* end)
{
  const char* ret = beg;
  while (beg != end) {
    char c = *beg;
    if (c >= '1' && c <= '9')
      ret = beg;
    ++beg;
  }
  return ret;
}

double my_strtod(const char* str, char** str_end)
{
  // discard leading whitespace characters
  const char* pstr = str;
  while (isspace(*pstr)) ++pstr;

  // process sign
  char neg = *pstr;
  switch (neg) {
    case '+':
    case '-':
      ++pstr;
      break;
    default:
      break;
  }

  if (str_end)
    *str_end = (char*)str;

  parse_t prs = {0};
  const char* endp= parse(&prs, pstr);
  if (endp == 0)
    return 0;

  if (str_end)
    *str_end = (char*)endp;

  uint64_t signBit = (neg=='-') ? (uint64_t)1 << 63 : 0;
  if (prs.mnt == 0)
    return u2d(signBit);

  int decExp = prs.decExp + prs.dotI - prs.eomI;

  if (decExp < -324-PARSE_DIG)
    return u2d(signBit);

  const uint64_t uINF = (uint64_t)2047 << 52;
  if (decExp > 308)
    return u2d(uINF+signBit);

  bool done;
  uint64_t uRet = quickCore(prs.mnt, prs.mnt + (prs.nzlast != 0), decExp, &done);
  if (done)
    return u2d(uRet+signBit);

  // Blitzkrieg didn't work, let's do it slowly
  if (prs.nzlast==0)
    prs.nzlast = find_nzlast(prs.nz0, prs.eom);

  int cmp = compareSrcWithMidpoint(&prs, uRet);
  cmp |= uRet & 1; // break tie to even
  uRet += (cmp > 0);
  return u2d(uRet+signBit);
}

const static uint64_t tab1[28] = { // 5**k
  1ull,
  5ull,
  25ull,
  125ull,
  625ull,
  3125ull,
  15625ull,
  78125ull,
  390625ull,
  1953125ull,
  9765625ull,
  48828125ull,
  244140625ull,
  1220703125ull,
  6103515625ull,
  30517578125ull,
  152587890625ull,
  762939453125ull,
  3814697265625ull,
  19073486328125ull,
  95367431640625ull,
  476837158203125ull,
  2384185791015625ull,
  11920928955078125ull,
  59604644775390625ull,
  298023223876953125ull,
  1490116119384765625ull,
  7450580596923828125ull,
};

const static uint64_t tab28[25] = { // 10**((k-13)*28) * 2**ceil((13-k)*93.0139866568461+64)
  0xe1afa13afbd14d6d, //  10**(-364) * 2**(64+1209)
  0xe3e27a444d8d98b7, //  10**(-336) * 2**(64+1116)
  0xe61acf033d1a45df, //  10**(-308) * 2**(64+1023)
  0xe858ad248f5c22c9, //  10**(-280) * 2**(64+ 930)
  0xea9c227723ee8bcb, //  10**(-252) * 2**(64+ 837)
  0xece53cec4a314ebd, //  10**(-224) * 2**(64+ 744)
  0xef340a98172aace4, //  10**(-196) * 2**(64+ 651)
  0xf18899b1bc3f8ca1, //  10**(-168) * 2**(64+ 558)
  0xf3e2f893dec3f126, //  10**(-140) * 2**(64+ 465)
  0xf64335bcf065d37d, //  10**(-112) * 2**(64+ 372)
  0xf8a95fcf88747d94, //  10**( -84) * 2**(64+ 279)
  0xfb158592be068d2e, //  10**( -56) * 2**(64+ 186)
  0xfd87b5f28300ca0d, //  10**( -28) * 2**(64+  93)
  0x8000000000000000, //  10**(   0) * 2**(64-   1)
  0x813f3978f8940984, //  10**(  28) * 2**(64-  94)
  0x82818f1281ed449f, //  10**(  56) * 2**(64- 187)
  0x83c7088e1aab65db, //  10**(  84) * 2**(64- 280)
  0x850fadc09923329e, //  10**( 112) * 2**(64- 373)
  0x865b86925b9bc5c2, //  10**( 140) * 2**(64- 466)
  0x87aa9aff79042286, //  10**( 168) * 2**(64- 559)
  0x88fcf317f22241e2, //  10**( 196) * 2**(64- 652)
  0x8a5296ffe33cc92f, //  10**( 224) * 2**(64- 745)
  0x8bab8eefb6409c1a, //  10**( 252) * 2**(64- 838)
  0x8d07e33455637eb2, //  10**( 280) * 2**(64- 931)
  0x8e679c2f5e44ff8f, //  10**( 308) * 2**(64-1024)
};


static uint64_t ldexp_u(uint64_t m2, int be)
{
  const uint64_t uINF = (uint64_t)2047 << 52;
  be += 64+1023+63; // biased exponent
  if (be > 1023*2)
    return uINF;

  uint64_t mnt = m2 >> 11;
  int mnt_bits = 53;
  if (be < 1) {
    // subnormal
    int rsh = 1-be;
    mnt_bits -= rsh;
    be = 0;
    if (mnt_bits < 0)
      return 0;
    mnt >>= rsh;
  }
  uint64_t tail = m2 << mnt_bits;
  tail |= (mnt & 1);           // break tie to even
  mnt &= ((uint64_t)-1 >>12);  // mask out implied '1'
  mnt |= (uint64_t)be << 52;   // + biased exponent
  mnt += tail > ((uint64_t)1 << 63); // round to nearest
  return mnt;
}

#ifdef _MSC_VER
static __inline int __builtin_clzll(uint64_t x) {
  unsigned long iMsb;
  _BitScanReverse64(&iMsb, x);
  return 63 - iMsb;
}
#endif

static uint64_t quickCore(uint64_t mntL, uint64_t mntU, int decExp, bool* done)
{
  int ie = decExp + 13*28;
  int iH = ie / 28; // index in tab28
  int iL = ie % 28; // index in tab1

  // multiply mntL,mntH by 10**decExp
#ifdef _MSC_VER
  uint64_t m1L, m2L, m1U, m2U;
  m1L = _umul128(mntL, tab1[iL], &m2L);
  m1U = _umul128(mntU, tab1[iL], &m2U);
#else
  unsigned __int128 mxL = (unsigned __int128)mntL * tab1[iL];
  unsigned __int128 mxU = (unsigned __int128)mntU * tab1[iL];
  uint64_t m2L = (uint64_t)(mxL >> 64);
  uint64_t m1L = (uint64_t)(mxL);
  uint64_t m2U = (uint64_t)(mxU >> 64);
  uint64_t m1U = (uint64_t)(mxU);
#endif
  int beL = iL, beU = beL; // binary exponent
  uint64_t m0L = 0;
  uint64_t m0U = 0;
  // up to this point multiplication is exact
  if (iH != 13) {
    // this multiplication is approximate, so we need different coefficients for upper and lower estimates
    beL += (int)ceil((iH-13)*93.0139866568461);
    beU = beL;
    uint64_t x28 = tab28[iH];
#ifdef _MSC_VER
    uint64_t m0Lh;
    m0L = _umul128(m1L, x28, &m0Lh);
    m1L = _umul128(m2L, x28, &m2L);
    unsigned char carry = _addcarry_u64(0, m1L, m0Lh, &m1L);
    _addcarry_u64(carry, m2L, 0, &m2L);
#else
    unsigned __int128 mlL, mlU;
    mlL = (unsigned __int128)m1L * x28;
    mxL = (unsigned __int128)m2L * x28;
    mxL += (uint64_t)(mlL >> 64);
    m2L = (uint64_t)(mxL >> 64);
    m1L = (uint64_t)(mxL);
    m0L = (uint64_t)(mlL);
#endif

#ifdef _MSC_VER
    uint64_t m0Uh;
    m0U = _umul128(m1U, x28+1, &m0Uh);
    m1U = _umul128(m2U, x28+1, &m2U);
    carry = _addcarry_u64(0, m1U, m0Uh, &m1U);
    _addcarry_u64(carry, m2U, 0, &m2U);
#else
    mlU = (unsigned __int128)m1U * (x28+1);
    mxU = (unsigned __int128)m2U * (x28+1);
    mxU += (uint64_t)(mlU >> 64);
    m2U = (uint64_t)(mxU >> 64);
    m1U = (uint64_t)(mxU);
    m0U = (uint64_t)(mlU);
#endif

    if (m2L == 0) {
      beL -= 64;
      m2L = m1L; m1L = m0L; m0L = 0;
    }

    if (m2U == 0) {
      beU -= 64;
      m2U = m1U; m1U = m0U; m0U = 0;
    }
  }

  if (m2L == 0) {
    beL -= 64;
    m2L = m1L; m1L = 0;
  }

  if (m2U == 0) {
    beU -= 64;
    m2U = m1U; m1U = 0;
  }

  // normalize m2L:M1L
  int lsh = __builtin_clzll(m2L);
  if (lsh) {
    m2L = (m2L << lsh) | (m1L >> (64-lsh));
  }
  beL -= lsh;

  // normalize m2U:M1U
  lsh = __builtin_clzll(m2U);
  if (lsh) {
    m2U = (m2U << lsh) | (m1U >> (64-lsh));
    m1U = (m1U << lsh);
  }
  beU -= lsh;
  m2U |= ((m1U|m0U) != 0); // round up

  *done = true;
  uint64_t resL = ldexp_u(m2L, beL);
  if (m2L != m2U || beL != beU)
    *done = ldexp_u(m2U, beU)==resL;

  return resL;
}

static int mp_mulw(uint64_t x[], uint64_t y, int nwords, uint64_t acc)
{ // in-place multiply vector x by scalar y and add scalar acc
  for (int i = 0; i < nwords; ++i) {
#ifdef _MSC_VER
    uint64_t xy0, xy1;
    xy0 = _umul128(x[i], y, &xy1);
    _addcarry_u64(
      _addcarry_u64(0, xy0, acc, &x[i]),
                       xy1, 0,   &acc);
#else
    unsigned __int128 xy = (unsigned __int128)x[i] * y + acc;
    x[i] = (uint64_t)xy;
    acc  = (uint64_t)(xy >> 64);
#endif
  }
  x[nwords] = acc;
  return nwords + (acc != 0); // number of result words
}

// return -1,0,+1 when source string respectively <, = or > of u2d(u)+0.5ULP
static int compareSrcWithMidpoint(parse_t* src, uint64_t u)
{
  // parse u as binary64
  const uint64_t BIT53 = (uint64_t)1 << 53;
  const uint64_t MSK53 = BIT53 - 1;
  const uint64_t BIT54 = (uint64_t)1 << 54;
  const uint64_t MSK54 = BIT54 - 1;

  uint64_t mnt = ((u*2) & MSK53) | 1 | BIT53;// += 0.5 ULP
  int biasedExp = u >> 52;
  int nbits = 54;       // number of significant bits in mnt
  if (biasedExp == 0) { // subnormal
    mnt       &= MSK53;
    biasedExp  = 1;
    nbits = 64 - __builtin_clzll(mnt); // number of significant bits in mnt
  }

  int be  = biasedExp - 1023 - 53; // binary exponent
  int nBe = -be;                   // val = mnt*2**(-nBe)
  uint64_t x[18]; // mantissa words, LS words first
  const char* str = src->nz0;
  const char* end_str = src->nzlast + 1;
  if (nbits + be > 0) {
    // x = mnt * 2**be >= 1.0, so it has an integer part

    // Calculate number of decimal digits in the integer part of the source string
    int nParseAccDig = (int)(src->eom - src->nz0); // number characters between start and end of accumulation
    if (src->dot && src->dot < src->eom)
      nParseAccDig -= 1; // one of the character was dot rather than digit
    int nDigitInt = src->decExp + src->dotI - src->eomI + nParseAccDig;

    // Convert integer part of the source string to binary
    x[0] = 0;
    int nwords = 0; // number of words in x[], including partial word
    const char* dot = src->dot;
    if (dot && dot >= end_str)
      dot = NULL;
    int remDig = nDigitInt;
    while (remDig > 0) {
      int contDig = dot ? (int)(dot - str) : (int)(end_str - str);
      if (contDig > remDig)
        contDig = remDig;
      remDig -= contDig;
      do {
        int nd = contDig < 19 ? contDig : 19;
        uint64_t acc = 0;
        int k = nd;
        do {
          acc = acc * 10 + (*str - '0');
          ++str;
        } while (--k);
        nwords = mp_mulw(x, tab1[nd] << nd, nwords, acc); // x = x * 10**nDig + acc
        contDig -= 19;
      } while (contDig > 0);

      if (str==dot) {
        dot = NULL;
        str += 1;
      }
      if (str==end_str)
        break;
    }

    // if no more digits then multiply by remaining power of 10
    while (remDig > 0) {
      int nd = remDig < 19 ? remDig : 19;
      nwords = mp_mulw(x, tab1[nd] << nd, nwords, 0); // x = x * 10**nDig + acc
      remDig -= 19;
    }

    // extract bits of x aligned to bits in mntat the same position a
    uint64_t sMnt, sRem = 0, xMnt = mnt;
    if (be > 0) {
      int wi = be / 64;
      int bi = be % 64;
      sMnt = x[wi+0];
      if (bi != 0) {
        sRem = sMnt << (64-bi);
        sMnt = (sMnt >> bi) | (x[wi+1] << (64-bi));
      }
      sMnt &= MSK54;
    } else {
      sMnt = x[0];
      xMnt >>= -be;
    }

    // compare integer parts
    if (sMnt != xMnt)
      return sMnt < xMnt ? -1 : 1;

    if (be >= 0) {
      if (str < end_str)
        return 1;  // source is bigger, because there are more non-zero digits after converted part

      if (sRem != 0)
        return 1;  // source is bigger
      // look at lower words of x
      int nw = be/64;
      for (int i = 0; i < nw; ++i)
        if (x[i] != 0)
          return 1; // source is bigger
      return 0; // equal
    }

    // Both integer and fractional part present and integer parts are equal
    // Let's compare fractional parts
    x[0] = mnt & ((uint64_t)-1 >> (64 + be));
    x[1] = 0;
    x[2] = 0;
  } else {
    // x = mnt * 2**be < 1.0, so only fractional part present
    x[0] = mnt;
                    // nbits == number of significant bits in x[]
    int nwords = 1; // number of words in x[], including partial word
    // multiply by power of 10 until val > 0
    do {
      int nDig = (((nBe + 1 - nbits)*(uint64_t)1292913986)>>32) + 1;
      if (nDig > 27)
        nDig = 27;
      // nDig is chosen to produce 1 or 2 digits above decimal point on the last step
      nwords = mp_mulw(x, tab1[nDig], nwords, 0); // x *= 5**nDig
      nbits  = nwords*64 - __builtin_clzll(x[nwords-1]);
      nBe   -= nDig;
    } while (nbits <= nBe);
    x[nwords+0] = 0;
    x[nwords+1] = 0;

    // extract integer part (one word)
    unsigned wi = nBe/64;
    unsigned bi = nBe%64;
    uint64_t x0 = x[wi+0];
    uint64_t x1 = x[wi+1];
    x[wi+0] = 0;
    x[wi+1] = 0;
    uint64_t xw = x0;
    if (bi != 0) {
      xw = (xw >> bi) | (x1 << (64-bi));
      x[wi+0] = x0 & (((uint64_t)-1) >> (64-bi));
    }

    // compare MS digits
    int sdig = *str++ - '0'; // no need to test the first character for dot
    int xdig = xw < 10 ? (int)xw : (int)(xw/10);
    if (sdig != xdig) {
      int diff = (sdig - xdig + 15) % 10 - 5; // handle case 0.9x vs 1.x
      return diff < 0 ? -1 : 1;
    }
    if (xw >= 10) {
      xdig = xw % 10;
      sdig = 0;
      if (str != end_str) {
        char c = *str++;
        if (c== '.') c = *str++;
        sdig = c - '0';
      }
      if (sdig != xdig)
        return sdig < xdig ? -1 : 1;
    }
  }

  // compare the rest of fractional bits, 27 digits at time
  while (nBe > 0) {
    int nDig = nBe > 27 ? 27 : nBe;
    mp_mulw(x, tab1[nDig], (nBe-1)/64+1, 0); // x *= 5**nDig
    nBe -= nDig;
    // extract integer part (2 words)
    unsigned wi = nBe/64;
    unsigned bi = nBe%64;
    uint64_t x0 = x[wi+0];
    uint64_t xw0 = x0;
    uint64_t xw1 = x[wi+1];
    x[wi+0] = 0;
    x[wi+1] = 0;
    if (bi != 0) {
      xw0 = (xw0 >> bi) | (xw1 << (64-bi));
      xw1 = (xw1 >> bi) | (x[wi+2] << (64-bi));
      x[wi+0] = x0 & (((uint64_t)-1) >> (64-bi));
      x[wi+2] = 0;
    }

    // read nDig digits from source and convert to binary
    uint64_t sw0 = 0;
    uint64_t sw1 = 0;
    if (str != end_str) {
      int cnt = nDig;
      do {
        sw1 = sw0;
        sw0 = 0;
        int nd = cnt > 9 ? cnt - 9 : cnt;
        cnt -= nd;
        if (str != end_str) {
          do {
            int dig = 0;
            if (str != end_str) {
              char c = *str++;
              if (c== '.') c = *str++;
              dig = c - '0';
            }
            sw0 = sw0*10 + dig;
          } while (--nd);
        }
      } while (cnt > 0);
    }
    const uint64_t pw9 = 1000000000ull;
#ifdef _MSC_VER
    uint64_t sq0, sq1;
    sq0 = _umul128(sw1, pw9, &sq1);
    _addcarry_u64(
      _addcarry_u64(0, sw0, sq0, &sw0),
                       sq1, 0,   &sw1);
#else
    unsigned __int128 sq = (unsigned __int128)sw1 * pw9 + sw0;
    sw1 = (uint64_t)(sq >> 64);
    sw0 = (uint64_t)(sq);
#endif
    if (sw1 != xw1)
      return sw1 < xw1 ? -1 : 1;
    if (sw0 != xw0)
      return sw0 < xw0 ? -1 : 1;
  }

  if (str < end_str)
    return 1;

  return 0;
}