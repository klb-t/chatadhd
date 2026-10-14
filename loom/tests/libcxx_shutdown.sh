#!/bin/sh
# Reproduces the Android (libc++) shutdown crash on a desktop: builds libloom with clang + libc++ and runs a C-ABI
# init/subscribe/shutdown/init cycle (tests/shutdown_cabi_probe.c). Before the ~Runtime fix it dies with SIGSEGV
# (unique_ptr<EventBus>::operator* this=0x148 in ActionRunner::~ActionRunner). Needs clang-18 and libc++-18-dev.
# usage: tests/libcxx_shutdown.sh [build-dir]   (run from loom/)
set -e
B=${1:-build/libcxx}
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++ \
  -DLOOM_SHARED=ON -DLOOM_BUILD_TESTS=OFF -DLOOM_BUILD_CLI=OFF -DLOOM_BUILD_SERVER=OFF
ninja -C "$B" loom
D=$(mktemp -d)
clang-18 -g -Iinclude tests/shutdown_cabi_probe.c -L"$B" -lloom -Wl,-rpath,"$PWD/$B" -o "$D/probe"
"$D/probe" "$D/data" 2>&1 | grep -c "shutdown ok" | grep -qx 2
echo "libc++ shutdown cycle OK"
