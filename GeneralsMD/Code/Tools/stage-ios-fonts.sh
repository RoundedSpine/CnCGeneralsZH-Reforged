#!/usr/bin/env bash
# Liberation 2.1.5, SIL OFL. These names match glyphrasteriser's Apple substitutions.
set -euo pipefail
out="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
# Preserve the pinned release bytes and checksum across independent hosts.
# GitHub attachment downloads occasionally return HTTP 504 on macOS Actions.
# MacPorts and OmniOS publish mirrors of this exact release, not replacement fonts.
expected=7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0
archive="$work/fonts.tar.gz"
found=0
for url in \
  https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz \
  https://distfiles.macports.org/liberation-fonts/liberation-fonts-ttf-2.1.5.tar.gz \
  https://mirrors.omnios.org/liberation-fonts/liberation-fonts-ttf-2.1.5.tar.gz \
  https://github.com/liberationfonts/liberation-fonts/releases/download/2.1.5/liberation-fonts-ttf-2.1.5.tar.gz; do
  rm -f "$archive"
  if curl --fail --location --retry 2 --retry-delay 2 --connect-timeout 20 \
      --max-time 90 "$url" -o "$archive" && \
      printf '%s  %s\n' "$expected" "$archive" | shasum -a 256 -c -; then
    found=1
    break
  fi
  echo "[ios-fonts] download failed or checksum mismatch: $url" >&2
done
if [ "$found" != 1 ]; then
  echo '[ios-fonts] No verified Liberation Fonts 2.1.5 archive available' >&2
  exit 1
fi
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
