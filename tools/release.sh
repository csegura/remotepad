#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION="${1:-$(date +%Y%m%d)}"
BINARY="$ROOT_DIR/build/remotepad"
RELEASE_DIR="$ROOT_DIR/release"
ARCHIVE="$RELEASE_DIR/remotepad-${VERSION}-linux-x64.zip"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found at $BINARY"
    echo "Build first: make"
    exit 1
fi

rm -rf "$RELEASE_DIR/staging"
mkdir -p "$RELEASE_DIR/staging"

cp "$BINARY" "$RELEASE_DIR/staging/"
cp "$ROOT_DIR/remotepad.sh" "$RELEASE_DIR/staging/"
cp "$ROOT_DIR/LICENSE" "$RELEASE_DIR/staging/"
cp "$ROOT_DIR/readme.md" "$RELEASE_DIR/staging/"

mkdir -p "$RELEASE_DIR"
(cd "$RELEASE_DIR/staging" && zip -r "$ARCHIVE" .)
rm -rf "$RELEASE_DIR/staging"

echo "Release: $ARCHIVE"
