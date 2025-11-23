#!/bin/sh
# Build script for RRVM
# Compiles the runtime and the new frontend sources (lexer + parser).
# Additionally attempts to compile the GNU Prolog optimizer into a single
# native executable embedding all Prolog sources.
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

# Compile C runtime/CLI.
$CC $CFLAGS -o ./bin/rrvm frontend/main.c frontend/lexer/lexer.c frontend/parser/parser.c

if [ $? -eq 0 ]; then
  echo "C build succeeded: ./bin/rrvm"
else
  echo "C build failed" >&2
  exit 1
fi

# -----------------------------------------------------------------------
# Attempt to build a single static/native GNU Prolog executable that embeds
# the optimizer (opt/main.pl and opt/pass/*.pl). This produces ./bin/rrvm-opt.
# If gplc is not available we skip Prolog compilation; if gplc fails we
# attempt a conservative fallback (stub).
# -----------------------------------------------------------------------
echo "=========================="

if command -v gplc >/dev/null 2>&1; then
  echo "gplc found: attempting to compile Prolog optimizer into ./bin/rrvm-opt"

  # Some build environments require RPM_ARCH and RPM_PACKAGE_RELEASE to be defined
  # for the underlying gcc call invoked by gplc. Ensure they are set to reasonable defaults.
  # Some build environments require RPM_ARCH, RPM_PACKAGE_RELEASE and
  # RPM_PACKAGE_VERSION to be defined for the underlying gcc call invoked by
  # gplc. Ensure they are set to reasonable defaults.
  if [ -z "$RPM_ARCH" ]; then
    RPM_ARCH=$(uname -m)
    export RPM_ARCH
  fi
  if [ -z "$RPM_PACKAGE_NAME" ]; then
    RPM_PACKAGE_NAME=rrvm
    export RPM_PACKAGE_NAME
  fi
  if [ -z "$RPM_PACKAGE_RELEASE" ]; then
    RPM_PACKAGE_RELEASE=1
    export RPM_PACKAGE_RELEASE
  fi
  if [ -z "$RPM_PACKAGE_VERSION" ]; then
    RPM_PACKAGE_VERSION=1
    export RPM_PACKAGE_VERSION
  fi

  # Files to compile into the native executable (explicit list keeps build
  # deterministic). If you add new passes, include them here or update this
  # list to use a glob (some gplc versions handle globs via the shell).
  PROLOG_SRCS="backend/main.pl backend/opt/common.pl backend/opt/const_fold.pl backend/opt/identity.pl"

  # Try to produce a minimal stripped executable. If gplc errors out we
  # try a less aggressive invocation as a fallback.
  gplc --min-size -s -o ./bin/rrvm-opt $PROLOG_SRCS 2>&1
  if [ $? -eq 0 ]; then
    echo "Prolog optimizer compiled successfully: ./bin/rrvm-opt"
  else
    echo "gplc compilation failed; attempting fallback (unstripped, simpler flags)" >&2
    gplc -o ./bin/rrvm-opt $PROLOG_SRCS 2>&1
    if [ $? -eq 0 ]; then
      echo "Prolog optimizer compiled (fallback): ./bin/rrvm-opt"
    else
      echo "Prolog compilation ultimately failed; producing no rrvm-opt binary" >&2
      # Optionally produce a small stub that consults sources at runtime
      echo "Attempting to produce a small stub that consults sources at runtime..."
      gplc -o ./bin/rrvm-opt-stub opt/cmd.pl 2>/dev/null || true
      if [ -f ./bin/rrvm-opt-stub ]; then
        echo "Created stub: ./bin/rrvm-opt-stub (will consult .pl files at runtime)"
      else
        echo "Stub creation failed; please ensure gplc and gcc are functional in your environment" >&2
      fi
    fi
  fi
else
  echo "gplc (GNU Prolog compiler) not found in PATH; skipping Prolog compilation"
  echo "You can install GNU Prolog and re-run this script to build the standalone optimizer."
fi

echo "Overall build finished."
