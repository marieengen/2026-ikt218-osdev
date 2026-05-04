#!/bin/bash
REPO="$(cd "$(dirname "$0")" && pwd)"
ISO="$REPO/build/06_OSDev_06/kernel.iso"
IMG="$REPO/build/06_OSDev_06/kernel.img"

if [ ! -f "$ISO" ]; then
    if [ -f "$IMG" ]; then
        ISO="$IMG"
    else
        echo "kernel.iso not found, building..."
        cmake -S "$REPO/src/06_OSDev_06" -B "$REPO/build/06_OSDev_06" --toolchain "$REPO/toolchain-i686.cmake" || { echo "Build failed."; exit 1; }
        cmake --build "$REPO/build/06_OSDev_06" --target uiaos-create-image || { echo "Build failed."; exit 1; }
    fi
fi

if [[ "$(uname)" == "Darwin" ]]; then
    qemu-system-i386 -boot d -cdrom "$ISO" -m 64 \
        -audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
        -display cocoa,zoom-to-fit=on
else
    qemu-system-i386 -boot d -cdrom "$ISO" -m 64 \
        -audiodev sdl,id=audio0,out.buffer-length=40000 -machine pcspk-audiodev=audio0
fi
