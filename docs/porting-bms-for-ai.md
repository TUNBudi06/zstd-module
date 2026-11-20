# 7. Porting a QuickBMS `.bms` to QuickTZS — a guide for AI agents

You are an AI agent handed a QuickBMS `.bms` script and asked to port it to a QuickTZS
module (unpacker, and often a repacker). This guide is the procedure, the command mapping,
the native-lib ABI, and the runtime quirks that actually bite — everything here was
verified by building and running the worked example in [`modules/ue-zstd/`](../ue-zstd).

Read the real references too: [`docs/03-unpacking-binaries.md`](https://github.com/TUNBudi06/tun-compiler/blob/main/docs/03-unpacking-binaries.md)
(script idioms), [`docs/04-libraries-and-platforms.md`](https://github.com/TUNBudi06/tun-compiler/blob/main/docs/04-libraries-and-platforms.md)
(the lib ABI), and the spec under [`spec/`](https://github.com/TUNBudi06/tun-compiler/blob/main/spec/00-overview.md).

---

## 0. Procedure (follow in order)

1. **Read the whole `.bms` and state what it does** in plain terms: the container layout,
   how files are located (TOC? scan for a magic?), the codec, how names/extensions are
   decided. Do not start translating until you can describe the format.
2. **Decide what becomes a native lib.** Every codec (compression, encryption, hashing) is
   a `.so` called via `import`, never a keyword. See §2 and §3. If the `.bms` hand-parses a
   format header only to feed the codec exact sizes, check whether the real library already
   knows those sizes (zstd/zlib frames are self-delimiting) — if so, **delete that parser**
   and let the lib do it.
3. **Write the lib** (`src/<name>.c` → `lib/<name>.so`), following the C ABI in §3 exactly.
   `quicktzs lib-check lib/<name>.so` must pass and list every export.
4. **Write `scripts/unpack.tzst`** using the mapping in §1 and the idioms in §4.
5. **Write `scripts/repack.tzst` only if asked.** Most `.bms` scripts are unpack-only;
   repack is new logic you design (§5). It needs the codec's *compress* side.
6. **Run the round-trip test gate (§6). Do not claim success until it passes.**

Start from [`modules/_template/`](../_template) — copy it and fill the `TODO`s.

---

## 1. QuickBMS → QuickTZS `.tzst` mapping

| QuickBMS | QuickTZS | Notes |
|---|---|---|
| `open FDSE "f"` | `glob <root> <pat>` + `select` | paths come from a `NEED read` root; never built from input |
| `get X long/short/byte` | `read_u32 / read_u16 / read_u8 <src> <off>` | honors `endian`; `src` is a path **or a `buf`** |
| `getdstring X n` | `read_str <src> <off> n` (str) / `read <src> <off> n` (buf) | |
| `idstring "MAGIC"` | `read_u32 @f 0` then `if @m == 0x...` | mind endianness (§4) |
| `comtype zstd/zlib/...` | `import "lib/<codec>"` | each codec is a `.so` you build (§3) |
| `clog NAME off zsize size` | `let @raw = read @f @off @zlen;` `lib.decompress(@raw)` | you usually don't need `zsize`; see §2 |
| `log NAME off size` | `read @f @off @size` → `write ... -> <path> 0;` | uncompressed extract |
| `findloc VAR binary "\xNN.."` | `findloc <path> <pat> [from]` | build `<pat>` with `hex.decode("..")` (§4) |
| `goto off` / `savepos VAR` | track an int `@pos` yourself; `align @pos n` | there is no file cursor |
| `getbits` | **not available** — read bytes and shift/mask (`shl`, `|`, `%`), or push the parse into the lib | the example dropped its `getbits` parser entirely (§2) |
| `for i = 0 < N` | `for @i in 0..@n { }` or `while` | `for` runs 0 times if `n <= 0` |
| `putarray/getarray` | `let @a = []; push @a <- @x; @a[@k]` | arrays are first-class; can't cross the lib boundary |
| `string NAME p "%08d" i` | build it manually: `substr ("00000000"+str(@i)) ...` | no printf padding (§4) |
| `math x += y` / `xmath` | `@x = @x + @y;` | plain expressions |
| reimport (repack) | design it (§5) | no built-in reimport |

---

## 2. The key simplification: let the codec find frame boundaries

Many `.bms` scripts contain a painful manual header parser (`getbits`, block walking) whose
**only purpose** is to compute a compressed chunk's exact size so `clog` can be told it.
A real codec does not need this: a zstd or zlib frame is self-delimiting.

So expose a lib function that decodes **one frame and reports how many compressed bytes it
consumed**, and the whole parser disappears:

- `decompress(buf) -> buf` — decode the first frame in `buf`, ignore trailing bytes.
- `frame_clen(buf) -> int` — the compressed length of that first frame, to advance the cursor.

In the script you then read a bounded window (not the whole file), decode, and advance by
`frame_clen`. This is smaller and more robust than trusting a scanned "next magic" offset,
because the decoder gives the true frame end. (For zstd: `ZSTD_findFrameCompressedSize`.)

---

## 3. The native lib C ABI (copy-paste ready)

A lib is a `.so` exporting four C symbols. Build: `mkdir -p lib && cc -shared -fPIC -O2 -o lib/x.so src/x.c <-lcodec>`.
Keep the C source in `src/`, **not** in `lib/` — `dev run` rejects a non-`.so` file in `lib/`.

```c
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t tag; int64_t int_; const uint8_t *ptr; size_t len; } TpVal;
/* tag: 0 none, 1 int, 2 str, 3 buf.  str/buf = ptr+len, no NUL.            */
/* A returned str/buf is malloc'd by the lib and freed later via qtz_free.  */

/* LFNT export table: "LFNT", <fn_count u16 LE>, then per fn:
   <fn_id u16 LE> <arg_count u8> <ret_type u8> <name_len u8> <name...>
   ret_type: 0 none 1 int 2 str 3 buf 4 arr 5 object 6 tuple            */
static const uint8_t LFNT[] = {
  'L','F','N','T', 1,0,
  0,0, 1, 3, 3, 'f','o','o'          /* fn 0 "foo"(buf)->buf */
};

uint32_t qtz_abi_version(void) { return 1; }                 /* must be 1 */
const uint8_t *qtz_lfnt(size_t *n) { *n = sizeof LFNT; return LFNT; }

int32_t qtz_call(uint16_t fn_id, const TpVal *args, uint8_t argc, TpVal *ret) {
  ret->tag = 0; ret->int_ = 0; ret->ptr = NULL; ret->len = 0;
  /* validate argc and each args[i].tag; dispatch on fn_id.
     On success return 0. On error return non-zero and optionally set ret
     to a tag-2 str message. NEVER unwind/throw across this boundary.     */
  return 2;
}

void qtz_free(uint8_t *ptr, size_t len) { (void)len; free(ptr); }
```

Rules that matter: only none/int/str/buf cross the boundary (arr/object/tuple error before
the call); arg memory is valid only during the call; allocate and free a returned buffer
with the same allocator; ids and names must be unique; `arg_count` is enforced at call time.
If you write the lib in Rust, build it `panic = "abort"` (never unwind across C).

---

## 4. Runtime quirks that will bite (all verified)

- **No `min`/`max`.** Use an `if`.
- **`exit N` needs a *literal* string.** `exit 2 f"..."` is a parse error. `print` the
  f-string first, then `exit 2 "short reason";`.
- **`findloc` takes a `buf` pattern.** Build raw magic bytes with
  `import "std/hex"; let @magic = hex.decode("28b52ffd");` — a string literal can't hold
  bytes > 0x7f. Form: `findloc <path> <pat> [from]`, returns an absolute offset or `-1`.
- **Typed reads accept a `buf` source**, not only a path: `read_u32 @decompressed_buf 0`.
- **`buf + buf` concatenates** (and `"s" + str(@n)`). Use this to assemble a file in memory.
- **`write @d -> @path @off` is atomic = a full rewrite each call** (temp file + rename).
  Writing K chunks at K offsets is **O(K²) disk**. Prefer: concatenate the chunks into one
  `buf` and do a **single** `write`; or use `append`.
- **`append` is staged**: it commits only when the program ends normally, at most **64**
  files may be staged at once, and it **appends onto an existing target** — clear the output
  file before a re-run (there is no delete op; overwrite its first frame with `write ... 0`).
- **`glob` is not recursive and allows no `/` in the pattern.** Enumerate subdirectories
  explicitly (e.g. loop buckets `file_0/`, `file_1000/`, ...).
- **`NEED` paths are absolute string literals** and go in the signed manifest. Anything
  outside a `NEED` root is exit 126. Keep every copy of a path (the `NEED`, the `glob` root,
  any variable) in sync.
- **`select` reads a number from stdin.** In tests pipe it: `echo 1 | quicktzs dev run main.tun`.
- **Endianness**: set `endian little;` / `endian big;` before typed reads; the default is
  little-endian with a compiler warning. On-disk magic `C1 83 2A 9E` reads as `0x9E2A83C1`
  little-endian.
- **String slicing** is `substr @s <start> <len>`; there is no printf. Zero-pad like:
  `fn pad8(@i)->(@s){ let @t="00000000"+str(@i); return substr @t (len(@t)-8) 8; }`.
- **Bit ops** `shl`, `|`, `%` exist (used for header fields when you must parse bytes).

---

## 5. Repack (when there is no reimport in the `.bms`)

You are designing the inverse of your unpacker. For a scan-located, no-TOC container
(frames back-to-back), repack is: enumerate the extracted files in unpack order, cut each
into the same chunk size, `codec.compress` each piece into a frame, and concatenate.

Pin these down and write them in a header comment:
- **Chunk boundary rule must match unpack exactly.** If unpack ends a file on "the first
  chunk smaller than N", then a file whose size is an exact multiple of N must end with a
  short (possibly 0-byte) terminator frame, or re-unpack will merge it with the next file.
- **Lossy cases.** Frames unpack *skipped* (e.g. encrypted) cannot be reproduced. State it.
- **No absolute offsets.** Only works because the format locates frames by scanning. If the
  real format has a TOC with offsets, repack must rebuild and fix the TOC — a bigger job.

Repack needs the codec's **compress** export, so add it to the lib in §3.

---

## 6. The test gate (mandatory)

Never report success on a port you did not round-trip. Build a **synthetic** container (you
control the inputs), then:

1. Unpack it → compare every output file byte-for-byte against the known originals.
2. Repack the outputs → unpack *that* → the twice-processed files must equal the once-unpacked
   files (`diff -r` clean).
3. Include the awkward cases: a file smaller than one chunk, one exactly one chunk, one
   spanning several chunks, an exact multiple of the chunk size, an empty file, and (if the
   format has them) a skipped/encrypted frame.

Run with `echo 1 | quicktzs dev run <main.tun>`. Write the synthetic-container generator and
the round-trip driver as throwaway scripts in a scratch directory outside the module; mirror
the shape of the `ue-zstd` example.

---

## 7. Worked example

[`modules/ue-zstd/`](../ue-zstd) is a complete, round-trip-verified port of an
Unreal Engine extractor (`.bms`) whose container is back-to-back zstd frames with no TOC.
Read its `src/zstd.c` (the three-function lib), `scripts/unpack.tzst`, and
`scripts/repack.tzst` as a concrete reference for everything above. Its known limitations
(O(K²) writes for very large multi-chunk files; a 128 KiB per-frame read window; Linux/glibc
`.so` only — a Termux/bionic build is a separate compile) are exactly the things this guide
tells you to watch for.
