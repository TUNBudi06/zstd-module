# quicktzs-modules

QuickTZS modules and a guide for porting QuickBMS (`.bms`) scripts to QuickTZS. This is a
**separate project that uses QuickTZS** — it is not part of the
[tun-compiler](https://github.com/TUNBudi06/tun-compiler) repo.

## Contents

- **[`docs/porting-bms-for-ai.md`](docs/porting-bms-for-ai.md)** — a guide for an AI agent
  (or a person) porting a `.bms` to a QuickTZS module: the command mapping, the native-lib
  C ABI, the runtime quirks that bite, and the round-trip test gate.
- **[`_template/`](_template/)** — a copy-me skeleton: `main.tun`, a native-lib C stub, and
  `unpack`/`repack` script skeletons with `TODO`s.
- **[`ue-zstd/`](ue-zstd/)** — a complete, round-trip-verified example: an Unreal Engine
  extractor (container = back-to-back zstd frames, no TOC) ported from a `.bms`.

## Prerequisites

You need the QuickTZS CLI (`quicktzs`, `exectzs`) on your `PATH`. Build it from the
[tun-compiler](https://github.com/TUNBudi06/tun-compiler) repo (`cargo build --release
--workspace`, then add `target/release` to `PATH`), or download a release build.

## Quick start

```sh
cp -r _template my-module && cd my-module
mkdir -p lib && cc -shared -fPIC -O2 -o lib/codec.so src/codec.c   # fill in src/codec.c first
quicktzs lib-check lib/codec.so
# edit the NEED paths in scripts/, then:
echo 1 | quicktzs dev run main.tun
```

Round-trip test before you trust it — see the porting guide, section 6.

## Note on the native `.so`

A built `lib/*.so` is platform-specific (Linux/glibc here). For Termux/Android build a
second `.so` with the NDK. Prefer committing the C source and build command, not the binary.
