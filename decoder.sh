#!/bin/bash

# backtrace deocder

for addr in "$1"; do
    xtensa-esp32-elf-addr2line -e build/*.elf ${addr%:*}
done
