# Maintainer: Winicius Cota <winiciuscota@github>
pkgname=rofi-bookmarks
pkgver=1.0.0
pkgrel=3
pkgdesc="Firefox local storage bookmark browser for rofi"
arch=('any')
url="https://github.com/winiciuscota/rofi-bookmarks"
license=('MIT')
depends=('rofi' 'xdg-utils' 'bash' 'python' 'libnotify' 'firefox')
makedepends=('git')
source=("rofi-bookmarks-src::git+https://github.com/winiciuscota/rofi-bookmarks.git#tag=v${pkgver}")
sha256sums=('SKIP')

build() {
    : # pure bash — nothing to compile
}

package() {
    cd "$srcdir/rofi-bookmarks-src"

    # Install main script
    install -Dm755 rofi-bookmarks         "$pkgdir/usr/local/bin/rofi-bookmarks"
    install -Dm755 rofi-bookmarks-launcher "$pkgdir/usr/local/bin/rofi-bookmarks-launcher"

    # Install docs
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 LICENSE   "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
