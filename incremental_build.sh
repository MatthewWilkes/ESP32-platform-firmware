#!/bin/bash
git submodule update --init --recursive
cd firmware
export PATH="$PATH:$(pwd)/xtensa-esp32s3-elf/bin"
bash mpy_cross.sh
make -j8
