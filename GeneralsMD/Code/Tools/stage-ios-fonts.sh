#!/usr/bin/env bash
# Liberation 2.1.5, SIL OFL. These names match glyphrasteriser's Apple substitutions.
set -euo pipefail
out="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
url=https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz
curl --fail --location --retry 3 "$url" -o "$work/fonts.tar.gz"
echo "7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0  $work/fonts.tar.gz" | shasum -a 256 -c -
tar -xzf "$work/fonts.tar.gz" -C "$work"
source="$work/liberation-fonts-ttf-2.1.5"
cp "$source/LiberationSans-Regular.ttf" "$out/Arial.ttf"
cp "$source/LiberationSans-Bold.ttf" "$out/Arial Bold.ttf"
cp "$source/LiberationSans-Regular.ttf" "$out/Arial Unicode.ttf"
cp "$source/LiberationMono-Regular.ttf" "$out/Courier New.ttf"
cp "$source/LiberationMono-Bold.ttf" "$out/Courier New Bold.ttf"
cp "$source/LiberationSerif-Regular.ttf" "$out/Times New Roman.ttf"
cp "$source/LiberationSerif-Bold.ttf" "$out/Times New Roman Bold.ttf"
cp "$source/LICENSE" "$out/Liberation-LICENSE.txt"
