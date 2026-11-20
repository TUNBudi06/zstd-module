#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

typedef struct { uint8_t tag; int64_t int_; const uint8_t *ptr; size_t len; } TpVal;

/* fn 0 decompress(buf)->buf, fn 1 frame_clen(buf)->int, fn 2 compress(buf)->buf */
static const uint8_t LFNT[] = {
  'L','F','N','T', 3,0,
  0,0, 1, 3, 10, 'd','e','c','o','m','p','r','e','s','s',
  1,0, 1, 1, 10, 'f','r','a','m','e','_','c','l','e','n',
  2,0, 1, 3, 8,  'c','o','m','p','r','e','s','s'
};

uint32_t qtz_abi_version(void) { return 1; }
const uint8_t *qtz_lfnt(size_t *len_out) { *len_out = sizeof LFNT; return LFNT; }

static int fail(TpVal *ret, const char *msg) {
  size_t n = strlen(msg);
  uint8_t *p = malloc(n ? n : 1);
  if (!p) return 1;
  memcpy(p, msg, n);
  ret->tag = 2; ret->ptr = p; ret->len = n;
  return 3;
}

static int do_decompress(const TpVal *a, TpVal *ret) {
  size_t clen = ZSTD_findFrameCompressedSize(a->ptr, a->len);
  if (ZSTD_isError(clen)) return fail(ret, ZSTD_getErrorName(clen));
  unsigned long long cs = ZSTD_getFrameContentSize(a->ptr, clen);
  uint8_t *dst;
  size_t n;
  if (cs != ZSTD_CONTENTSIZE_UNKNOWN && cs != ZSTD_CONTENTSIZE_ERROR) {
    dst = malloc(cs ? cs : 1);
    if (!dst) return fail(ret, "out of memory");
    n = ZSTD_decompress(dst, cs, a->ptr, clen);
    if (ZSTD_isError(n)) { free(dst); return fail(ret, ZSTD_getErrorName(n)); }
  } else {
    ZSTD_DStream *ds = ZSTD_createDStream();
    size_t cap = 1 << 16;
    n = 0;
    dst = malloc(cap);
    if (!ds || !dst) { ZSTD_freeDStream(ds); free(dst); return fail(ret, "out of memory"); }
    ZSTD_inBuffer in = { a->ptr, clen, 0 };
    for (;;) {
      if (n == cap) {
        uint8_t *t = realloc(dst, cap * 2);
        if (!t) { ZSTD_freeDStream(ds); free(dst); return fail(ret, "out of memory"); }
        dst = t; cap *= 2;
      }
      ZSTD_outBuffer out = { dst, cap, n };
      size_t r = ZSTD_decompressStream(ds, &out, &in);
      n = out.pos;
      if (ZSTD_isError(r)) { ZSTD_freeDStream(ds); free(dst); return fail(ret, ZSTD_getErrorName(r)); }
      if (r == 0) break;
      if (in.pos == in.size && n < cap) { ZSTD_freeDStream(ds); free(dst); return fail(ret, "truncated frame"); }
    }
    ZSTD_freeDStream(ds);
  }
  ret->tag = 3; ret->ptr = dst; ret->len = n;
  return 0;
}

int32_t qtz_call(uint16_t fn_id, const TpVal *args, uint8_t argc, TpVal *ret) {
  ret->tag = 0; ret->int_ = 0; ret->ptr = NULL; ret->len = 0;
  if (argc != 1 || args[0].tag != 3) return fail(ret, "expected one buf argument");
  const TpVal *a = &args[0];
  switch (fn_id) {
  case 0: return do_decompress(a, ret);
  case 1: {
    size_t c = ZSTD_findFrameCompressedSize(a->ptr, a->len);
    ret->tag = 1; ret->int_ = ZSTD_isError(c) ? -1 : (int64_t)c;
    return 0;
  }
  case 2: {
    size_t bound = ZSTD_compressBound(a->len);
    uint8_t *dst = malloc(bound);
    if (!dst) return fail(ret, "out of memory");
    size_t n = ZSTD_compress(dst, bound, a->ptr, a->len, 19);
    if (ZSTD_isError(n)) { free(dst); return fail(ret, ZSTD_getErrorName(n)); }
    ret->tag = 3; ret->ptr = dst; ret->len = n;
    return 0;
  }
  }
  return 2;
}

void qtz_free(uint8_t *ptr, size_t len) { (void)len; free(ptr); }
