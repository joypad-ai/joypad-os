#!/bin/sh
# Build NUSProbe.app — minimal ad-hoc signed CoreBluetooth shell.
# macOS hides OS-paired BLE HID devices from unsigned processes; an ad-hoc
# signed app bundle is the smallest identity that passes the gate.
set -e
cd "$(dirname "$0")"
APP=NUSProbe.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp Info.plist "$APP/Contents/"
xcrun swiftc -O main.swift -o "$APP/Contents/MacOS/NUSProbe" \
    -framework CoreBluetooth -framework Foundation
codesign -s - --force "$APP"
echo "built $(pwd)/$APP"
