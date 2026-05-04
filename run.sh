#!/bin/bash
ISO="build/06_OSDev_06/kernel.iso"

if [ ! -f "$ISO" ]; then
    echo "kernel.iso not found, building..."
    cmake -S src/06_OSDev_06 -B build/06_OSDev_06 --toolchain toolchain-i686.cmake
    cmake --build build/06_OSDev_06 --target uiaos-create-image
fi

if [[ "$(uname)" == "Darwin" ]]; then
    qemu-system-i386 -boot d -cdrom "$ISO" -m 64 \
        -audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
        -display cocoa,zoom-to-fit=on
else
    qemu-system-i386 -boot d -cdrom "$ISO" -m 64 \
        -audiodev sdl,id=audio0,out.buffer-length=40000 -machine pcspk-audiodev=audio0
fi
