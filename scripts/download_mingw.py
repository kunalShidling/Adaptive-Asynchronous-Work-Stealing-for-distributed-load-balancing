import urllib.request
import sys

url = "https://github.com/niXman/mingw-builds-binaries/releases/download/13.2.0-rt_v11-rev1/x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev1.7z"
out = "d:/mingw64_new.7z"
print(f"Downloading {url}")
print(f"To: {out}")
urllib.request.urlretrieve(url, out)
print("Download complete")
