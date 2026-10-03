# Vendored third-party code

Everything here is copied verbatim from upstream (no local edits) so Loom builds
offline and identically on every platform. Update by re-downloading from the
listed source and refreshing the checksum.

| Directory | Library | Version | License | Source |
|---|---|---|---|---|
| `nlohmann/` | nlohmann/json (single header) | 3.11.3 | MIT (`LICENSE.MIT`) | https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp |
| `cpp-httplib/` | cpp-httplib | 0.18.3 | MIT (`LICENSE`) | https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h |
| `doctest/` | doctest | 2.4.11 | MIT (`LICENSE.txt`) | https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h |
| `miniz/` | miniz (amalgamated `miniz.c`/`miniz.h`) | 3.0.2 | MIT (`LICENSE`) | https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip |
| `sqlite/` | SQLite amalgamation | 3.47.2 | Public domain (`LICENSE.md`) | `package/deps/sqlite3/` inside https://registry.npmjs.org/better-sqlite3/-/better-sqlite3-11.7.0.tgz |

SHA-256 of the main files:

```
06c3bf23ecb30c91587e0f96d2fea71f3e5acc8305cda0bb8d23ee29d522745f  sqlite/sqlite3.c
9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6  nlohmann/nlohmann/json.hpp
a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5  cpp-httplib/httplib.h
44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb  doctest/doctest/doctest.h
0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740  miniz/miniz.c
295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b  miniz/miniz.h
```

Notes:

- **SQLite**: sqlite.org is not reachable from the build environment, so we
  take the amalgamation that better-sqlite3 ships. better-sqlite3 applies one
  patch (`deps/patches/1208.patch`), which folds two constant expressions
  (`30.0*86400.0` becomes `2592000.0`) to work around an MSVC issue. It does not
  change behavior. Loom compiles the amalgamation as its own C translation unit
  with FTS5 and JSON enabled (see `loom/CMakeLists.txt`). It is used when
  `LOOM_USE_SYSTEM_SQLITE=OFF` (Android, the `vendored`/`tsan` presets, or any
  platform without a system libsqlite3).
- **cpp-httplib** is only compiled into the default desktop HTTP transport.
  Platforms that inject their own transport (Android via JNI) don't need it.
- **miniz** is used by the importer for ZIP archives.
