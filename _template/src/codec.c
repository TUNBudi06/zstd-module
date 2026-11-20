/* Native codec lib for a QuickTZS module. Build:
 *   cc -shared -fPIC -O2 -o lib/codec.so src/codec.c   (add -l<codec> if you link one)
 * See docs/04-libraries-and-platforms.md and docs/07-porting-bms-for-ai.md.
 *
 * Exports the fixed QuickTZS C ABI: qtz_abi_version, qtz_lfnt, qtz_call, qtz_free.
 * TODO: replace the three stub functions with real codec calls (zstd/zlib/...). */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t tag; int64_t int_; const uint8_t *ptr; size_t len; } TpVal;
/* tag: 0 none, 1 int, 2 str, 3 buf. str/buf = ptr+len, no NUL terminator. */

/* LFNT export table: "LFNT", <fn_count u16 LE>, then per fn:
 *   <fn_id u16 LE> <arg_count u8> <ret_type u8> <name_len u8> <name...>
 * ret_type: 0 none 1 int 2 str 3 buf 4 arr 5 object 6 tuple.
 * TODO: keep this in sync with the functions you implement below. */
static const uint8_t LFNT[] = {
  'L','F','N','T', 3,0,
  0,0, 1, 3, 10, 'd','e','c','o','m','p','r','e','s','s',  /* 0 decompress(buf)->buf */
  1,0, 1, 1, 10, 'f','r','a','m','e','_','c','l','e','n',  /* 1 frame_clen(buf)->int */
  2,0, 1, 3, 8,  'c','o','m','p','r','e','s','s'           /* 2 compress(buf)->buf   */
};

uint32_t qtz_abi_version(void) { return 1; }
const uint8_t *qtz_lfnt(size_t *len_out) { *len_out = sizeof LFNT; return LFNT; }

/* Set ret to an error message and return non-zero. */
static int fail(TpVal *ret, const char *msg) {
  size_t n = strlen(msg);
  uint8_t *p = malloc(n ? n : 1);
  if (!p) return 1;
  memcpy(p, msg, n);
  ret->tag = 2; ret->ptr = p; ret->len = n;
  return 3;
}

int32_t qtz_call(uint16_t fn_id, const TpVal *args, uint8_t argc, TpVal *ret) {
  ret->tag = 0; ret->int_ = 0; ret->ptr = NULL; ret->len = 0;
  if (argc != 1 || args[0].tag != 3) return fail(ret, "expected one buf argument");
  const TpVal *a = &args[0];
  switch (fn_id) {
  case 0: /* decompress: decode ONE frame at a->ptr, return decompressed bytes */
    return fail(ret, "TODO: decompress not implemented");
  case 1: /* frame_clen: compressed length of the first frame, or -1 on error */
    ret->tag = 1; ret->int_ = -1; return 0; /* TODO */
  case 2: /* compress: encode a->ptr into one frame, return it */
    return fail(ret, "TODO: compress not implemented");
  }
  return 2;
}

void qtz_free(uint8_t *ptr, size_t len) { (void)len; free(ptr); }
