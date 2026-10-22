#!/bin/sh
# Build SnipFix.exe with LLVM-MinGW clang or MinGW-w64 GCC, without the C runtime.
#   CC=x86_64-w64-mingw32-clang ./build.sh   -> build/SnipFix.exe
#   ./build.sh test                          -> run the unit tests with the host compiler
set -eu
cd "$(dirname "$0")"
mkdir -p build

if [ "${1:-}" = "test" ]; then
    ${HOSTCC:-cc} -std=c99 -Wall -Wextra -Werror -Isrc tests/test_logic.c src/logic.c -o build/test_logic
    ./build/test_logic
    exit
fi

CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-$(echo "$CC" | sed 's/-\(gcc\|clang\)$/-windres/')}

$WINDRES -O coff -I src src/snipfix.rc -o build/snipfix.res.o

# -nostdlib + own entry point: only system DLLs are imported (kernel32, user32, ...)
$CC -std=c99 -Oz -s -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -fno-stack-protector -fno-builtin -mno-stack-arg-probe -fno-ident \
    -municode -mwindows -nostdlib -Wl,-e,start -Wl,--gc-sections -Wl,--no-insert-timestamp \
    -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va \
    src/main.c src/logic.c src/crt.c build/snipfix.res.o \
    -lkernel32 -luser32 -lshell32 -lgdi32 -ladvapi32 \
    -o build/SnipFix.exe

ls -l build/SnipFix.exe
