"""Fetch the build's dependency tarballs into build/downloads, with retries.

CMake's FetchContent reads that directory before downloading (TASFW_DOWNLOAD_DIR in
CMakeLists.txt) and verifies each file's hash, so a tarball put there is used as is. CI
restores the directory from its cache and runs this first, so a run does not fail on the
504 GitHub's release and archive downloads return now and then; it is also the way to
prepare an offline build. The names, URLs and hashes come from the CMake files, one list.
A download that keeps failing is reported and left to CMake, so this never fails a build.

    python scripts/fetch_deps.py [--dir build/downloads] [--retries 6]
"""
import argparse
import hashlib
import pathlib
import re
import sys
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CMAKE_FILES = ["CMakeLists.txt", "tasfw-tests/CMakeLists.txt", "tasfw-perf/CMakeLists.txt"]
DECLARATION = re.compile(r"DOWNLOAD_NAME\s+(\S+)\s+URL\s+(\S+)\s+URL_HASH\s+SHA256=([0-9a-f]{64})")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(url, retries):
    for attempt in range(1, retries + 1):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "tasfw-fetch-deps"})
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read()
        except Exception as e:  # HTTP errors, timeouts, resets: all worth another try
            print(f"  attempt {attempt} of {retries} failed: {e}")
            if attempt < retries:
                time.sleep(5 * attempt)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dir", default=str(ROOT / "build" / "downloads"), help="where CMake looks for the tarballs")
    parser.add_argument("--retries", type=int, default=6)
    args = parser.parse_args()
    out = pathlib.Path(args.dir)
    out.mkdir(parents=True, exist_ok=True)

    left_to_cmake = []
    for cmake_file in CMAKE_FILES:
        text = (ROOT / cmake_file).read_text(encoding="utf-8")
        for name, url, sha in DECLARATION.findall(text):
            path = out / name
            if path.exists() and sha256_of(path) == sha:
                print(f"{name}: cached")
                continue
            print(f"{name}: fetching {url}")
            data = fetch(url, args.retries)
            if data is None:
                left_to_cmake.append(name)
                continue
            if hashlib.sha256(data).hexdigest() != sha:
                print(f"{name}: the download's hash is not the one CMakeLists.txt expects; left to CMake")
                left_to_cmake.append(name)
                continue
            path.write_bytes(data)
            print(f"{name}: fetched, {len(data)} bytes")
    if left_to_cmake:
        print("not fetched, CMake will try itself: " + ", ".join(left_to_cmake))
    return 0


if __name__ == "__main__":
    sys.exit(main())
