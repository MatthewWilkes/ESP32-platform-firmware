#!/usr/bin/env bash
export PATH="$PATH:$(pwd)/xtensa-esp32s3-elf/bin"
cd firmware
make flash
