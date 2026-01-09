# Maintainer: Winicius Cota <winiciuscota@github>
pkgname=rofi-bookmarks
pkgver=1.0.0
pkgrel=1
pkgdesc="Encrypted bookmarks manager plugin for rofi"
arch=('x86_64')
url="https://github.com/winiciuscota/rofi-bookmarks"
license=('MIT')
depends=('rofi' 'rbw' 'gnupg' 'xdg-utils' 'bash' 'coreutils')
makedepends=('cmake' 'gcc' 'git')
source=("${pkgname}::git+https://github.com/winiciuscota/rofi-bookmarks.git#tag=v${pkgver}")
sha256sums=('SKIP')

build() {
    cd "$pkgname"
    mkdir -p build
    cd build
    cmake ..
    make
}

package() {
    cd "$pkgname/build"
    
    # Install the plugin
    install -Dm755 bookmarks.so "$pkgdir/usr/lib/rofi/bookmarks.so"
    
    # Install scripts
    install -Dm755 "$srcdir/$pkgname/rofi-bookmarks-helper" "$pkgdir/usr/local/bin/rofi-bookmarks-helper"
    install -Dm755 "$srcdir/$pkgname/rofi-bookmarks-launcher" "$pkgdir/usr/local/bin/rofi-bookmarks-launcher"
    
    # Install docs
    install -Dm644 "$srcdir/$pkgname/README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 "$srcdir/$pkgname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
