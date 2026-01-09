# Maintainer: Your Name <your.email@example.com>
pkgname=rofi-bookmarks
pkgver=1.0.0
pkgrel=1
pkgdesc="Browser bookmarks plugin for rofi"
arch=('x86_64')
url="https://github.com/yourusername/rofi-bookmarks"
license=('MIT')
depends=('rofi' 'redis' 'xdg-utils')
makedepends=('cmake' 'gcc')
source=()
md5sums=()

build() {
    cd "$startdir"
    mkdir -p build
    cd build
    cmake ..
    make
}

package() {
    cd "$startdir/build"
    
    # Install the plugin
    install -Dm755 bookmarks.so "$pkgdir/usr/lib/rofi/bookmarks.so"
    
    # Install the helper script
    install -Dm755 "$startdir/rofi-bookmarks-helper" "$pkgdir/usr/local/bin/rofi-bookmarks-helper"
    
    # Install launcher if it exists
    if [ -f "$startdir/rofi-bookmarks-launcher" ]; then
        install -Dm755 "$startdir/rofi-bookmarks-launcher" "$pkgdir/usr/local/bin/rofi-bookmarks-launcher"
    fi
    
    # Install README if it exists
    if [ -f "$startdir/README.md" ]; then
        install -Dm644 "$startdir/README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
    fi
}
