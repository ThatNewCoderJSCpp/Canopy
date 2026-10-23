#!/usr/bin/env bash
set -euo pipefail

abis="${1:-arm64-v8a,x86_64}"
out_dir="${2:-.}"
ndk="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-}}"
if [ -z "$ndk" ] || [ ! -f "$ndk/build/cmake/android.toolchain.cmake" ]; then
    echo "::error::set ANDROID_NDK_HOME to the Android NDK"
    exit 1
fi

root=$(pwd)
libs="$root/build-android-libs"
jobs="${JOBS:-4}"

cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DFIZMO_TUNE=OFF \
    -DCANOPY_WITH_WALNUT=OFF -DCANOPY_BUILD_TESTS=OFF
cmake --build build-host --target grove -j "$jobs"
grove="$root/build-host/bin/grove"

for abi in ${abis//,/ }; do
    cmake -S . -B "build-android-$abi" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
        -DFIZMO_TUNE=OFF -DFIZMO_BUILD_SHARED=OFF -DFIZMO_BUILD_SHADER_TOOL=OFF \
        -DCANOPY_GITHUB_CLIENT_ID="${CANOPY_GITHUB_CLIENT_ID:-}" \
        -DCANOPY_ANDROID_LIBS_DIR="$libs"
    cmake --build "build-android-$abi" --target canopy_android -j "$jobs"
    test -f "$libs/$abi/libcanopy_android.so"
done

version=$(sed -n 's/^project(canopy VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n 1)
IFS=. read -r major minor patch <<< "$version"
code=$(( ${major:-0} * 1000000 + ${minor:-0} * 1000 + ${patch:-0} ))

sign=()
if [ -n "${CANOPY_KEYSTORE_BASE64:-}" ]; then
    printf '%s' "$CANOPY_KEYSTORE_BASE64" | base64 -d > "$RUNNER_TEMP/canopy.keystore"
    sign=(--keystore "$RUNNER_TEMP/canopy.keystore" --ks-pass env:CANOPY_KEYSTORE_PASS)
    if [ -n "${CANOPY_KEY_ALIAS:-}" ]; then sign+=(--key-alias "$CANOPY_KEY_ALIAS"); fi
else
    echo "::warning::CANOPY_KEYSTORE_BASE64 isn't set, so the APK is signed with a new debug key; phones that have an older Canopy APK will need to uninstall it first"
fi

mkdir -p "$out_dir"
apk="$out_dir/Canopy-$version-android.apk"
"$grove" build UI/canopy.grove --target android \
    --runtime-lib "$libs" --lib-name canopy_android --abi "$abis" \
    --set package=io.github.thatnewcoderjscpp.canopy \
    --set label=Canopy \
    --set storage=all \
    --set "version=$version" \
    --set "version-code=$code" \
    --set icon=../Packaging/icons/canopy-512.png \
    "${sign[@]}" \
    -o "$apk"
ls -la "$apk"
