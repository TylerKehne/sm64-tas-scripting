#!/usr/bin/env python3

'''
Basic install script to pull packages from GitHub and export them to Conan cache.
'''

import subprocess as subp
import shutil
import tarfile
import urllib.request as request
import tempfile as tmp
import contextlib
from pathlib import Path

def download(package_name, tar_url):
	with tmp.TemporaryDirectory() as dir:
		req = request.Request(tar_url)
		conn = request.urlopen(req)
		try:
			with tarfile.open(fileobj=conn, mode="r:gz") as tar:
				tar.extractall(dir)
		finally:
			conn.close()
		
		subp.run(["conan", "export", [i for i in Path(dir).iterdir()][0]])
		
		
		

def install(package_name, tar_url):
	if shutil.which("conan") is None:
		raise RuntimeError("Conan is not installed. Install Conan with \"pip install conan\" (without the quotes)")
	
	# Search local cache for package
	search = subp.run(["conan", "search", package_name, "--raw"], capture_output=True, check=True, encoding="utf-8")
	if search.stdout == "":
		print(f"{package_name} is not present. Downloading from GitHub...", flush=True)
		download(package_name, tar_url)
	else:
		print(f"{package_name} is already installed.")
	
	

if __name__ == "__main__":
	install("mtap/0.2", "https://github.com/jgcodes2020/mtap/archive/refs/tags/v0.2.tar.gz")