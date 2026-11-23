"""Extract 7z file using Python's lzma or py7zr if available."""
import subprocess
import sys
import os

# Try using py7zr
try:
    import py7zr
    print("Using py7zr to extract...")
    with py7zr.SevenZipFile('d:/mingw64_new.7z', mode='r') as z:
        z.extractall(path='d:/')
    print("Extraction complete!")
except ImportError:
    # Fall back to subprocess with PowerShell Expand-Archive (won't work for 7z)
    # Try downloading py7zr first
    print("py7zr not found, installing...")
    subprocess.run([sys.executable, '-m', 'pip', 'install', 'py7zr'], check=True)
    import py7zr
    print("Extracting with py7zr...")
    with py7zr.SevenZipFile('d:/mingw64_new.7z', mode='r') as z:
        z.extractall(path='d:/')
    print("Done!")
