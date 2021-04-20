#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <fenv.h>
#include <locale.h>

#ifdef __GNUC__
#define LIKELY(x)       __builtin_expect((x),1)
#define UNLIKELY(x)     __builtin_expect((x),0)
#else
#define LIKELY(x)       x
#define UNLIKELY(x)     x
#endif

#define MNT_MAX ((uint64_t)-1)

enum {
  INPLEN_MAX = 100000, // maximal length of mantissa part of legal input string, not including leading whitespace characters and sign
};

typedef struct {
  uint64_t    mnt;
  const char* eom;    // end of part of mantissa accumulated within mnt
  const char* lastDig;// last non-zero digit of mantissa. Recorded only when there is at least one non-zero digit after eom
  const char* dot;    // dot character. Recorded only when dot encountered at or after eom
  int         decExp;
} parse_t;

static int compareSrcWithThreshold(parse_t* src, uint64_t u, int roundingMode); // return -1,0,+1 when source string respectively <, = or > of u2d(u)+0.5ULP

static double u2d(uint64_t x) {
  double y;
  memcpy(&y, &x, sizeof(y));
  return y;
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

const static uint64_t tab303[] = {
 11,                 // nwords
 303,
 0x80a8ab58d818ff0d, // 5**303
 0xd82ee807acb4e04a,
 0x3f2f7c3c7d52768c,
 0x592b1ec0db4fd779,
 0x5bbdb4201a048818,
 0xd490df5ae941dd25,
 0x5487f097ff592863,
 0xd6898606dc1740fd,
 0xbe643f001dea2bc7,
 0xd30560258f54e6ba,
 0xbaa718e68396cffd,
};

const static uint64_t tab220[] = {
 8,                  // nwords
 220,
 0x60c58d209ab55311, // 5**220
 0xa1c8387566126cba,
 0xc44e8767587f4c16,
 0x908059e41a047cf2,
 0x7cfc8e8a0ba063ec,
 0xf0144b2e1fac055e,
 0x172257324207eb0e,
 0x71505aee4b8f981d,
};

static uint64_t ldexp_u(uint64_t m56, int be, int roundingMode)
{
  const uint64_t uINF = (uint64_t)2047 << 52;
  be += 64+1023+63; // biased exponent
  if (be > 1023*2)
    return uINF;

  uint64_t mnt = m56 >> 3; // isolate data bits
  int mnt_bits = 53+8;
  if (be < 1) {
    // subnormal
    int rsh = 1-be;
    mnt_bits -= rsh;
    be = 0;
    if (mnt_bits < 0)
      return roundingMode == FE_UPWARD ? 1 : 0;
    mnt >>= rsh;
  }
  uint64_t tail = m56 << mnt_bits;          // shift away data bits
  uint64_t res = mnt & ((uint64_t)-1 >>12); // mask out implied '1'
  res |= (uint64_t)be << 52;                // + biased exponent
  if (roundingMode == FE_TONEAREST) {
    tail |= (mnt & 1);                 // break tie to even
    res += tail > ((uint64_t)1 << 63); // round to nearest
  } else if (roundingMode == FE_UPWARD) {
    res += (tail != 0);                // round up
  }
  return res;
}

#ifdef _MSC_VER
static __inline int __builtin_clzll(uint64_t x) {
  unsigned long iMsb;
  _BitScanReverse64(&iMsb, x);
  return 63 - iMsb;
}
#endif

static bool is_case_insensitively_equal(const char* x, const char* uppercaseRef, unsigned len) {
  for (unsigned i = 0; i < len; ++i)
    if (toupper(x[i]) != uppercaseRef[i])
      return false;
  return true;
}

double my_strtod(const char* str, char** str_end)
{
  if (str_end)
    *str_end = (char*)str;

  // discard leading whitespace characters
  while (isspace(*str)) ++str;

  // process sign
  char neg = *str;
  switch (neg) {
    case '+':
    case '-':
      ++str;
      break;
    default:
      break;
  }

  if (str_end)
    *str_end = (char*)str;

  struct lconv *lc = localeconv();
  char dotC = lc->decimal_point[0];
  const char* effDot = NULL; // no dot
  if (*str == dotC) { // dot found before the 1st digit
    ++str;
    effDot = str; // record the next position, when dot found before the end of mnt
    dotC = '0';
  }

  const uint64_t uINF = (uint64_t)2047 << 52;
  const uint64_t uNaN = (uint64_t)-1 >> 1;

  // accumulate mantissa
  uint64_t signBit = (neg=='-') ? (uint64_t)1 << 63 : 0;
  const char* p = str;
  const uint64_t MNT_LIMIT = (MNT_MAX - 9)/10;
  uint64_t mnt = 0;
  const char* eom = NULL;    // end of part of mantissa accumulated within mnt
  const char* dot = NULL;    // dot character. Recorded only when dot encountered at or after eom
  const char* lastDig = NULL;// last non-zero digit of mantissa. Recorded only when there is at least one non-zero digit after eom
  for (;;) {
    for (;;) {
      unsigned char dig = *(unsigned char*)p - '0';
      if (dig > 9)
        break; // non-digit
      ++p;
      mnt = mnt * 10 + dig;
      if (UNLIKELY(mnt > MNT_LIMIT)) {
        // No more room in mnt.
        eom  = p;
        // Scan throw the rest of mantissa digits
        for (;;) {
          char c = *p;
          while (c >= '0' && c <= '9')
            c = *++p;
          if (c != dotC)
            break;
          // dot found
          dot = effDot = p;
          dotC = '0';
          ++p;
        }
        if (p > eom) {
          // look for last non-zero digit
          lastDig = p - 1;
          while (*lastDig == '0') --lastDig;
          if (lastDig == dot) {
            --lastDig;
            while (*lastDig == '0') --lastDig;
          }
          if (lastDig < eom)
            lastDig = NULL;
        }
        goto mantissa_done;
      }
    }
    // non-digit
    if (*p != dotC) {
      eom  = p;
      // check if there were digits
      if (p==str) { // there were no digits
        uint64_t ret = 0;
        if (effDot == 0) {
          // look for Inf/Nan
          if (is_case_insensitively_equal(p, "INF", 3)) {
            ret = uINF;
            p += 3; // "inf" found, but it could be "infinity"
            if (is_case_insensitively_equal(p, "INITY", 5))
              p += 5;
          } else if (is_case_insensitively_equal(&p[1], "NAN", 3)) {
            ret = uNaN;
            p += 3;
          }
        }
        if (ret) {
          ret |= signBit;
          if (str_end)
            *str_end = (char*)p;
        }
        return u2d(ret);
      }
      break;
    }
    // dot found
    effDot = p + 1; // record the next position, when dot found before the end of mnt
    dotC = '0';
    ++p;
  }
  mantissa_done:

  if (p-str >= INPLEN_MAX)
    return 0; // input too long

  // parse part of the string after last digit of mantissa
  if (!effDot) // there was no dot
    effDot = p;
  int decExp = (int)(effDot - eom);

  const char* ret_end = p;
  switch (*p) {
    case 'e':
    case 'E':
    { // exponent
      ++p;
      // process sign
      char expNeg = *p;
      switch (expNeg) {
        case '+':
        case '-':
          ++p;
          break;
        default:
          break;
      }
      if (*p >= '0' && *p <= '9') { // exponent present
        // accumulate decExp
        int decExpAcc = 0;
        for (;;) {
          unsigned dig = *(unsigned char*)p - '0';
          if (dig > 9)
            break;
          ++p;
          if (LIKELY(decExpAcc < INPLEN_MAX*2))
            decExpAcc = decExpAcc * 10 + dig;
        }
        if (expNeg=='-')
          decExpAcc = -decExpAcc;
        decExp += decExpAcc;
        ret_end = p;
      }
    }
      break; // exponent

    default:
      break;
  }

  if (str_end)
    *str_end = (char*)ret_end;

  // Convert to floating point
  // Calculate upper and lower estimates
  if (mnt == 0)
    return u2d(signBit);

  if (decExp > 308)
    return u2d(uINF+signBit);

  int roundingMode = fegetround();
  // translate up/down rounding modes to toward zero/away from zero (represented by FE_UPWARD)
  switch (roundingMode) {
    case FE_DOWNWARD:
      roundingMode = signBit ? FE_UPWARD : FE_TOWARDZERO;
      break;
    case FE_UPWARD:
      roundingMode = signBit ? FE_TOWARDZERO : FE_UPWARD;
      break;
    default:
      break;
  }

  if (decExp < -342)
    return u2d(roundingMode!=FE_UPWARD ? signBit : signBit | 1);

  // decExp range [-342:308]
  int ie = decExp + 13*28; // range [22:672;
  int iH = ie / 28; // index in tab28, range [0:24]
  int iL = ie % 28; // index in tab1,  range [0:27]

  uint64_t mntL = mnt;
  uint64_t mntU = mntL + (lastDig != 0);
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
  int be = iL; // binary exponent
  uint64_t m0L = 0;
  uint64_t m0U = 0;
  // up to this point multiplication is exact
  if (iH != 13) {
    // this multiplication is approximate, so we need different coefficients for upper and lower estimates
    // be += (int)ceil((iH-13)*93.0139866568461);
    be += (((iH-13)*24383059) >> 18) + 1;
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

    if (m2U == 0) {
      be -= 64;
      m2L = m1L; m1L = m0L; m0L = 0;
      m2U = m1U; m1U = m0U; m0U = 0;
    }
  }

  if (m2U == 0) {
    be -= 64;
    m2L = m1L; m1L = 0;
    m2U = m1U; m1U = 0;
  }

  // normalize m2U:M1U
  int lsh = __builtin_clzll(m2U);
  if (lsh) {
    m2L = (m2L << lsh) | (m1L >> (64-lsh));
    m1L = (m1L << lsh);
    m2U = (m2U << lsh) | (m1U >> (64-lsh));
    m1U = (m1U << lsh);
  }
  be -= lsh;

  // Pack m2L/M2U to 56 bits
  // m2U consists of 53 data bits, 1 guard bit and 2 sticky bits
  // Since m2L could be not fully normalized, it can contain up to 54 data bits
  m2U = (m2U >> 8) | (((m2U & 255)|m1U|m0U) != 0); // set sticky bit
  m2L = (m2L >> 8) | (((m2L & 255)|m1L|m0L) != 0); // set sticky bit

  uint64_t res, resU;
  for (uint64_t m2 = m2U;;) {
    res = ldexp_u(m2, be, roundingMode);
    if (m2 == m2L)
      break;

    // 56-bit estimates of mantissa differ
    // normalize m2L (at most by 1 bit position)
    resU = res;
    const uint64_t BIT55 = (uint64_t)1 << 55;
    if (m2L < BIT55) {
      m2L += m2L;
      be  -= 1;
    }
    m2 = m2L;
  }

  if (UNLIKELY(m2U != m2L && res != resU)) {
    // Blitzkrieg didn't work, let's do it slowly
    parse_t prs;
    prs.mnt     = mnt;
    prs.eom     = eom;
    prs.lastDig = lastDig;
    prs.dot     = dot;
    prs.decExp  = decExp;
    int cmp = compareSrcWithThreshold(&prs, res, roundingMode);
    if (roundingMode == FE_TONEAREST) {
      cmp |= res & 1;   // break tie to even
      res += (cmp > 0);
    } else if (roundingMode == FE_UPWARD) {
      res += (cmp > 0);
    } else {
      res += (cmp >= 0);
    }
  }
  return u2d(res+signBit);
}

static int mp_mulw(uint64_t dst[], const uint64_t src[], uint64_t y, int nwords, uint64_t acc)
{ // Multiply vector src[] by scalar y and add scalar acc, store result is dst[]
  // src and dst can point to the same array
  for (int i = 0; i < nwords; ++i) {
#ifdef _MSC_VER
    uint64_t xy0, xy1;
    xy0 = _umul128(src[i], y, &xy1);
    _addcarry_u64(
      _addcarry_u64(0, xy0, acc, &dst[i]),
                       xy1, 0,   &acc);
#else
    unsigned __int128 xy = (unsigned __int128)src[i] * y + acc;
    dst[i] = (uint64_t)xy;
    acc  = (uint64_t)(xy >> 64);
#endif
  }
  dst[nwords] = acc;
  return nwords + (acc != 0); // number of result words
}

// mp_mulwsqr - multiply vector src[] by square of scalar, store result is dst[]
// src and dst can point to the same array
static void mp_mulwsqr(uint64_t dst[], const uint64_t src[], uint64_t y, int nwords)
{
  uint64_t acc1 = 0, acc2 = 0;
  for (int i = 0; i < nwords; ++i) {
#ifdef _MSC_VER
    uint64_t xy10, xy11, xy20, xy21;
    xy10 = _umul128(src[i], y, &xy11);
    _addcarry_u64(
      _addcarry_u64(0, xy10, acc1, &xy10),
                       xy11, 0,    &acc1);
    xy20 = _umul128(xy10, y, &xy21);
    _addcarry_u64(
      _addcarry_u64(0, xy20, acc2, &dst[i]),
                       xy21, 0,    &acc2);
#else
    unsigned __int128 xy1 = (unsigned __int128)src[i] * y + acc1;
    unsigned __int128 xy2 = (unsigned __int128)(uint64_t)xy1 * y + acc2;
    dst[i] = (uint64_t)xy2;
    acc1  = (uint64_t)(xy1 >> 64);
    acc2  = (uint64_t)(xy2 >> 64);
#endif
  }
#ifdef _MSC_VER
  uint64_t xyLast0, xyLast1;
  xyLast0 = _umul128(acc1, y, &xyLast1);
  _addcarry_u64(
    _addcarry_u64(0, xyLast0, acc2, &dst[nwords+0]),
                     xyLast1, 0,    &dst[nwords+1]);
#else
  unsigned __int128 xyLast = (unsigned __int128)acc1 * y + acc2;
  dst[nwords+0] = (uint64_t)xyLast;
  dst[nwords+1] = (uint64_t)(xyLast >> 64);
#endif
}

static uint64_t Ascii18ToBin(const char* src) {
  uint32_t accH = src[9*0];
  uint32_t accL = src[9*1];
  for (int i = 1; i < 9; ++i) {
    accH = accH*10 + src[i+9*0];
    accL = accL*10 + src[i+9*1];
  }
  const uint32_t adj9 = 111111111u * '0';
  accH -= adj9;
  accL -= adj9;
  return (uint64_t)accH*1000000000u + accL;
}

static void inline move8(char* dst)
{
  uint64_t tmp;
  memcpy(&tmp, dst+1, sizeof(tmp));
  memcpy(dst,   &tmp, sizeof(tmp));
}

// return -1,0,+1 when source string respectively <, = or > of u2d(u)+0.5ULP
static int compareSrcWithThreshold(parse_t* src, uint64_t u, int roundingMode)
{
  // parse u as binary64
  const uint64_t BIT53 = (uint64_t)1 << 53;
  const uint64_t MSK53 = BIT53 - 1;
  const uint64_t BIT54 = (uint64_t)1 << 54;
  const uint64_t MSK54 = BIT54 - 1;

  // calculate Thr
  if (roundingMode == FE_TOWARDZERO)
    u += 1; // += ULP, Thr = a next representable FP numbers
  uint64_t mnt = ((u*2) & MSK53) | BIT53;
  int biasedExp = u >> 52;
  if (biasedExp == 0) { // subnormal
    mnt       &= MSK53;
    biasedExp  = 1;
  }
  int nBe = 1023 + 53 - biasedExp; // reversed binary exponent. Thr = mnt*2**(-nBe)
  if (roundingMode == FE_TONEAREST)
    mnt += 1; // += 0.5 ULP, Thr = a mid point between representable FP numbers

  uint64_t x[18]; // buffer for mantissa words, LS words first.
                  // x[] hold mantissa either of integer part of the source string or of Thr

  const char* str = src->eom;
  const char* end_str = str;
  const char* nonDigit = str; // points to the first input character that shouldn't be treated as digit
  int nSrcDigits = 0;

  // find last non-zero digit
  const char* nzlast = src->lastDig;
  if (nzlast) {
    end_str  = nzlast + 1;
    nonDigit = end_str;
    nSrcDigits = (int)(end_str - str);
    if (src->dot != NULL && src->dot < end_str) {
      if (src->dot == str)
        str += 1;
      else
        nonDigit = src->dot;
      nSrcDigits -= 1;
    }
  }

  int srcDecExp = src->decExp;
  if (srcDecExp >= 0) { // src->mnt was scaled up or unscaled
    // Convert integer part of the source string to binary and store in x[]
    x[0] = src->mnt;
    int nwords = 1; // number of words in x[], including partial word

    int nCvtDigits = srcDecExp < nSrcDigits ? srcDecExp : nSrcDigits;
    srcDecExp  -= nCvtDigits;
    nSrcDigits -= nCvtDigits;
    // read source and accumulate in x[], 19 digits at time
    while (nCvtDigits >= 19) {
      nCvtDigits -= 19;
      char tmp[48];
      const char* pSrc = str;
      if (nonDigit-str < 19) {
        int contLen = (int)(nonDigit-str);
        if (contLen == 0) {
          pSrc = str + 1;
        } else {
          // copy source to continuous buffer
          memcpy(tmp, str, 20);
          move8(tmp+contLen+8*0);
          move8(tmp+contLen+8*1);
          move8(tmp+contLen+8*2);
          pSrc = tmp;
        }
        str += 1;
        // nonDigit was pointing to dot rather than to end_str
        nonDigit = end_str;
      }
      str += 19;
      uint64_t acc = Ascii18ToBin(pSrc)*10+(pSrc[18]-'0');
      nwords = mp_mulw(x, x, tab1[19] << 19, nwords, acc); // x = x * 10**19 + acc
    }

    if (nCvtDigits > 0) { // convert and add remaining digits
      uint64_t acc = 0;
      int nd = nCvtDigits;
      do {
        if (str != nonDigit)
          acc = acc * 10 + (*str - '0');
        ++str;
      } while (--nd);
      nwords = mp_mulw(x, x, tab1[nCvtDigits] << nCvtDigits, nwords, acc); // x = x * 10**nCvtDigits + acc
    }

    // No more source digits, multiply by remaining power of 10
    while (srcDecExp > 0) {
      int nd = srcDecExp < 19 ? srcDecExp : 19;
      nwords = mp_mulw(x, x, tab1[nd] << nd, nwords, 0); // x = x * 10**nDig + acc
      srcDecExp -= 19;
    }

    // extract bits of x aligned to bits in mnt
    uint64_t sMnt, sRem = 0;
    int be  = -nBe; // binary exponent of Thr. Thr = mnt*2**(be)
    // It can be proven mathematically that when PARSE_DIG >= 17 then be >= 0
    // extract sMnt from x starting from bit # be
    int wi = be / 64;
    int bi = be % 64;
    sMnt = x[wi+0];
    if (bi != 0) {
      sRem = sMnt << (64-bi);
      sMnt = (sMnt >> bi) | (x[wi+1] << (64-bi));
    }
    sMnt &= MSK54;

    // compare integer parts
    if (sMnt != mnt)
      return sMnt < mnt ? -1 : 1;

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

  // src->mnt was scaled down
  x[0] = mnt;
  int nwords = 1; // number of words in x[], including partial word
  // Multiply by power of 10 until integer part of x[] * 2**(-nBe) aligned with src->mnt
  // Implemented via multiplication of x[] by power of 5 with simultaneous subtraction from nBe
  nBe += srcDecExp;
  int multPowerOfTen = -srcDecExp;
  if (multPowerOfTen >= 220) {
    const uint64_t* pow5tab = (multPowerOfTen >= 303) ? tab303 : tab220;
    nwords = mp_mulw(x, &pow5tab[2], mnt, (int)pow5tab[0], 0); // x = 5**tabPow * mnt
    multPowerOfTen -= (int)pow5tab[1];
  }
  while (multPowerOfTen > 0) {
    int nDig = multPowerOfTen < 27 ? multPowerOfTen : 27;
    // a last nDig is chosen to align x[] with src->mnt
    nwords = mp_mulw(x, x, tab1[nDig], nwords, 0); // x *= 5**nDig
    multPowerOfTen -= 27;
  }
  x[nwords+0] = 0;
  x[nwords+1] = 0;
  x[nwords+2] = 0;

  // extract integer part (one word)
  {
  uint64_t xw;
  if (nBe >= 0) {
    unsigned wi = nBe/64;
    unsigned bi = nBe%64;
    uint64_t x0 = x[wi+0];
    uint64_t x1 = x[wi+1];
    x[wi+0] = 0;
    x[wi+1] = 0;
    xw = x0;
    if (bi != 0) {
      xw = (xw >> bi) | (x1 << (64-bi));
      x[wi+0] = x0 & (((uint64_t)-1) >> (64-bi));
    }
  } else { // nBe < 0
    xw = x[0] << (-nBe);
  }

  // compare with mnt
  if (src->mnt != xw)
    return src->mnt < xw ? -1 : 1;
  }

  // So far processed part of the source is equal to Thr,
  // Continue comparison of less significant digits
  if (nzlast == NULL) { // source does not contain any more significant digits
    if (nBe > 0) {
      // look for non-zero words in x[]
      int nw = (nBe - 1)/64 + 1; // number of words in x[]
      for (int i = 0; i < nw; ++i) {
        if (x[i] != 0)
          return -1; // source is below threshold
      }
    }
    return 0; // source == threshold
  }
  end_str = nzlast + 1;

  // compare the rest of fractional bits
  // x[] contains mantissa of Thr
  int nCmpDigits = nBe < nSrcDigits ? nBe : nSrcDigits;
  // compare by groups of 27*2 digits
  while (nCmpDigits >= 27*2) {
    mp_mulwsqr(x, x, tab1[27], (nBe-1)/64+1); // x *= (5**27*2)
    nCmpDigits -= 27*2;
    nBe        -= 27*2;

    // read 27*2 digits from source and convert to binary
    char tmp[120];
    const char* pSrc = str;
    if (nonDigit-str < 27*2) {
      int contLen = (int)(nonDigit-str);
      if (contLen == 0) {
        pSrc = str + 1;
      } else {
        // copy source to continuous buffer
        memcpy(tmp, str, 56);
        move8(tmp+contLen+8*0);
        move8(tmp+contLen+8*1);
        move8(tmp+contLen+8*2);
        move8(tmp+contLen+8*3);
        move8(tmp+contLen+8*4);
        move8(tmp+contLen+8*5);
        move8(tmp+contLen+8*6);
        pSrc = tmp;
      }
      str += 1;
      // nonDigit was pointing to dot rather than to end_str
      nonDigit = end_str;
    }
    str += 27*2;
    uint64_t sw[3];
    sw[0] = Ascii18ToBin(&pSrc[18*0]);
    sw[1] = Ascii18ToBin(&pSrc[18*1]);
    sw[2] = Ascii18ToBin(&pSrc[18*2]);

    // sw[] are at base=1E18, convert to true binary
    uint64_t sw0, sw1, sw2;
    const uint64_t pw18 = 1000000000000000000ull;
    #ifdef _MSC_VER
    uint64_t sq0, sq1, sq00, sq01, sq10, sq11;
    sq0 = _umul128(sw[0], pw18, &sq1);
    _addcarry_u64(_addcarry_u64(0,
       sq0, sw[1], &sq0),
       sq1, 0,   &sq1);
    sq00 = _umul128(sq0, pw18, &sq01);
    sq10 = _umul128(sq1, pw18, &sq11);
    _addcarry_u64(_addcarry_u64(_addcarry_u64(0,
       sq00, sw[2], &sw0),
       sq01, sq10,  &sw1),
       sq11, 0,     &sw2);
    #else
    unsigned __int128 sq  = (unsigned __int128)sw[0] * pw18 + sw[1];
    unsigned __int128 sq0 = (unsigned __int128)(uint64_t)(sq>> 0) * pw18 + sw[2];
    unsigned __int128 sq1 = (unsigned __int128)(uint64_t)(sq>>64) * pw18 + (uint64_t)(sq0>>64);
    sw0 = (uint64_t)(sq0);
    sw1 = (uint64_t)(sq1 >>  0);
    sw2 = (uint64_t)(sq1 >> 64);
    #endif

    // extract integer part of x[] (3 words)
    unsigned wi = nBe/64;
    unsigned bi = nBe%64;
    uint64_t x0 = x[wi+0];
    uint64_t xw0 = x0;
    uint64_t xw1 = x[wi+1];
    uint64_t xw2 = x[wi+2];
    x[wi+0] = 0;
    x[wi+1] = 0;
    x[wi+2] = 0;
    if (bi != 0) {
      uint64_t xw3 = x[wi+3];
      xw0 = (xw0 >> bi) | (xw1 << (64-bi));
      xw1 = (xw1 >> bi) | (xw2 << (64-bi));
      xw2 = (xw2 >> bi) | (xw3 << (64-bi));
      x[wi+0] = x0 & (((uint64_t)-1) >> (64-bi));
      x[wi+3] = 0;
    }

    if (sw2 != xw2) return sw2 < xw2 ? -1 : 1;
    if (sw1 != xw1) return sw1 < xw1 ? -1 : 1;
    if (sw0 != xw0) return sw0 < xw0 ? -1 : 1;
  }

  // continue comparison by groups of up to 19 digits
  while (nCmpDigits > 0) {
    // process last groups of digits (up to 19 digits at time)
    int nDig = nCmpDigits > 19 ? 19 : nCmpDigits;
    int contLen = (int)(nonDigit-str);
    if (contLen < nDig) {
      // special cases
      if (contLen == 0) { // str==nonDigit
        // nonDigit point to dot rather than to end_str
        ++str; // skip character
        nonDigit = end_str;
      } else {
        nDig = contLen;
      }
    }
    mp_mulw(x, x, tab1[nDig], (nBe-1)/64+1, 0); // x *= 5**nDig
    nBe -= nDig;
    nCmpDigits -= nDig;

    // read nDig digits from source and convert to binary
    uint64_t sw = 0;
    do {
      char c = *str++;
      sw = sw*10 + (c - '0');
    } while (--nDig);

    // extract integer part of threshold (1 64-bit word)
    unsigned wi = nBe/64;
    unsigned bi = nBe%64;
    uint64_t x0 = x[wi+0];
    uint64_t xw = x0;
    x[wi+0] = 0;
    if (bi != 0) {
      uint64_t x1 = x[wi+1];
      xw = (x0 >> bi) | (x1 << (64-bi));
      x[wi+0] = x0 & (((uint64_t)-1) >> (64-bi));
      x[wi+1] = 0;
    }

    if (sw != xw)
      return sw < xw ? -1 : 1;
  }

  if (str != end_str)
    return 1; // source is bigger, because there are no more bits in Thr, but source still contains significant digits

  // no more significant digits in source
  if (nBe > 0) { // look for non-zero words in x[]
    int nw = (nBe - 1)/64 + 1; // number of words in x[]
    for (int i = 0; i < nw; ++i) {
      if (x[i] != 0)
        return -1; // source is smaller than threshold
    }
  }

  return 0;
}
