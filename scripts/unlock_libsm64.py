#!/usr/bin/env python3
"""Unlock a libsm64 build and lay out the per-thread copies.

  unlock_libsm64.py --rom <JP ROM> --out res --copies 24              fresh machine: res/sm64_jp_0.dll .. sm64_jp_23.dll
  unlock_libsm64.py --key-env LIBSM64_KEY --out <dir>                  CI: the key comes from the environment
  unlock_libsm64.py --rom <JP ROM> --print-key                         the value for the LIBSM64_KEY repository secret
  options: --platform windows|linux (default: this one), --version jp|us (default jp),
           --locked <file> (default: fetch the pinned bitfs-sbb build into build/downloads/),
           --copies N (default 1), --name <pattern with {} for the index> (default sm64_<version>_{}.<ext>)

The builds are bitfs-sbb's (https://github.com/jgcodes2020/bitfs-sbb) at the commit pinned
below, fetched by raw URL and checked against the sha256 recorded here before and after
unlocking, so a wrong key or a changed upstream file fails loudly. They are not the pinned
2022 wafel DLL; they pass every check against it (docs/libsm64.md, "Known builds").

The key is what wafel 0.7.1 derives and bitfs-sbb's fernet-lock.py reproduces:
PBKDF2-HMAC-SHA256 over the ROM in z64 byte order, no salt, 10,000 iterations, 32 bytes,
urlsafe base64. That derivation is standard library; the Fernet decryption needs the
`cryptography` package. The ROM is read for the derivation and nothing else; only the derived
key ever leaves this script, and only through --print-key. Never commit a ROM, an unlocked
binary or the key (AGENTS.md hard rule 6).
"""
import argparse
import base64
import hashlib
import os
import pathlib
import platform
import struct
import sys
import urllib.request

BITFS_SBB_COMMIT = "a33d76c0dd50195eecad5055c439f4f3b33fd0bd"   # 2026-07-06
RAW_URL = "https://raw.githubusercontent.com/jgcodes2020/bitfs-sbb/%s/%s"

# (platform, version): the locked file in bitfs-sbb, its sha256, and the unlocked build's.
BUILDS = {
    ("windows", "jp"): dict(path="libsm64/data/win32/sm64_jp.dll.locked", ext="dll",
                            locked="b31adb199c3025d5667565c40f2d810e809fcc4528491ddd11fc8a1415139854",
                            unlocked="960fe979068b78e6733b3ddb87741833fbf4eb29b2a21a2bba871977427c2d0f"),
    ("windows", "us"): dict(path="libsm64/data/win32/sm64_us.dll.locked", ext="dll",
                            locked="01f9b8d222a32385358fecbb14f088038533eea35dc6def015d628b5e6fb6b12",
                            unlocked="15cec32895d5e8d27237602a69035a51661865725c90b48a5778526a19229002"),
    ("linux", "jp"): dict(path="libsm64/data/linux/sm64_jp.so.locked", ext="so",
                          locked="ffda1d774bb5abc9ccbba4a8c636f86ddfe8b6285da4d2e6fade2aaa287bc6d5",
                          unlocked="60d762588f8f63c2d7b674507761cad087ed328d826bbfbd08c2701950f3cf31"),
    ("linux", "us"): dict(path="libsm64/data/linux/sm64_us.so.locked", ext="so",
                          locked="40c54bca16a554f3979245c9d2a42cd922d170634a69e8d3685ed737997d96bd",
                          unlocked="7b4d2fcef8f853d8c541f5f1f7a8b78b118aac84666d05350e259b8f1d37cec3"),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def rom_to_z64(rom):
    """Byte-swap a .v64 or .n64 image to .z64 order; .z64 passes through."""
    head = struct.unpack(">I", rom[:4])[0]
    if head == 0x80371240:
        return rom
    if head == 0x37804012:
        n = len(rom) // 2
        return struct.pack(">%dH" % n, *struct.unpack("<%dH" % n, rom[:n * 2]))
    if head == 0x40123780:
        n = len(rom) // 4
        return struct.pack(">%dI" % n, *struct.unpack("<%dI" % n, rom[:n * 4]))
    raise SystemExit("not an N64 ROM image (unknown byte order marker %08x)" % head)


def derive_key(rom_path):
    with open(rom_path, "rb") as f:
        rom = rom_to_z64(f.read())
    return base64.urlsafe_b64encode(hashlib.pbkdf2_hmac("sha256", rom, b"", 10_000, 32))


def fetch(build, cache_dir):
    """The locked file from the pinned commit, cached and verified."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = cache_dir / pathlib.Path(build["path"]).name
    if target.exists() and sha256(target.read_bytes()) == build["locked"]:
        return target
    url = RAW_URL % (BITFS_SBB_COMMIT, build["path"])
    print("fetching %s" % url)
    with urllib.request.urlopen(url) as response:
        data = response.read()
    if sha256(data) != build["locked"]:
        raise SystemExit("downloaded %s does not match the pinned sha256 (%s)" % (url, build["locked"]))
    target.write_bytes(data)
    return target


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--rom", help="the vanilla ROM (.z64, .v64 or .n64) the key is derived from")
    source.add_argument("--key-env", metavar="NAME", help="environment variable holding the derived key")
    ap.add_argument("--print-key", action="store_true", help="print the derived key (for the repository secret) and exit")
    ap.add_argument("--platform", choices=["windows", "linux"], default="windows" if platform.system() == "Windows" else "linux")
    ap.add_argument("--version", choices=["jp", "us"], default="jp")
    ap.add_argument("--locked", help="an already downloaded locked file instead of fetching the pinned one")
    ap.add_argument("--out", help="directory for the unlocked copies")
    ap.add_argument("--copies", type=int, default=1, help="how many copies to write (one per search thread)")
    ap.add_argument("--name", help="file name pattern with {} for the copy index (default sm64_<version>_{}.<ext>)")
    ap.add_argument("--cache", default=str(pathlib.Path(__file__).resolve().parent.parent / "build" / "downloads"),
                    help="where fetched locked files are kept (default build/downloads)")
    args = ap.parse_args(argv)

    if args.rom:
        key = derive_key(args.rom)
    else:
        value = os.environ.get(args.key_env, "")
        if not value:
            raise SystemExit("environment variable %s is empty; nothing to unlock with" % args.key_env)
        key = value.strip().encode("ascii")
    if args.print_key:
        print("the key is a secret: paste it into the LIBSM64_KEY repository secret and nowhere else", file=sys.stderr)
        print(key.decode("ascii"))
        return 0
    if not args.out:
        ap.error("--out is required unless --print-key is given")

    try:
        from cryptography.fernet import Fernet, InvalidToken
    except ImportError:
        raise SystemExit("the cryptography package is needed to unlock (pip install cryptography)")

    build = BUILDS[(args.platform, args.version)]
    locked_path = pathlib.Path(args.locked) if args.locked else fetch(build, pathlib.Path(args.cache))
    locked = locked_path.read_bytes()
    if sha256(locked) != build["locked"]:
        raise SystemExit("%s is not the pinned locked build (sha256 %s)" % (locked_path, build["locked"]))
    try:
        unlocked = Fernet(key).decrypt(locked)
    except InvalidToken:
        raise SystemExit("unlock failed: the key does not fit this build (derived from the wrong ROM, or mistyped)")
    if sha256(unlocked) != build["unlocked"]:
        raise SystemExit("unlocked bytes do not match the recorded sha256 (%s); refusing to write them" % build["unlocked"])

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    name = args.name or ("sm64_%s_{}.%s" % (args.version, build["ext"]))
    for i in range(args.copies):
        (out / name.format(i)).write_bytes(unlocked)
    print("%d cop%s of the %s %s build (bitfs-sbb %s) written to %s as %s"
          % (args.copies, "y" if args.copies == 1 else "ies", args.platform, args.version.upper(),
             BITFS_SBB_COMMIT[:7], out, name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
