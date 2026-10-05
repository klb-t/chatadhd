# Integrator9: complete local runtime negatives, 2026-10-05

Source checkpoint `0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`; original5 `5f0abd20dd83e5c0e7f43e84c333d5e23a5e681f`, actual selected/rebased source in parent. These failures are not accepted into main.

Original logs: first5build failed on restored zero-byte objects; exact original compile_commands replay repaired db.cpp.o, packet.cpp.o and test_catalog.cpp.o without source changes. Subsequent fullbuild exit0; all247 objects ELF,9valid archives/binaries. Complete failed and final build logs and receipt preserved.

LocalLLVM18 extraction yielded a truncated library (38,522,880 vs123,215,144 bytes), causing SIGBUS; exact original package restored atomically. Standalone Clang18/SQLite passed after repair. ASan/UBSan compile/link passed, standalone runtime failed because LeakSanitizer could not access process task data in this host. No sanitizer option or test weakened. This is not a claimed full Clang/ASan application gate.

ZIP SHA256 `a554df658e8b8c88dfee462d2bb5f7ac9faa34e06d81dd3fd8b57c5f6d555fed`, 29 files. Full raw logs, commands, exact helper source and package/source hashes in ZIP. Intermediate empty object bytes are necessarily empty; compiled binaries/deb packages are reproducible from recorded source/package hashes and commands, not republished. No credential or private data is included. Earlier pre-cleanup negatives remain explicitly unavailable as described in the main report; this archive does not claim to recover them.

## Do wątku8

Use actual complete Clang/vendored gates; evaluate ASan on a host with supported proc access, preserve options and honest source bindings.
