#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
export DESTDIR="$stage/root" OUT_DIR="$stage/artifacts" PREFIX=/usr/local
mkdir -p "$OUT_DIR" "$DESTDIR/etc/systemd/system"
printf 'test bootstrap' > "$OUT_DIR/bootstrap.so"
printf 'test native' > "$OUT_DIR/native.so"
printf 'preserve me' > "$DESTDIR/etc/systemd/system/steamlink.service"
"$root/scripts/install" install
cmp "$OUT_DIR/native.so" "$DESTDIR/usr/local/lib/steamlink-tool/native.so"
test -L "$DESTDIR/usr/local/bin/steamlink-tool"
test "$(cat "$DESTDIR/etc/systemd/system/steamlink.service")" = 'preserve me'
printf 'unrelated' > "$DESTDIR/usr/local/lib/steamlink-tool/unrelated"
"$root/scripts/install" uninstall
test ! -e "$DESTDIR/usr/local/lib/steamlink-tool/native.so"
test -f "$DESTDIR/usr/local/lib/steamlink-tool/unrelated"
test -f "$DESTDIR/etc/systemd/system/steamlink.service"
rm -r "$DESTDIR/usr/local/lib/steamlink-tool"
rm "$OUT_DIR/native.so"
if "$root/scripts/install" install >/dev/null 2>&1; then echo 'Missing artifact unexpectedly installed' >&2; exit 1; fi
test ! -e "$DESTDIR/usr/local/lib/steamlink-tool/bootstrap.so"
printf 'fake' > "$OUT_DIR/native.so"
mkdir -p "$stage/escape" "$stage/link-root"
ln -s "$stage/escape" "$stage/link-root/usr"
if DESTDIR="$stage/link-root" "$root/scripts/install" install >/dev/null 2>&1; then echo 'Staged symlink escaped' >&2; exit 1; fi
test ! -e "$stage/escape/local/lib/steamlink-tool/native.so"
mkdir "$stage/launcher" "$stage/runtime"
cp "$root/scripts/launch" "$stage/launcher/launch"
for command in /bin/true /bin/false; do
    expected=0; test "$command" != /bin/false || expected=1
    result=0
    TMPDIR="$stage/runtime" STEAMLINK_RUNTIME_DIR= STEAMLINK_COMMAND="$command" "$stage/launcher/launch" || result=$?
    test "$result" = "$expected"
    test -z "$(ls -A "$stage/runtime")"
done
echo 'PASS shell installation, unrelated-file preservation, staging containment and launcher cleanup'
