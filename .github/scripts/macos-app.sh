#!/usr/bin/env bash
set -euo pipefail

package="$1"
source_dir="$2"
version="$3"
out="$4"
platform="$5"

package="$(cd "$package" && pwd)"
source_dir="$(cd "$source_dir" && pwd)"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

if [ ! -x "$package/bin/canopy" ]; then
    echo "$package/bin/canopy is missing; pass the folder from the extracted Canopy archive" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
app="$work/root/Canopy.app"
contents="$app/Contents"
mkdir -p "$contents/MacOS" "$contents/Resources"

cp -R "$package/bin/." "$contents/MacOS/"
for part in share include runtime fizmo toolchain docs; do
    if [ -d "$package/$part" ]; then cp -R "$package/$part" "$contents/"; fi
done

cat > "$contents/MacOS/Canopy" <<'EOF'
#!/bin/sh
here="$(cd "$(dirname "$0")" && pwd)"
export PATH="$here:$PATH"
exec "$here/canopy" --wait "$@"
EOF
chmod +x "$contents/MacOS/Canopy"

iconset="$work/canopy.iconset"
mkdir -p "$iconset"
icons="$source_dir/Packaging/icons"
for size in 16 32 128 256 512; do
    if [ -f "$icons/canopy-$size.png" ]; then cp "$icons/canopy-$size.png" "$iconset/icon_${size}x${size}.png"; fi
    double=$((size * 2))
    if [ -f "$icons/canopy-$double.png" ]; then cp "$icons/canopy-$double.png" "$iconset/icon_${size}x${size}@2x.png"; fi
done
if ! iconutil -c icns "$iconset" -o "$contents/Resources/canopy.icns"; then
    echo "::warning::couldn't make canopy.icns, so Canopy.app has no icon"
fi

sed -e "s/@CANOPY_VERSION@/$version/g" "$source_dir/Packaging/macos/Info.plist.in" > "$contents/Info.plist"
printf 'APPL????' > "$contents/PkgInfo"
plutil -lint "$contents/Info.plist"

if ! codesign --force --sign - "$app" 2> "$work/codesign.txt"; then
    echo "::warning::couldn't ad-hoc sign Canopy.app: $(cat "$work/codesign.txt")"
fi

ln -s /Applications "$work/root/Applications"
dmg="$out/Canopy-$version-$platform.dmg"
rm -f "$dmg"
hdiutil create -volname "Canopy $version" -srcfolder "$work/root" -fs HFS+ -format UDZO -ov "$dmg"
hdiutil verify "$dmg"

rm -f "$work/root/Applications"
(cd "$work/root" && ditto -c -k --sequesterRsrc --keepParent Canopy.app "$out/Canopy-$version-$platform-app.zip")

ls -la "$dmg" "$out/Canopy-$version-$platform-app.zip"
