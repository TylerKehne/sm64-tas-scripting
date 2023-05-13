#!/usr/bin/env python3

'''
Basic install script to pull packages from GitHub and export them to Conan cache.
'''

import subprocess as subp
import shutil
import tarfile
import urllib.request as request
import tempfile as tmp
from pathlib import Path

def download(package_name, tar_url):
	with tmp.TemporaryDirectory() as dir:
		req = request.Request(tar_url)
		conn = request.urlopen(req)
		try:
			with tarfile.open(fileobj=conn, mode="r:gz") as tar:
	
	import os
	
	def is_within_directory(directory, target):
		
		abs_directory = os.path.abspath(directory)
		abs_target = os.path.abspath(target)
	
		prefix = os.path.commonprefix([abs_directory, abs_target])
		
		return prefix == abs_directory
	
	def safe_extract(tar, path=".", members=None, *, numeric_owner=False):
	
		for member in tar.getmembers():
			member_path = os.path.join(path, member.name)
			if not is_within_directory(path, member_path):
				raise Exception("Attempted Path Traversal in Tar File")
	
		tar.extractall(path, members, numeric_owner=numeric_owner) 
		
	
	safe_extract(tar, dir)
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