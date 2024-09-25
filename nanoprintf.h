/* nanoprintf v0.5.1: a tiny embeddable printf replacement written in C.
   https://github.com/charlesnicholson/nanoprintf
   charles.nicholson+nanoprintf@gmail.com
   dual-licensed under 0bsd and unlicense, take your pick. see eof for details. */

#ifndef NANOPRINTF_H_INCLUDED
#define NANOPRINTF_H_INCLUDED

#include <stdarg.h>
#include <stddef.h>

#ifdef NANOPRINTF_USE_WCHAR
#  include <wchar.h>
#  define NPF_CHAR_TYPE wchar_t
#else
#  define NPF_CHAR_TYPE char
#endif

// Define this to fully sandbox nanoprintf inside of a translation unit.
#ifdef NANOPRINTF_VISIBILITY_STATIC
  #define NPF_VISIBILITY static
#else
  #define NPF_VISIBILITY extern
#endif

#if 0
#if !defined(NANOPRINTF_USE_WCHAR) && (defined(__clang__) || defined(__GNUC__) || defined(__GNUG__))
  #define NPF_PRINTF_ATTR(FORMAT_INDEX, VARGS_INDEX) \
    __attribute__((format(printf, FORMAT_INDEX, VARGS_INDEX)))
#else
  #define NPF_PRINTF_ATTR(FORMAT_INDEX, VARGS_INDEX)
#endif
#else
  #define NPF_PRINTF_ATTR(FORMAT_INDEX, VARGS_INDEX)
#endif

// Public API

#ifdef __cplusplus
extern "C" {
#endif

NPF_VISIBILITY int npf_verify_format(NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format);

// The npf_ functions all return the number of bytes required to express the
// fully-formatted string, not including the null terminator character.
// The npf_ functions do not return negative values, since the lack of 'l' length
// modifier support makes encoding errors impossible.

NPF_VISIBILITY int npf_snprintf(
  NPF_CHAR_TYPE *buffer, size_t bufsz, NPF_CHAR_TYPE const *reference, const NPF_CHAR_TYPE *format, ...) NPF_PRINTF_ATTR(3, 4);

NPF_VISIBILITY int npf_vsnprintf(
  NPF_CHAR_TYPE *buffer, size_t bufsz, NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format, va_list vlist) NPF_PRINTF_ATTR(3, 0);

typedef void (*npf_putc)(int c, void *ctx);
NPF_VISIBILITY int npf_pprintf(
  npf_putc pc, void *pc_ctx, NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format, ...) NPF_PRINTF_ATTR(3, 4);

NPF_VISIBILITY int npf_vpprintf(
  npf_putc pc, void *pc_ctx, NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format, va_list vlist) NPF_PRINTF_ATTR(3, 0);

#ifdef __cplusplus
}
#endif

#endif // NANOPRINTF_H_INCLUDED

/* The implementation of nanoprintf begins here, to be compiled only if
   NANOPRINTF_IMPLEMENTATION is defined. In a multi-file library what follows would
   be nanoprintf.c. */

#ifdef NANOPRINTF_IMPLEMENTATION

#ifndef NANOPRINTF_IMPLEMENTATION_INCLUDED
#define NANOPRINTF_IMPLEMENTATION_INCLUDED

#include <ovutf.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>

// The conversion buffer must fit at least UINT64_MAX in octal format with the leading '0'.
#ifndef NANOPRINTF_CONVERSION_BUFFER_SIZE
  #define NANOPRINTF_CONVERSION_BUFFER_SIZE    23
#endif
#if NANOPRINTF_CONVERSION_BUFFER_SIZE < 23
  #error The size of the conversion buffer must be at least 23 bytes.
#endif

// Pick reasonable defaults if nothing's been configured.
#if !defined(NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS) && \
    !defined(NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS) && \
    !defined(NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS) && \
    !defined(NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS) && \
    !defined(NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS) && \
    !defined(NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS) && \
    !defined(NANOPRINTF_USE_FMT_SPEC_OPT_STAR) && \
    !defined(NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS)
  #define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
  #define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
  #define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 1
  #define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 0
  #define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
  #define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
  #define NANOPRINTF_USE_FMT_SPEC_OPT_STAR 1
  #define NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS 0
#endif

// If anything's been configured, everything must be configured.
#ifndef NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS
  #error NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS
  #error NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS
  #error NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS
  #error NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS
  #error NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS
  #error NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_FMT_SPEC_OPT_STAR
  #error NANOPRINTF_USE_FMT_SPEC_OPT_STAR must be #defined to 0 or 1
#endif
#ifndef NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS
  #error NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS must be #defined to 0 or 1
#endif

// Ensure flags are compatible.
#if (NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1) && \
    (NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 0)
  #error Precision format specifiers must be enabled if float support is enabled.
#endif

// intmax_t / uintmax_t require stdint from c99 / c++11
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
  #ifndef _MSC_VER
    #ifdef __cplusplus
      #if __cplusplus < 201103L
        #error large format specifier support requires C++11 or later.
      #endif
    #else
      #if __STDC_VERSION__ < 199409L
        #error nanoprintf requires C99 or later.
      #endif
    #endif
  #endif
#endif

// Figure out if we can disable warnings with pragmas.
#ifdef __clang__
  #define NANOPRINTF_CLANG 1
  #define NANOPRINTF_GCC_PAST_4_6 0
#else
  #define NANOPRINTF_CLANG 0
  #if defined(__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 6)))
    #define NANOPRINTF_GCC_PAST_4_6 1
  #else
    #define NANOPRINTF_GCC_PAST_4_6 0
  #endif
#endif

#if NANOPRINTF_CLANG || NANOPRINTF_GCC_PAST_4_6
  #define NANOPRINTF_HAVE_GCC_WARNING_PRAGMAS 1
#else
  #define NANOPRINTF_HAVE_GCC_WARNING_PRAGMAS 0
#endif

#if NANOPRINTF_HAVE_GCC_WARNING_PRAGMAS
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wunused-function"
  #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
  #ifdef __cplusplus
    #pragma GCC diagnostic ignored "-Wold-style-cast"
  #endif
  #pragma GCC diagnostic ignored "-Wpadded"
  #pragma GCC diagnostic ignored "-Wfloat-equal"
  #if NANOPRINTF_CLANG
    #pragma GCC diagnostic ignored "-Wc++98-compat-pedantic"
    #pragma GCC diagnostic ignored "-Wcovered-switch-default"
    #pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
    #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
    #ifndef __APPLE__
      #pragma GCC diagnostic ignored "-Wunsafe-buffer-usage"
    #endif
  #elif NANOPRINTF_GCC_PAST_4_6
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
  #endif
#endif

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable:4619) // there is no warning number 'number'
  // C4619 has to be disabled first!
  #pragma warning(disable:4127) // conditional expression is constant
  #pragma warning(disable:4505) // unreferenced local function has been removed
  #pragma warning(disable:4514) // unreferenced inline function has been removed
  #pragma warning(disable:4701) // potentially uninitialized local variable used
  #pragma warning(disable:4706) // assignment within conditional expression
  #pragma warning(disable:4710) // function not inlined
  #pragma warning(disable:4711) // function selected for inline expansion
  #pragma warning(disable:4820) // padding added after struct member
  #pragma warning(disable:5039) // potentially throwing function passed to extern C function
  #pragma warning(disable:5045) // compiler will insert Spectre mitigation for memory load
  #pragma warning(disable:5262) // implicit switch fall-through
  #pragma warning(disable:26812) // enum type is unscoped
#endif

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
  #define NPF_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
  #define NPF_NOINLINE __declspec(noinline)
#else
  #define NPF_NOINLINE
#endif

#if (NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1) || \
    (NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1)
enum {
  NPF_FMT_SPEC_OPT_NONE,
  NPF_FMT_SPEC_OPT_LITERAL,
#if NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1
  NPF_FMT_SPEC_OPT_STAR,
#endif
};
#endif

enum {
  NPF_FMT_SPEC_LEN_MOD_NONE,
  NPF_FMT_SPEC_LEN_MOD_SHORT,       // 'h'
  NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE, // 'L'
  NPF_FMT_SPEC_LEN_MOD_CHAR,        // 'hh'
  NPF_FMT_SPEC_LEN_MOD_LONG,        // 'l'
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
  NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG, // 'll'
  NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX,    // 'j'
  NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET,     // 'z'
  NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT,  // 't'
#endif
};

enum {
  NPF_FMT_SPEC_CONV_NONE,
  NPF_FMT_SPEC_CONV_PERCENT,      // '%'
  NPF_FMT_SPEC_CONV_CHAR,         // 'c'
  NPF_FMT_SPEC_CONV_STRING,       // 's'
  NPF_FMT_SPEC_CONV_SIGNED_INT,   // 'i', 'd'
#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
  NPF_FMT_SPEC_CONV_BINARY,       // 'b'
#endif
  NPF_FMT_SPEC_CONV_OCTAL,        // 'o'
  NPF_FMT_SPEC_CONV_HEX_INT,      // 'x', 'X'
  NPF_FMT_SPEC_CONV_UNSIGNED_INT, // 'u'
  NPF_FMT_SPEC_CONV_POINTER,      // 'p'
#if NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS == 1
  NPF_FMT_SPEC_CONV_WRITEBACK,    // 'n'
#endif
#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
  NPF_FMT_SPEC_CONV_FLOAT_DEC,      // 'f', 'F'
  NPF_FMT_SPEC_CONV_FLOAT_SCI,      // 'e', 'E'
  NPF_FMT_SPEC_CONV_FLOAT_SHORTEST, // 'g', 'G'
  NPF_FMT_SPEC_CONV_FLOAT_HEX,      // 'a', 'A'
#endif
};

typedef struct npf_format_spec {
  int order;

#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
  int field_width;
  uint8_t field_width_opt;
  NPF_CHAR_TYPE left_justified;   // '-'
  NPF_CHAR_TYPE leading_zero_pad; // '0'
#endif
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
  int prec;
  uint8_t prec_opt;
#endif
  NPF_CHAR_TYPE prepend;          // ' ' or '+'
  NPF_CHAR_TYPE alt_form;         // '#'
  char case_adjust;      // 'a' - 'A'
  uint8_t length_modifier;
  uint8_t conv_spec;
} npf_format_spec_t;

#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 0
  typedef long npf_int_t;
  typedef unsigned long npf_uint_t;
#else
  typedef intmax_t npf_int_t;
  typedef uintmax_t npf_uint_t;
#endif

typedef struct npf_bufputc_ctx {
  NPF_CHAR_TYPE *dst;
  size_t len;
  size_t cur;
} npf_bufputc_ctx_t;

#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
  #ifdef _MSC_VER
    #include <BaseTsd.h>
    typedef SSIZE_T ssize_t;
  #else
    #include <sys/types.h>
  #endif
#endif

#ifdef _MSC_VER
  #include <intrin.h>
#endif

static int npf_max(int x, int y) { return (x > y) ? x : y; }

static int npf_parse_format_spec(NPF_CHAR_TYPE const *format, npf_format_spec_t *out_spec) {
  NPF_CHAR_TYPE const *cur = format;
  out_spec->order = 0;

#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
  out_spec->left_justified = 0;
  out_spec->leading_zero_pad = 0;
#endif
  out_spec->case_adjust = 'a' - 'A'; // lowercase
  out_spec->prepend = 0;
  out_spec->alt_form = 0;

  switch (*++cur) { // cur points at the leading '%' character
#if NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS == 1
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    while ((*cur >= '0') && (*cur <= '9')) {
      out_spec->order = (out_spec->order * 10) + (*cur++ - '0');
    }
    if (*cur == '$') {
      if (out_spec->order == 0) {
        return 0;
      }
    } else {
      cur = format;
      out_spec->order = 0;
    }
    break;
#endif
  default:
    --cur;
    break;
  }

  while (*++cur) {
    switch (*cur) { // Optional flags
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
      case '-': out_spec->left_justified = '-'; out_spec->leading_zero_pad = 0; continue;
      case '0': out_spec->leading_zero_pad = !out_spec->left_justified; continue;
#endif
      case '+': out_spec->prepend = '+'; continue;
      case ' ': if (out_spec->prepend == 0) { out_spec->prepend = ' '; } continue;
      case '#': out_spec->alt_form = '#'; continue;
      default: break;
    }
    break;
  }

#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
  out_spec->field_width = 0;
  out_spec->field_width_opt = NPF_FMT_SPEC_OPT_NONE;
#if NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1
  if (*cur == '*') {
    out_spec->field_width_opt = NPF_FMT_SPEC_OPT_STAR;
    ++cur;
#if NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS == 1
    switch (*cur) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      while ((*cur >= '0') && (*cur <= '9')) {
        out_spec->field_width = (out_spec->field_width * 10) + (*cur++ - '0');
      }
      if (*cur != '$' || out_spec->field_width == 0) {
        return 0;
      }
    }
#endif
  } else
#endif
  {
    out_spec->field_width = 0;
    while ((*cur >= '0') && (*cur <= '9')) {
      out_spec->field_width_opt = NPF_FMT_SPEC_OPT_LITERAL;
      out_spec->field_width = (out_spec->field_width * 10) + (*cur++ - '0');
    }
  }
#endif

#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
  out_spec->prec = 0;
  out_spec->prec_opt = NPF_FMT_SPEC_OPT_NONE;
  if (*cur == '.') {
    ++cur;
#if NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1
    if (*cur == '*') {
      out_spec->prec_opt = NPF_FMT_SPEC_OPT_STAR;
      ++cur;
#if NANOPRINTF_USE_ORDER_FORMAT_EXTENSION_SPECIFIERS == 1
      switch (*cur) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        while ((*cur >= '0') && (*cur <= '9')) {
          out_spec->prec = (out_spec->prec * 10) + (*cur++ - '0');
        }
        if (*cur != '$' || out_spec->prec == 0) {
          return 0;
        }
      }
#endif
    } else
#endif
    {
      if (*cur == '-') {
        ++cur;
      } else {
        out_spec->prec_opt = NPF_FMT_SPEC_OPT_LITERAL;
      }
      while ((*cur >= '0') && (*cur <= '9')) {
        out_spec->prec = (out_spec->prec * 10) + (*cur++ - '0');
      }
    }
  }
#endif

  uint_fast8_t tmp_conv = NPF_FMT_SPEC_CONV_NONE;
  out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_NONE;
  switch (*cur++) { // Length modifier
    case 'h':
      out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_SHORT;
      if (*cur == 'h') {
        out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_CHAR;
        ++cur;
      }
      break;
    case 'l':
      out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_LONG;
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
      if (*cur == 'l') {
        out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG;
        ++cur;
      }
#endif
      break;
#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
    case 'L': out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE; break;
#endif
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
    case 'j': out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX; break;
    case 'z': out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET; break;
    case 't': out_spec->length_modifier = NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT; break;
#endif
    default: --cur; break;
  }

  switch (*cur++) { // Conversion specifier
    case '%': out_spec->conv_spec = NPF_FMT_SPEC_CONV_PERCENT;
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
      out_spec->prec_opt = NPF_FMT_SPEC_OPT_NONE;
#endif
      break;

    case 'c': out_spec->conv_spec = NPF_FMT_SPEC_CONV_CHAR;
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
      out_spec->prec_opt = NPF_FMT_SPEC_OPT_NONE;
#endif
      break;

    case 's': out_spec->conv_spec = NPF_FMT_SPEC_CONV_STRING;
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
      out_spec->leading_zero_pad = 0;
#endif
      break;

    case 'i':
    case 'd': tmp_conv = NPF_FMT_SPEC_CONV_SIGNED_INT;
    case 'o':
      if (tmp_conv == NPF_FMT_SPEC_CONV_NONE) { tmp_conv = NPF_FMT_SPEC_CONV_OCTAL; }
    case 'u':
      if (tmp_conv == NPF_FMT_SPEC_CONV_NONE) { tmp_conv = NPF_FMT_SPEC_CONV_UNSIGNED_INT; }
    case 'X':
      if (tmp_conv == NPF_FMT_SPEC_CONV_NONE) { out_spec->case_adjust = 0; }
    case 'x':
      if (tmp_conv == NPF_FMT_SPEC_CONV_NONE) { tmp_conv = NPF_FMT_SPEC_CONV_HEX_INT; }
      out_spec->conv_spec = (uint8_t)tmp_conv;
#if (NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1) && \
    (NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1)
      if (out_spec->prec_opt != NPF_FMT_SPEC_OPT_NONE) { out_spec->leading_zero_pad = 0; }
#endif
      break;

#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
    case 'F': out_spec->case_adjust = 0;
    case 'f':
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_FLOAT_DEC;
      if (out_spec->prec_opt == NPF_FMT_SPEC_OPT_NONE) { out_spec->prec = 6; }
      break;

    case 'E': out_spec->case_adjust = 0;
    case 'e':
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_FLOAT_SCI;
      if (out_spec->prec_opt == NPF_FMT_SPEC_OPT_NONE) { out_spec->prec = 6; }
      break;

    case 'G': out_spec->case_adjust = 0;
    case 'g':
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_FLOAT_SHORTEST;
      if (out_spec->prec_opt == NPF_FMT_SPEC_OPT_NONE) { out_spec->prec = 6; }
      break;

    case 'A': out_spec->case_adjust = 0;
    case 'a':
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_FLOAT_HEX;
      if (out_spec->prec_opt == NPF_FMT_SPEC_OPT_NONE) { out_spec->prec = 6; }
      break;
#endif

#if NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS == 1
    case 'n':
      // todo: reject string if flags or width or precision exist
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_WRITEBACK;
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
      out_spec->prec_opt = NPF_FMT_SPEC_OPT_NONE;
#endif
      break;
#endif

    case 'p':
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_POINTER;
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
      out_spec->prec_opt = NPF_FMT_SPEC_OPT_NONE;
#endif
      break;

#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
    case 'B':
      out_spec->case_adjust = 0;
    case 'b':
      out_spec->conv_spec = NPF_FMT_SPEC_CONV_BINARY;
      break;
#endif

    default: return 0;
  }

  return (int)(cur - format);
}

static NPF_NOINLINE int npf_utoa_rev(
    npf_uint_t val, NPF_CHAR_TYPE *buf, uint_fast8_t base, char case_adj) {
  uint_fast8_t n = 0;
  do {
    int_fast8_t const d = (int_fast8_t)(val % base);
    *buf++ = (NPF_CHAR_TYPE)(((d < 10) ? '0' : ('A' - 10 + case_adj)) + d);
    ++n;
    val /= base;
  } while (val);
  return (int)n;
}

#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1

#include <float.h>

#if (DBL_MANT_DIG <= 11) && (DBL_MAX_EXP <= 16)
  typedef uint_fast16_t npf_double_bin_t;
  typedef int_fast8_t npf_ftoa_exp_t;
#elif (DBL_MANT_DIG <= 24) && (DBL_MAX_EXP <= 128)
  typedef uint_fast32_t npf_double_bin_t;
  typedef int_fast8_t npf_ftoa_exp_t;
#elif (DBL_MANT_DIG <= 53) && (DBL_MAX_EXP <= 1024)
  typedef uint_fast64_t npf_double_bin_t;
  typedef int_fast16_t npf_ftoa_exp_t;
#else
  #error Unsupported width of the double type.
#endif

// The floating point conversion code works with an unsigned integer type of any size.
#ifndef NANOPRINTF_CONVERSION_FLOAT_TYPE
  #define NANOPRINTF_CONVERSION_FLOAT_TYPE unsigned int
#endif
typedef NANOPRINTF_CONVERSION_FLOAT_TYPE npf_ftoa_man_t;

#if (NANOPRINTF_CONVERSION_BUFFER_SIZE <= UINT_FAST8_MAX) && (UINT_FAST8_MAX <= INT_MAX)
  typedef uint_fast8_t npf_ftoa_dec_t;
#else
  typedef int npf_ftoa_dec_t;
#endif

enum {
  NPF_DOUBLE_EXP_MASK = DBL_MAX_EXP * 2 - 1,
  NPF_DOUBLE_EXP_BIAS = DBL_MAX_EXP - 1,
  NPF_DOUBLE_MAN_BITS = DBL_MANT_DIG - 1,
  NPF_DOUBLE_BIN_BITS = sizeof(npf_double_bin_t) * CHAR_BIT,
  NPF_FTOA_MAN_BITS   = sizeof(npf_ftoa_man_t) * CHAR_BIT,
  NPF_FTOA_SHIFT_BITS =
    ((NPF_FTOA_MAN_BITS < DBL_MANT_DIG) ? NPF_FTOA_MAN_BITS : DBL_MANT_DIG) - 1
};

/* Generally, floating-point conversion implementations use
   grisu2 (https://bit.ly/2JgMggX) and ryu (https://bit.ly/2RLXSg0) algorithms,
   which are mathematically exact and fast, but require large lookup tables.

   This implementation was inspired by Wojciech Muła's (zdjęcia@garnek.pl)
   algorithm (http://0x80.pl/notesen/2015-12-29-float-to-string.html) and
   extended further by adding dynamic scaling and configurable integer width by
   Oskars Rubenis (https://github.com/Okarss). */

static int npf_ftoa_rev(NPF_CHAR_TYPE *buf, npf_format_spec_t const *spec, double f) {
  NPF_CHAR_TYPE const *ret = NULL;
  npf_double_bin_t bin; { // Union-cast is UB pre-C11, compiler optimizes byte-copy loop.
    NPF_CHAR_TYPE const *src = (NPF_CHAR_TYPE const *)&f;
    NPF_CHAR_TYPE *dst = (NPF_CHAR_TYPE *)&bin;
    for (uint_fast8_t i = 0; i < sizeof(f); ++i) { dst[i] = src[i]; }
  }

  // Unsigned -> signed int casting is IB and can raise a signal but generally doesn't.
  npf_ftoa_exp_t exp =
    (npf_ftoa_exp_t)((npf_ftoa_exp_t)(bin >> NPF_DOUBLE_MAN_BITS) & NPF_DOUBLE_EXP_MASK);

  bin &= ((npf_double_bin_t)0x1 << NPF_DOUBLE_MAN_BITS) - 1;
  if (exp == (npf_ftoa_exp_t)NPF_DOUBLE_EXP_MASK) { // special value
    ret = (bin) ? "NAN" : "FNI";
    goto exit;
  }
  if (spec->prec > (NANOPRINTF_CONVERSION_BUFFER_SIZE - 2)) { goto exit; }
  if (exp) { // normal number
    bin |= (npf_double_bin_t)0x1 << NPF_DOUBLE_MAN_BITS;
  } else { // subnormal number
    ++exp;
  }
  exp = (npf_ftoa_exp_t)(exp - NPF_DOUBLE_EXP_BIAS);

  uint_fast8_t carry; carry = 0;
  npf_ftoa_dec_t end, dec; dec = (npf_ftoa_dec_t)spec->prec;
  if (dec || spec->alt_form) {
    buf[dec++] = '.';
  }

  { // Integer part
    npf_ftoa_man_t man_i;

    if (exp >= 0) {
      int_fast8_t shift_i =
        (int_fast8_t)((exp > NPF_FTOA_SHIFT_BITS) ? (int)NPF_FTOA_SHIFT_BITS : exp);
      npf_ftoa_exp_t exp_i = (npf_ftoa_exp_t)(exp - shift_i);
      shift_i = (int_fast8_t)(NPF_DOUBLE_MAN_BITS - shift_i);
      man_i = (npf_ftoa_man_t)(bin >> shift_i);

      if (exp_i) {
        if (shift_i) {
          carry = (bin >> (shift_i - 1)) & 0x1;
        }
        exp = NPF_DOUBLE_MAN_BITS; // invalidate the fraction part
      }

      // Scale the exponent from base-2 to base-10.
      for (; exp_i; --exp_i) {
        if (!(man_i & ((npf_ftoa_man_t)0x1 << (NPF_FTOA_MAN_BITS - 1)))) {
          man_i = (npf_ftoa_man_t)(man_i << 1);
          man_i = (npf_ftoa_man_t)(man_i | carry); carry = 0;
        } else {
          if (dec >= NANOPRINTF_CONVERSION_BUFFER_SIZE) { goto exit; }
          buf[dec++] = '0';
          carry = (((uint_fast8_t)(man_i % 5) + carry) > 2);
          man_i /= 5;
        }
      }
    } else {
      man_i = 0;
    }
    end = dec;

    do { // Print the integer
      if (end >= NANOPRINTF_CONVERSION_BUFFER_SIZE) { goto exit; }
      buf[end++] = (char)('0' + (char)(man_i % 10));
      man_i /= 10;
    } while (man_i);
  }

  { // Fraction part
    npf_ftoa_man_t man_f;
    npf_ftoa_dec_t dec_f = (npf_ftoa_dec_t)spec->prec;

    if (exp < NPF_DOUBLE_MAN_BITS) {
      int_fast8_t shift_f = (int_fast8_t)((exp < 0) ? -1 : exp);
      npf_ftoa_exp_t exp_f = (npf_ftoa_exp_t)(exp - shift_f);
      npf_double_bin_t bin_f =
        bin << ((NPF_DOUBLE_BIN_BITS - NPF_DOUBLE_MAN_BITS) + shift_f);

      // This if-else statement can be completely optimized at compile time.
      if (NPF_DOUBLE_BIN_BITS > NPF_FTOA_MAN_BITS) {
        man_f = (npf_ftoa_man_t)(bin_f >> ((unsigned)(NPF_DOUBLE_BIN_BITS -
                                                      NPF_FTOA_MAN_BITS) %
                                           NPF_DOUBLE_BIN_BITS));
        carry = (uint_fast8_t)((bin_f >> ((unsigned)(NPF_DOUBLE_BIN_BITS -
                                                     NPF_FTOA_MAN_BITS - 1) %
                                          NPF_DOUBLE_BIN_BITS)) & 0x1);
      } else {
        man_f = (npf_ftoa_man_t)((npf_ftoa_man_t)bin_f
                                 << ((unsigned)(NPF_FTOA_MAN_BITS -
                                                NPF_DOUBLE_BIN_BITS) % NPF_FTOA_MAN_BITS));
        carry = 0;
      }

      // Scale the exponent from base-2 to base-10 and prepare the first digit.
      for (uint_fast8_t digit = 0; dec_f && (exp_f < 4); ++exp_f) {
        if ((man_f > ((npf_ftoa_man_t)-4 / 5)) || digit) {
          carry = (uint_fast8_t)(man_f & 0x1);
          man_f = (npf_ftoa_man_t)(man_f >> 1);
        } else {
          man_f = (npf_ftoa_man_t)(man_f * 5);
          if (carry) { man_f = (npf_ftoa_man_t)(man_f + 3); carry = 0; }
          if (exp_f < 0) {
            buf[--dec_f] = '0';
          } else {
            ++digit;
          }
        }
      }
      man_f = (npf_ftoa_man_t)(man_f + carry);
      carry = (exp_f >= 0);
      dec = 0;
    } else {
      man_f = 0;
    }

    if (dec_f) {
      // Print the fraction
      for (;;) {
        buf[--dec_f] = (NPF_CHAR_TYPE)('0' + (char)(man_f >> (NPF_FTOA_MAN_BITS - 4)));
        man_f = (npf_ftoa_man_t)(man_f & ~((npf_ftoa_man_t)0xF << (NPF_FTOA_MAN_BITS - 4)));
        if (!dec_f) { break; }
        man_f = (npf_ftoa_man_t)(man_f * 10);
      }
      man_f = (npf_ftoa_man_t)(man_f << 4);
    }
    if (exp < NPF_DOUBLE_MAN_BITS) {
      carry &= (uint_fast8_t)(man_f >> (NPF_FTOA_MAN_BITS - 1));
    }
  }

  // Round the number
  for (; carry; ++dec) {
    if (dec >= NANOPRINTF_CONVERSION_BUFFER_SIZE) { goto exit; }
    if (dec >= end) { buf[end++] = '0'; }
    if (buf[dec] == '.') { continue; }
    carry = (buf[dec] == '9');
    buf[dec] = (NPF_CHAR_TYPE)(carry ? '0' : (buf[dec] + 1));
  }

  return (int)end;
exit:
  if (!ret) { ret = "RRE"; }
  uint_fast8_t i;
  for (i = 0; ret[i]; ++i) { buf[i] = (NPF_CHAR_TYPE)(ret[i] + spec->case_adjust); }
  return (int)i;
}

#endif // NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS

#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
static int npf_bin_len(npf_uint_t u) {
  // Return the length of the binary string format of 'u', preferring intrinsics.
  if (!u) { return 1; }

#ifdef _MSC_VER // Win64, use _BSR64 for everything. If x86, use _BSR when non-large.
  #ifdef _M_X64
    #define NPF_HAVE_BUILTIN_CLZ
    #define NPF_CLZ _BitScanReverse64
  #elif NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 0
    #define NPF_HAVE_BUILTIN_CLZ
    #define NPF_CLZ _BitScanReverse
  #endif
  #ifdef NPF_HAVE_BUILTIN_CLZ
    unsigned long idx;
    NPF_CLZ(&idx, u);
    return (int)(idx + 1);
  #endif
#elif defined(NANOPRINTF_CLANG) || defined(NANOPRINTF_GCC_PAST_4_6)
  #define NPF_HAVE_BUILTIN_CLZ
  #if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
    #define NPF_CLZ(X) ((sizeof(long long) * CHAR_BIT) - (size_t)__builtin_clzll(X))
  #else
    #define NPF_CLZ(X) ((sizeof(long) * CHAR_BIT) - (size_t)__builtin_clzl(X))
  #endif
  return (int)NPF_CLZ(u);
#endif

#ifndef NPF_HAVE_BUILTIN_CLZ
  int n;
  for (n = 0; u; ++n, u >>= 1); // slow but small software fallback
  return n;
#else
  #undef NPF_HAVE_BUILTIN_CLZ
  #undef NPF_CLZ
#endif
}
#endif

struct npf_arg_type {
  npf_format_spec_conversion_t conv_spec;
  npf_format_spec_length_modifier_t length_modifier;
};

union npf_arg_value {
  char *hstr;
  wchar_t *lstr;
  int i;
  long l;
  unsigned u;
  unsigned long ul;
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
  long long ll;
  intmax_t imx;
  ssize_t ssz;
  ptrdiff_t ptrdiff;
  unsigned long long ull;
  uintmax_t uimx;
  size_t sz;
#endif
  void *ptr;
#if NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS == 1
  int *ip;
  short *sp;
  double *dp;
  char *cp;
  long *lp;
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
  long long *llp;
  intmax_t *imxp;
  ssize_t *ssz;
  ptrdiff_t *ptrdiffp;
#endif
#endif
#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
  long double ld;
  double d;
#endif
};

static size_t npf_arg_sizeof(npf_format_spec_conversion_t const conv_spec,
                             npf_format_spec_length_modifier_t const length_modifier) {
  switch (conv_spec) {
  case NPF_FMT_SPEC_CONV_PERCENT:
    return 0;
  case NPF_FMT_SPEC_CONV_CHAR:
    return sizeof(int);
  case NPF_FMT_SPEC_CONV_STRING:
    if (length_modifier == NPF_FMT_SPEC_LEN_MOD_NONE || length_modifier == NPF_FMT_SPEC_LEN_MOD_SHORT) {
      return sizeof(char *);
    }
    if (length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG) {
      return sizeof(wchar_t *);
    }
    return 0;
  case NPF_FMT_SPEC_CONV_SIGNED_INT:
    switch (length_modifier) {
    case NPF_FMT_SPEC_LEN_MOD_NONE:
    case NPF_FMT_SPEC_LEN_MOD_SHORT:
    case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
    case NPF_FMT_SPEC_LEN_MOD_CHAR:
      return sizeof(int);
    case NPF_FMT_SPEC_LEN_MOD_LONG:
      return sizeof(long);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
    case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
      return sizeof(long long);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
      return sizeof(intmax_t);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
      return sizeof(ssize_t);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
      return sizeof(ptrdiff_t);
#endif
    default:
      return 0;
    }
#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
  case NPF_FMT_SPEC_CONV_BINARY:
#endif
  case NPF_FMT_SPEC_CONV_OCTAL:
  case NPF_FMT_SPEC_CONV_HEX_INT:
  case NPF_FMT_SPEC_CONV_UNSIGNED_INT:
    switch (length_modifier) {
    case NPF_FMT_SPEC_LEN_MOD_NONE:
    case NPF_FMT_SPEC_LEN_MOD_SHORT:
    case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
    case NPF_FMT_SPEC_LEN_MOD_CHAR:
      return sizeof(unsigned);
    case NPF_FMT_SPEC_LEN_MOD_LONG: // 'l'
      return sizeof(unsigned long);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
    case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
      return sizeof(unsigned long long);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
      return sizeof(uintmax_t);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
    case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
      return sizeof(size_t);
#endif
    default:
      return 0;
    }
  case NPF_FMT_SPEC_CONV_POINTER:
    return sizeof(void *);
#if NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS == 1
  case NPF_FMT_SPEC_CONV_WRITEBACK:
    switch (length_modifier) {
    case NPF_FMT_SPEC_LEN_MOD_NONE:
      return sizeof(int *);
    case NPF_FMT_SPEC_LEN_MOD_SHORT:
      return sizeof(short *);
    case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
      return sizeof(double *);
    case NPF_FMT_SPEC_LEN_MOD_CHAR:
      return sizeof(char *);
    case NPF_FMT_SPEC_LEN_MOD_LONG:
      return sizeof(long *);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
    case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
      return sizeof(long long *);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
      return sizeof(intmax_t *);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
      return sizeof(ssize_t *);
    case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
      return sizeof(ptrdiff_t *);
#endif
    default:
      return 0;
    }
#endif
#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
  case NPF_FMT_SPEC_CONV_FLOAT_DECIMAL:
    if (length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE) {
      return sizeof(long double);
    }
    return sizeof(double);
#endif
  }
  return 0;
}

static int npf_is_int(struct npf_arg_type *a) {
  if (a->conv_spec != NPF_FMT_SPEC_CONV_SIGNED_INT) {
    return 0;
  }
  switch (a->length_modifier) {
  case NPF_FMT_SPEC_LEN_MOD_NONE:
  case NPF_FMT_SPEC_LEN_MOD_SHORT:
  case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
  case NPF_FMT_SPEC_LEN_MOD_CHAR:
    return 1;
  case NPF_FMT_SPEC_LEN_MOD_LONG:
    return sizeof(int) <= sizeof(long);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
  case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
    return sizeof(int) <= sizeof(long long);
  case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
    return sizeof(int) <= sizeof(intmax_t);
  case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
    return sizeof(int) <= sizeof(ssize_t);
  case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
    return sizeof(int) <= sizeof(ptrdiff_t);
#endif
  default:
    return 0;
  }
}

static int npf_format_to_npf_arg_type(NPF_CHAR_TYPE const *const format,
                                      int const nargs,
                                      struct npf_arg_type *const types,
                                      int const accept_new_param) {
  npf_format_spec_t fs;
  int n = 0, used_max = 0;
  NPF_CHAR_TYPE const *cur = format;
  while (*cur) {
    int const fs_len = (*cur != '%') ? 0 : npf_parse_format_spec(cur, &fs);
    if (!fs_len) {
      cur++;
      continue;
    }
    cur += fs_len;
    if (fs.conv_spec == NPF_FMT_SPEC_CONV_PERCENT) {
      continue;
    }
#if (NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1) && (NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1)
    if (fs.field_width_opt == NPF_FMT_SPEC_OPT_STAR) {
      if (fs.field_width == 0) {
        fs.field_width = ++n;
      }
      if (fs.field_width > nargs) {
        return -1;
      }
      used_max = npf_max(used_max, fs.field_width);
      struct npf_arg_type *const a = &types[fs.field_width - 1];
      if (accept_new_param && a->conv_spec == NPF_FMT_SPEC_CONV_PERCENT) {
        a->conv_spec = NPF_FMT_SPEC_CONV_SIGNED_INT;
        a->length_modifier = NPF_FMT_SPEC_LEN_MOD_NONE;
      }
      if (!npf_is_int(a)) {
        return -1;
      }
    }
#endif
#if (NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1) && (NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1)
    if (fs.prec_opt == NPF_FMT_SPEC_OPT_STAR) {
      if (fs.prec == 0) {
        fs.prec = ++n;
      }
      if (fs.prec > nargs) {
        return -1;
      }
      used_max = npf_max(used_max, fs.prec);
      struct npf_arg_type *const a = &types[fs.prec - 1];
      if (accept_new_param && a->conv_spec == NPF_FMT_SPEC_CONV_PERCENT) {
        a->conv_spec = NPF_FMT_SPEC_CONV_SIGNED_INT;
        a->length_modifier = NPF_FMT_SPEC_LEN_MOD_NONE;
      }
      if (!npf_is_int(a)) {
        return -1;
      }
    }
#endif
    if (fs.order == 0) {
      fs.order = ++n;
    }
    if (fs.order > nargs) {
      return -1;
    }
    used_max = npf_max(used_max, fs.order);
    struct npf_arg_type *const a = &types[fs.order - 1];
    if (accept_new_param && a->conv_spec == NPF_FMT_SPEC_CONV_PERCENT) {
      a->conv_spec = fs.conv_spec;
      a->length_modifier = fs.length_modifier;
    }
    if (a->conv_spec != fs.conv_spec) {
      return -1;
    } else if (a->length_modifier != fs.length_modifier &&
               npf_arg_sizeof(fs.conv_spec, fs.length_modifier) != npf_arg_sizeof(a->conv_spec, a->length_modifier)) {
      return -1;
    }
  }
  return used_max;
}

static int npf_verify_and_assign_values(int const args_max,
                                        struct npf_arg_type const *const types,
                                        union npf_arg_value *const values,
                                        va_list args) {
  for (int i = 1; i <= args_max; ++i) {
    int const idx = i - 1;
    switch (types[idx].conv_spec) {
    case NPF_FMT_SPEC_CONV_PERCENT:
      return 0;
    case NPF_FMT_SPEC_CONV_CHAR:
      values[idx].i = va_arg(args, int);
      continue;
    case NPF_FMT_SPEC_CONV_STRING:
      if (types[idx].length_modifier == NPF_FMT_SPEC_LEN_MOD_NONE ||
          types[idx].length_modifier == NPF_FMT_SPEC_LEN_MOD_SHORT) {
        values[idx].hstr = va_arg(args, char *);
      } else if (types[idx].length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG) {
        values[idx].lstr = va_arg(args, wchar_t *);
      } else {
        return 0;
      }
      continue;
    case NPF_FMT_SPEC_CONV_SIGNED_INT:
      switch (types[idx].length_modifier) {
      case NPF_FMT_SPEC_LEN_MOD_NONE:
      case NPF_FMT_SPEC_LEN_MOD_SHORT:
      case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
      case NPF_FMT_SPEC_LEN_MOD_CHAR:
        values[idx].i = va_arg(args, int);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LONG:
        values[idx].l = va_arg(args, long);
        continue;
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
      case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
        values[idx].ll = va_arg(args, long long);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
        values[idx].imx = va_arg(args, intmax_t);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
        values[idx].ssz = va_arg(args, ssize_t);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
        values[idx].ptrdiff = va_arg(args, ptrdiff_t);
        continue;
#endif
      default:
        return 0;
      }
#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
    case NPF_FMT_SPEC_CONV_BINARY:
#endif
    case NPF_FMT_SPEC_CONV_OCTAL:
    case NPF_FMT_SPEC_CONV_HEX_INT:
    case NPF_FMT_SPEC_CONV_UNSIGNED_INT:
      switch (types[idx].length_modifier) {
      case NPF_FMT_SPEC_LEN_MOD_NONE:
      case NPF_FMT_SPEC_LEN_MOD_SHORT:
      case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
      case NPF_FMT_SPEC_LEN_MOD_CHAR:
        values[idx].u = va_arg(args, unsigned);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LONG: // 'l'
        values[idx].ul = va_arg(args, unsigned long);
        continue;
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
      case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
        values[idx].ull = va_arg(args, unsigned long long);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
        values[idx].uimx = va_arg(args, uintmax_t);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
      case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
        values[idx].sz = va_arg(args, size_t);
        continue;
#endif
      default:
        return 0;
      }
    case NPF_FMT_SPEC_CONV_POINTER:
      values[idx].ptr = va_arg(args, void *);
      continue;
#if NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS == 1
    case NPF_FMT_SPEC_CONV_WRITEBACK:
      switch (types[idx].length_modifier) {
      case NPF_FMT_SPEC_LEN_MOD_NONE:
        values[idx].ip = va_arg(args, int *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_SHORT:
        values[idx].sp = va_arg(args, short *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE:
        values[idx].dp = va_arg(args, double *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_CHAR:
        values[idx].cp = va_arg(args, char *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LONG:
        values[idx].lp = va_arg(args, long *);
        continue;
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
      case NPF_FMT_SPEC_LEN_MOD_LARGE_LONG_LONG:
        values[idx].llp = va_arg(args, long long *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_INTMAX:
        values[idx].imxp = va_arg(args, intmax_t *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_SIZET:
        values[idx].sszp = va_arg(args, ssize_t *);
        continue;
      case NPF_FMT_SPEC_LEN_MOD_LARGE_PTRDIFFT:
        values[idx].ptrdiffp = va_arg(args, ptrdiff_t *);
        continue;
#endif
      default:
        return 0;
      }
#endif
#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
    case NPF_FMT_SPEC_CONV_FLOAT_DECIMAL:
      if (types[idx].length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE) {
        values[idx].ld = va_arg(args, long double);
        continue;
      }
      values[idx].d = va_arg(args, double);
      continue;
#endif
    }
  }
  return 1;
}

int npf_verify_format(NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format) {
  enum {
    max_args = 64,
  };
  struct npf_arg_type args[max_args] = {0};
  int const used_max = npf_format_to_npf_arg_type(reference, max_args, args, 1);
  if (used_max == -1) {
    return 0;
  }
  // check for unused arguments
  for (int i = 1; i <= used_max; ++i) {
    if (args[i - 1].conv_spec == NPF_FMT_SPEC_CONV_PERCENT) {
      return 0;
    }
  }
  if (format == NULL || reference == format) {
    return 1;
  }
  int const n = npf_format_to_npf_arg_type(format, used_max, args, 0);
  if (n == -1) {
    return 0;
  }
  return 1;
}

static void npf_bufputc(int c, void *ctx) {
  npf_bufputc_ctx_t *bpc = (npf_bufputc_ctx_t *)ctx;
  if (bpc->cur < bpc->len) { bpc->dst[bpc->cur++] = (NPF_CHAR_TYPE)c; }
}

static void npf_bufputc_nop(int c, void *ctx) { (void)c; (void)ctx; }

typedef struct npf_cnt_putc_ctx {
  npf_putc pc;
  void *ctx;
  int n;
} npf_cnt_putc_ctx_t;

static void npf_putc_cnt(int c, void *ctx) {
  npf_cnt_putc_ctx_t *pc_cnt = (npf_cnt_putc_ctx_t *)ctx;
  ++pc_cnt->n;
  pc_cnt->pc(c, pc_cnt->ctx); // sibling-call optimization
}

struct ovutf_context {
  npf_putc pc;
  void *pc_ctx;
};

static enum ov_codepoint_fn_result write_codepoint(int_fast32_t codepoint, void *ctx) {
  struct ovutf_context *const c = ctx;
  if (sizeof(NPF_CHAR_TYPE) != sizeof(char)) {
    if (sizeof(wchar_t) == 2 && codepoint > 0xffff) {
      c->pc((int)((codepoint - 0x10000) / 0x400 + 0xd800), c->pc_ctx);
      c->pc((int)((codepoint - 0x10000) % 0x400 + 0xdc00), c->pc_ctx);
      return ov_codepoint_fn_result_continue;
    }
    c->pc((int)codepoint, c->pc_ctx);
    return ov_codepoint_fn_result_continue;
  }
  if (codepoint < 0x80) {
    c->pc((int)(codepoint & 0x7f), c->pc_ctx);
    return ov_codepoint_fn_result_continue;
  }
  if (codepoint < 0x800) {
    c->pc((int)(0xc0 | ((codepoint >> 6) & 0x1f)), c->pc_ctx);
    c->pc((int)(0x80 | (codepoint & 0x3f)), c->pc_ctx);
    return ov_codepoint_fn_result_continue;
  }
  if (codepoint < 0x10000) {
    c->pc((int)(0xe0 | ((codepoint >> 12) & 0x0f)), c->pc_ctx);
    c->pc((int)(0x80 | ((codepoint >> 6) & 0x3f)), c->pc_ctx);
    c->pc((int)(0x80 | (codepoint & 0x3f)), c->pc_ctx);
    return ov_codepoint_fn_result_continue;
  }
  c->pc((int)(0xf0 | ((codepoint >> 18) & 0x07)), c->pc_ctx);
  c->pc((int)(0x80 | ((codepoint >> 12) & 0x3f)), c->pc_ctx);
  c->pc((int)(0x80 | ((codepoint >> 6) & 0x3f)), c->pc_ctx);
  c->pc((int)(0x80 | (codepoint & 0x3f)), c->pc_ctx);
  return ov_codepoint_fn_result_continue;
}

#define NPF_PUTC(VAL) do { npf_putc_cnt((int)(VAL), &pc_cnt); } while (0)

#define NPF_EXTRACT(MOD, CAST_TO, FROM) \
  case NPF_FMT_SPEC_LEN_MOD_##MOD: val = (CAST_TO)(FROM); break

#define NPF_WRITEBACK(MOD, TYPE) \
  case NPF_FMT_SPEC_LEN_MOD_##MOD: *(va_arg(args, TYPE *)) = (TYPE)pc_cnt.n; break

int npf_vpprintf(npf_putc pc, void *pc_ctx, NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format, va_list args) {
  enum {
    max_args = 64,
  };
  struct npf_arg_type arg_types[max_args] = {0};
  union npf_arg_value arg_values[max_args] = {0};
  int used_args = npf_format_to_npf_arg_type(reference ? reference : format, max_args, arg_types, 1);
  if (used_args == -1) {
    return 0;
  }
  if (reference != format) {
    used_args = npf_format_to_npf_arg_type(format, used_args, arg_types, 0);
    if (used_args == -1) {
      return 0;
    }
  }
  if (npf_verify_and_assign_values(used_args, arg_types, arg_values, args) == 0) {
    return 0;
  }

  npf_format_spec_t fs;
  NPF_CHAR_TYPE const *cur = format;
  npf_cnt_putc_ctx_t pc_cnt;
  pc_cnt.pc = pc;
  pc_cnt.ctx = pc_ctx;
  pc_cnt.n = 0;

  int arg_index = 0;
  while (*cur) {
    int const fs_len = (*cur != '%') ? 0 : npf_parse_format_spec(cur, &fs);
    if (!fs_len) { NPF_PUTC(*cur++); continue; }
    cur += fs_len;

    // Extract star-args immediately
#if (NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1) && (NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1)
    if (fs.field_width_opt == NPF_FMT_SPEC_OPT_STAR) {
      fs.field_width = arg_values[(fs.field_width == 0 ? ++arg_index : fs.field_width) - 1].i;
      if (fs.field_width < 0) {
        fs.field_width = -fs.field_width;
        fs.left_justified = 1;
      }
    }
#endif
#if (NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1) && (NANOPRINTF_USE_FMT_SPEC_OPT_STAR == 1)
    if (fs.prec_opt == NPF_FMT_SPEC_OPT_STAR) {
      fs.prec = arg_values[(fs.prec == 0 ? ++arg_index : fs.prec) - 1].i;
      if (fs.prec < 0) { fs.prec_opt = NPF_FMT_SPEC_OPT_NONE; }
    }
#endif
    if (fs.conv_spec != NPF_FMT_SPEC_CONV_PERCENT && fs.order == 0) {
      fs.order = ++arg_index;
    }

    union { NPF_CHAR_TYPE cbuf_mem[NANOPRINTF_CONVERSION_BUFFER_SIZE]; npf_uint_t binval; } u;
    NPF_CHAR_TYPE *cbuf = u.cbuf_mem, sign_c = 0;
    int cbuf_len = 0, cbuf_origlen = 0, need_0x = 0;
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
    int field_pad = 0;
    NPF_CHAR_TYPE pad_c = 0;
#endif
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
    int prec_pad = 0;
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
    int zero = 0;
#endif
#endif

    // Extract and convert the argument to string, point cbuf at the text.
    switch (fs.conv_spec) {
      case NPF_FMT_SPEC_CONV_PERCENT:
        *cbuf = '%';
        cbuf_len = 1;
        break;

      case NPF_FMT_SPEC_CONV_CHAR:
        *cbuf = (NPF_CHAR_TYPE)(arg_values[fs.order - 1].i);
        cbuf_len = 1;
        break;

      case NPF_FMT_SPEC_CONV_STRING: {
      if (fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_NONE || fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_SHORT) {
        cbuf = (void *)arg_values[fs.order - 1].hstr;
        for (char const *s = (void *)cbuf; *s; ++s, ++cbuf_len)
          ; // strlen
        if (sizeof(NPF_CHAR_TYPE) != sizeof(char)) {
          cbuf_origlen = cbuf_len;
          if (cbuf_len) {
            cbuf_len = (int)ov_utf8_to_wchar_len((void *)cbuf, (size_t)cbuf_len);
            if (!cbuf_len) {
              return 0;
            }
          }
        }
      } else if (fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG) {
        cbuf = (void *)arg_values[fs.order - 1].lstr;
        for (wchar_t const *s = (void *)cbuf; *s; ++s, ++cbuf_len)
          ; // wcslen
        if (sizeof(NPF_CHAR_TYPE) != sizeof(wchar_t)) {
          cbuf_origlen = cbuf_len;
          if (cbuf_len) {
            cbuf_len = (int)ov_wchar_to_utf8_len((void *)cbuf, (size_t)cbuf_len);
            if (!cbuf_len) {
              return 0;
            }
          }
        }
      } else {
        return 0;
      }
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
        for (char const *s = cbuf;
             ((fs.prec_opt == NPF_FMT_SPEC_OPT_NONE) || (cbuf_len < fs.prec)) && *s;
             ++s, ++cbuf_len);
#else
        for (char const *s = cbuf; *s; ++s, ++cbuf_len); // strlen
#endif
      } break;

      case NPF_FMT_SPEC_CONV_SIGNED_INT: {
        npf_int_t val = 0;
        switch (fs.length_modifier) {
          NPF_EXTRACT(NONE, int, arg_values[fs.order - 1].i);
          NPF_EXTRACT(SHORT, short, arg_values[fs.order - 1].i);
          NPF_EXTRACT(LONG_DOUBLE, int, arg_values[fs.order - 1].i);
          NPF_EXTRACT(CHAR, char, arg_values[fs.order - 1].i);
          NPF_EXTRACT(LONG, long, arg_values[fs.order - 1].l);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
          NPF_EXTRACT(LARGE_LONG_LONG, long long, arg_values[fs.order - 1].ll);
          NPF_EXTRACT(LARGE_INTMAX, intmax_t, arg_values[fs.order - 1].imx);
          NPF_EXTRACT(LARGE_SIZET, ssize_t, arg_values[fs.order - 1].ssz);
          NPF_EXTRACT(LARGE_PTRDIFFT, ptrdiff_t, arg_values[fs.order - 1].ptrdiff);
#endif
          default: break;
        }

        sign_c = (val < 0) ? '-' : fs.prepend;

#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
        zero = !val;
#endif
        // special case, if prec and value are 0, skip
        if (!val && (fs.prec_opt != NPF_FMT_SPEC_OPT_NONE) && !fs.prec) {
          cbuf_len = 0;
        } else
#endif
        {
          npf_uint_t uval = (npf_uint_t)val;
          if (val < 0) { uval = 0 - uval; }
          cbuf_len = npf_utoa_rev(uval, cbuf, 10, fs.case_adjust);
        }
      } break;

#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
      case NPF_FMT_SPEC_CONV_BINARY:
#endif
      case NPF_FMT_SPEC_CONV_OCTAL:
      case NPF_FMT_SPEC_CONV_HEX_INT:
      case NPF_FMT_SPEC_CONV_UNSIGNED_INT: {
        npf_uint_t val = 0;

        switch (fs.length_modifier) {
          NPF_EXTRACT(NONE, unsigned, arg_values[fs.order - 1].u);
          NPF_EXTRACT(SHORT, unsigned short, arg_values[fs.order - 1].u);
          NPF_EXTRACT(LONG_DOUBLE, unsigned, arg_values[fs.order - 1].u);
          NPF_EXTRACT(CHAR, unsigned char, arg_values[fs.order - 1].u);
          NPF_EXTRACT(LONG, unsigned long, arg_values[fs.order - 1].ul);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
          NPF_EXTRACT(LARGE_LONG_LONG, unsigned long long, arg_values[fs.order - 1].ull);
          NPF_EXTRACT(LARGE_INTMAX, uintmax_t, arg_values[fs.order - 1].uimx);
          NPF_EXTRACT(LARGE_SIZET, size_t, arg_values[fs.order - 1].sz);
          NPF_EXTRACT(LARGE_PTRDIFFT, size_t, arg_values[fs.order - 1].sz);
#endif
          default: break;
        }

#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
        zero = !val;
#endif
        if (!val && (fs.prec_opt != NPF_FMT_SPEC_OPT_NONE) && !fs.prec) {
          // Zero value and explicitly-requested zero precision means "print nothing".
          if ((fs.conv_spec == NPF_FMT_SPEC_CONV_OCTAL) && fs.alt_form) {
            fs.prec = 1; // octal special case, print a single '0'
          }
        } else
#endif
#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
        if (fs.conv_spec == NPF_FMT_SPEC_CONV_BINARY) {
          cbuf_len = npf_bin_len(val); u.binval = val;
        } else
#endif
        {
          uint_fast8_t const base = (fs.conv_spec == NPF_FMT_SPEC_CONV_OCTAL) ?
            8u : ((fs.conv_spec == NPF_FMT_SPEC_CONV_HEX_INT) ? 16u : 10u);
          cbuf_len = npf_utoa_rev(val, cbuf, base, fs.case_adjust);
        }

        if (val && fs.alt_form && (fs.conv_spec == NPF_FMT_SPEC_CONV_OCTAL)) {
          cbuf[cbuf_len++] = '0'; // OK to add leading octal '0' immediately.
        }

        if (val && fs.alt_form) { // 0x or 0b but can't write it yet.
          if (fs.conv_spec == NPF_FMT_SPEC_CONV_HEX_INT) { need_0x = 'X'; }
#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
          else if (fs.conv_spec == NPF_FMT_SPEC_CONV_BINARY) { need_0x = 'B'; }
#endif
          if (need_0x) { need_0x += fs.case_adjust; }
        }
      } break;

      case NPF_FMT_SPEC_CONV_POINTER: {
        cbuf_len =
          npf_utoa_rev((npf_uint_t)(uintptr_t)(arg_values[fs.order - 1].ptr), cbuf, 16, 'a' - 'A');
        need_0x = 'x';
      } break;

#if NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS == 1
      case NPF_FMT_SPEC_CONV_WRITEBACK:
        switch (fs.length_modifier) {
          NPF_WRITEBACK(NONE, int);
          NPF_WRITEBACK(SHORT, short);
          NPF_WRITEBACK(LONG, long);
          NPF_WRITEBACK(LONG_DOUBLE, double);
          NPF_WRITEBACK(CHAR, char);
#if NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS == 1
          NPF_WRITEBACK(LARGE_LONG_LONG, long long);
          NPF_WRITEBACK(LARGE_INTMAX, intmax_t);
          NPF_WRITEBACK(LARGE_SIZET, size_t);
          NPF_WRITEBACK(LARGE_PTRDIFFT, ptrdiff_t);
#endif
          default: break;
        } break;
#endif

#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
      case NPF_FMT_SPEC_CONV_FLOAT_DEC:
      case NPF_FMT_SPEC_CONV_FLOAT_SCI:
      case NPF_FMT_SPEC_CONV_FLOAT_SHORTEST:
      case NPF_FMT_SPEC_CONV_FLOAT_HEX: {
        double val;
        if (fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG_DOUBLE) {
          val = (double)(arg_values[fs.order - 1].ld);
        } else {
          val = arg_values[fs.order - 1].d;
        }

        sign_c = (val < 0.) ? '-' : fs.prepend;
#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
        zero = (val == 0.);
#endif
        cbuf_len = npf_ftoa_rev(cbuf, &fs, val);
      } break;
#endif
      default: break;
    }

#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
    // Compute the field width pad character
    if (fs.field_width_opt != NPF_FMT_SPEC_OPT_NONE) {
      if (fs.leading_zero_pad) { // '0' flag is only legal with numeric types
        if ((fs.conv_spec != NPF_FMT_SPEC_CONV_STRING) &&
            (fs.conv_spec != NPF_FMT_SPEC_CONV_CHAR) &&
            (fs.conv_spec != NPF_FMT_SPEC_CONV_PERCENT)) {
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
          if ((fs.prec_opt != NPF_FMT_SPEC_OPT_NONE) && !fs.prec && zero) {
            pad_c = ' ';
          } else
#endif
          { pad_c = '0'; }
        }
      } else { pad_c = ' '; }
    }
#endif

    // Compute the number of bytes to truncate or '0'-pad.
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
    if (fs.conv_spec != NPF_FMT_SPEC_CONV_STRING) {
#if NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS == 1
      // float precision is after the decimal point
      if (fs.conv_spec != NPF_FMT_SPEC_CONV_FLOAT_DEC)
#endif
      { prec_pad = npf_max(0, fs.prec - cbuf_len); }
    }
#endif

#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
    // Given the full converted length, how many pad bytes?
    field_pad = fs.field_width - cbuf_len - !!sign_c;
    if (need_0x) { field_pad -= 2; }
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
    field_pad -= prec_pad;
#endif
    field_pad = npf_max(0, field_pad);

    // Apply right-justified field width if requested
    if (!fs.left_justified && pad_c) { // If leading zeros pad, sign goes first.
      if (pad_c == '0') {
        if (sign_c) { NPF_PUTC(sign_c); sign_c = 0; }
        // Pad byte is '0', write '0x' before '0' pad chars.
        if (need_0x) { NPF_PUTC('0'); NPF_PUTC(need_0x); }
      }
      while (field_pad-- > 0) { NPF_PUTC(pad_c); }
      // Pad byte is ' ', write '0x' after ' ' pad chars but before number.
      if ((pad_c != '0') && need_0x) { NPF_PUTC('0'); NPF_PUTC(need_0x); }
    } else
#endif
    { if (need_0x) { NPF_PUTC('0'); NPF_PUTC(need_0x); } } // no pad, '0x' requested.

    // Write the converted payload
    if (fs.conv_spec == NPF_FMT_SPEC_CONV_STRING) {
      if (fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_NONE || fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_SHORT) {
        if (sizeof(NPF_CHAR_TYPE) != sizeof(char) && cbuf_origlen) {
          if (!ov_utf8_to_codepoint(write_codepoint,
                                    &(struct ovutf_context){.pc = npf_putc_cnt, .pc_ctx = &pc_cnt},
                                    (void *)cbuf,
                                    (size_t)cbuf_origlen)) {
            return 0;
          }
        } else {
          for (int i = 0; i < cbuf_len; ++i) {
            NPF_PUTC(cbuf[i]);
          }
        }
      } else if (fs.length_modifier == NPF_FMT_SPEC_LEN_MOD_LONG) {
        if (sizeof(NPF_CHAR_TYPE) != sizeof(wchar_t) && cbuf_origlen) {
          if (!ov_wchar_to_codepoint(write_codepoint,
                                     &(struct ovutf_context){.pc = npf_putc_cnt, .pc_ctx = &pc_cnt},
                                     (void *)cbuf,
                                     (size_t)cbuf_origlen)) {
            return 0;
          }
        } else {
          for (int i = 0; i < cbuf_len; ++i) {
            NPF_PUTC(cbuf[i]);
          }
        }
    } else {
      if (sign_c) { NPF_PUTC(sign_c); }
#if NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS == 1
      while (prec_pad-- > 0) { NPF_PUTC('0'); } // int precision leads.
#endif
#if NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS == 1
      if (fs.conv_spec == NPF_FMT_SPEC_CONV_BINARY) {
        while (cbuf_len) { NPF_PUTC('0' + ((u.binval >> --cbuf_len) & 1)); }
      } else
#endif
      { while (cbuf_len-- > 0) { NPF_PUTC(cbuf[cbuf_len]); } } // payload is reversed
    }

#if NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS == 1
    if (fs.left_justified && pad_c) { // Apply left-justified field width
      while (field_pad-- > 0) { NPF_PUTC(pad_c); }
    }
#endif
  }

  return pc_cnt.n;
}

#undef NPF_PUTC
#undef NPF_EXTRACT
#undef NPF_WRITEBACK

int npf_pprintf(npf_putc pc, void *pc_ctx, NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format, ...) {
  va_list val;
  va_start(val, format);
  int const rv = npf_vpprintf(pc, pc_ctx, reference, format, val);
  va_end(val);
  return rv;
}

int npf_snprintf(NPF_CHAR_TYPE *buffer, size_t bufsz, NPF_CHAR_TYPE const *reference, const NPF_CHAR_TYPE *format, ...) {
  va_list val;
  va_start(val, format);
  int const rv = npf_vsnprintf(buffer, bufsz, reference, format, val);
  va_end(val);
  return rv;
}

int npf_vsnprintf(NPF_CHAR_TYPE *buffer, size_t bufsz,  NPF_CHAR_TYPE const *reference, NPF_CHAR_TYPE const *format, va_list vlist) {
  npf_bufputc_ctx_t bufputc_ctx;
  bufputc_ctx.dst = buffer;
  bufputc_ctx.len = bufsz;
  bufputc_ctx.cur = 0;

  npf_putc const pc = buffer ? npf_bufputc : npf_bufputc_nop;
  int const n = npf_vpprintf(pc, &bufputc_ctx, reference, format, vlist);
  pc('\0', &bufputc_ctx);

  if (buffer && bufsz) {
#ifdef NANOPRINTF_SNPRINTF_SAFE_EMPTY_STRING_ON_OVERFLOW
    if (n >= (int)bufsz) { buffer[0] = '\0'; }
#else
    buffer[bufsz - 1] = '\0';
#endif
  }

  return n;
}

#if NANOPRINTF_HAVE_GCC_WARNING_PRAGMAS
  #pragma GCC diagnostic pop
#endif

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif // NANOPRINTF_IMPLEMENTATION_INCLUDED
#endif // NANOPRINTF_IMPLEMENTATION

/*
  nanoprintf is dual-licensed under both the "Unlicense" and the
  "Zero-Clause BSD" (0BSD) licenses. The intent of this dual-licensing
  structure is to make nanoprintf as consumable as possible in as many
  environments / countries / companies as possible without any
  encumberances.

  The text of the two licenses follows below:

  ============================== UNLICENSE ==============================

  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org>

  ================================ 0BSD =================================

  Copyright (C) 2019- by Charles Nicholson <charles.nicholson+nanoprintf@gmail.com>

  Permission to use, copy, modify, and/or distribute this software for
  any purpose with or without fee is hereby granted.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
