# _template — a starting point for porting a `.bms` to QuickTZS

Copy this folder and fill in the `TODO`s. It is the skeleton the AI porting guide
([`docs/porting-bms-for-ai.md`](../docs/porting-bms-for-ai.md)) expects. A complete,
round-trip-verified example is [`ue-zstd/`](../ue-zstd).

```
_template/
  main.tun            # launcher: loads the lib, runs a script
  src/codec.c         # native lib source (C ABI) -> build to lib/codec.so
  scripts/unpack.tzst # read the container, extract files
  scripts/repack.tzst # rebuild the container (only if you need it)
```

## Build the lib

Keep the C source in `src/` (a non-`.so` file in `lib/` makes `dev run` fail):

```sh
mkdir -p lib && cc -shared -fPIC -O2 -o lib/codec.so src/codec.c   # add -l<yourcodec> if you link one
quicktzs lib-check lib/codec.so
```

## Run

Edit the `NEED` paths in the scripts to absolute directories you control, then:

```sh
echo 1 | quicktzs dev run main.tun
```

## Before you say it works

Round-trip test it on a synthetic container (unpack → compare to originals; repack → unpack
again → `diff -r`). See §6 of the porting guide.
