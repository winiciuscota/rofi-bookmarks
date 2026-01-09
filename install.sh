#!/bin/bash

cd "$(dirname "$0")"

# Build
if [ -d build ]; then
    cd build
else
    mkdir -p build
    cd build
    cmake ..
fi
make

# Install
sudo make install

echo ""
echo "Installation complete!"
echo "Run with: rofi -show bookmarks -modi bookmarks"
