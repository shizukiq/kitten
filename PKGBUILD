pkgname=kitten
pkgver=0.1.0
pkgrel=1
pkgdesc="Kitten application"
arch=('x86_64')
url="https://github.com/shizukiq/kitten"
license=('BSD-2-Clause')
depends=()
makedepends=('make' 'gcc')

source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname-$pkgver"
    make
}

package() {
    cd "$srcdir/$pkgname-$pkgver"

    make PREFIX=/usr DESTDIR="$pkgdir" install
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
