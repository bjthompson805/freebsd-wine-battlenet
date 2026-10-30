#!/bin/sh
# Build emulators/wine-devel from the FreeBSD ports tree with the patches in
# patches/, as an unprivileged user, and copy the result to $DEST.
#
#   WORK  build directory (ports checkout, sources, objects)  default: ~/src/freebsd-wine-battlenet
#   DEST  where the patched Wine is installed                  default: ~/wine-wc
set -eu

here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$HOME/src/freebsd-wine-battlenet}
dest=${DEST:-$HOME/wine-wc}
ports=$work/ports
port=$ports/emulators/wine-devel
mk="make -C $port WRKDIRPREFIX=$work/wrk DISTDIR=$work/distfiles BATCH=yes NO_DEPENDS=yes"

if [ ! -d "$ports" ]; then
	git clone --depth 1 --filter=blob:none --sparse https://git.FreeBSD.org/ports.git "$ports"
	git -C "$ports" sparse-checkout set Mk Templates Keywords emulators/wine-devel
fi

cp "$here"/patches/patch-* "$port/files/"
$mk clean
$mk MAKE_JOBS_NUMBER="$(sysctl -n hw.ncpu)" stage

rm -rf "$dest"
cp -a "$($mk -V STAGEDIR)/usr/local" "$dest"
"$dest/bin/wine" --version
