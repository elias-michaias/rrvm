#!/bin/sh
# Build script for RRVM
# Compiles the runtime and the new frontend sources (lexer + parser).
set -e

# Ensure we operate from repo root (script lives in rrvm/)
cd "$(dirname "$0")" || exit 1

# Choose compiler: prefer clang, fall back to gcc
if command -v clang >/dev/null 2>&1; then
  CC=clang
elif command -v gcc >/dev/null 2>&1; then
  CC=gcc
else
  echo "No C compiler (clang or gcc) found in PATH" >&2
  exit 1
fi

# Common flags
CFLAGS="-std=c11 -O2 -Wall -Wextra -I."

# Ensure output dir exists
mkdir -p ./bin
echo "Building rrvm with $CC $CFLAGS"

# Compile sources. The project uses many headers included by main.c, and the
# frontend C files are now compiled in as separate translation units.
$CC $CFLAGS -o ./bin/out main.c frontend/lexer/lexer.c frontend/parser/parser.c

if [ $? -eq 0 ]; then
  echo "Build succeeded: ./bin/out"
else
  echo "Build failed" >&2
  exit 1
fi
