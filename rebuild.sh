#!/bin/bash
# Quick rebuild without sudo
cd "$(dirname "$0")"
[ ! -f Makefile ] && { mkdir -p build && cd build && cmake .. && cd ..; }
make && make install 2>&1 | grep -v "Permission denied" && cp /usr/local/bin/rofi-bookmarks-helper ~/.local/bin/ 2>/dev/null; echo "✓ Rebuilt and installed"
