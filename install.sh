#!/bin/bash

set -e

cd "$(dirname "$0")"

echo "Building rofi-bookmarks..."

# Build
if [ -d build ]; then
    cd build
    cmake .. >/dev/null
else
    mkdir -p build
    cd build
    cmake .. >/dev/null
fi
make

# Install to user local directory
PLUGIN_DIR="$HOME/.local/share/rofi/plugins"
mkdir -p "$PLUGIN_DIR"

echo "Installing plugin to: $PLUGIN_DIR"
cp bookmarks.so "$PLUGIN_DIR/"

# Make sure wrapper script exists
BIN_DIR="$HOME/.local/bin"
mkdir -p "$BIN_DIR"

echo "Installation complete!"
echo ""
echo "To use, run: rofi -show bookmarks -plugin-path ~/.local/share/rofi/plugins"
echo ""
echo "Or add to your rofi config (~/.config/rofi/config.rasi):"
echo "  configuration {"
echo "    modi: \"window,run,drun,bookmarks\";"
echo "    plugins-directory: \"~/.local/share/rofi/plugins\";"
echo "  }"

