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

## Post-quantum / password-hash / curve25519 (T5b, 2026-10-12)

Copied verbatim from `git clone --depth 1 --branch <tag>`; no scripts from the clones were run; only the portable C sources below were copied (no assembly or architecture backends; `native/` backend directories and `*.S` were left out). Tests/proofs/examples of the upstream repositories are not included. Built by the `loom_pqc` target in `loom/CMakeLists.txt` with warnings relaxed for that target only. `src/crypto/vendor/pq_glue.c` is Loom's own glue.

| Directory | Library | Tag | Commit | Licence (from the copied LICENSE file) | Upstream |
|---|---|---|---|---|---|
| `mlkem-native/` | mlkem-native (ML-KEM, FIPS 203), `MLK_CONFIG_PARAMETER_SET=768` | v2.0.0 | d1b2fe782888bdb761a50336012923180be7f502 | Apache-2.0 OR ISC OR MIT (`LICENSE`: all sources under mlkem/*) | https://github.com/pq-code-package/mlkem-native |
| `mldsa-native/` | mldsa-native (ML-DSA, FIPS 204), `MLD_CONFIG_PARAMETER_SET=65` | v2.0.0 | 834a90d5e846ffa1e1611bd24e160bb2e9b86d35 | Apache-2.0 OR ISC OR MIT (`LICENSE`) | https://github.com/pq-code-package/mldsa-native |
| `argon2/` | phc-winner-argon2 (Argon2, RFC 9106), reference `ref.c` backend, built with `ARGON2_NO_THREADS` | 20190702 | 62358ba2123abd17fccf2a108a301d4b52c01a7c | CC0-1.0 OR Apache-2.0 (`LICENSE`) | https://github.com/P-H-C/phc-winner-argon2 |
| `monocypher/` | Monocypher (X25519, Ed25519 + SHA-512 from `monocypher-ed25519`) | 4.0.3 | ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f | BSD-2-Clause OR CC0-1.0 (`LICENCE.md`) | https://github.com/LoupVaillant/Monocypher |

Known-answer tests: `loom/tests/test_crypto_pq.cpp` with fixtures in `loom/tests/fixtures/crypto/` (NIST ACVP ML-KEM/ML-DSA subsets, X-Wing draft vectors; RFC 7748/8032/9106/5869 vectors inline).

File lists and SHA-256 (sha256sum of each copied file):

### `mlkem-native/`
```
1c730e3c2cd4f70e058519ef3e910d8bdd4ed822ae2ce689f3c63d32fc52314b  mlkem-native/LICENSE
561c05fceefb56416fa1360adcaa71a1e9697ab9d0a4bb1cc00eaedcea07371a  mlkem-native/mlkem/README.md
e533497ae0c31d5023d00cdb25ae5f8646727eee53323fba4107818231ad8956  mlkem-native/mlkem/mlkem_native.c
3546f0f7318fc08d007097add98a77d48a8c1f670e0d8db2c5222e1acaa88ce1  mlkem-native/mlkem/mlkem_native.h
710b09e8003d19d13167af89575499ca2790668e87b0dce7cdc6c590c32daafa  mlkem-native/mlkem/mlkem_native_config.h
180bf239c5d7b84fa451727546d4fbc9595008382cfc86d4ae5791e0e5a6b910  mlkem-native/mlkem/src/cbmc.h
833c4d8e52822d5a209fdc72ef655f6eb7801fd712c515805d2c5360e58bdaab  mlkem-native/mlkem/src/common.h
a13d44b3a96962ae982722fba8b1ff6744f16e7b3b760883edfe762061cb26b5  mlkem-native/mlkem/src/compress.c
746ce7e55d088f75220ca0a5b08fc515c033167cbcacfd74be127e14f1f5e128  mlkem-native/mlkem/src/compress.h
e6788b29f4883f57752fd8327a80dfc978eb21ad72a0055cbbaaa7b433637edf  mlkem-native/mlkem/src/context.h
263ac19f2b2bf5c784cbf04fd4c968762abedc6961d44f507c8953b52a38827e  mlkem-native/mlkem/src/debug.c
d1fab329b439fc6dd9c4234f3fe053e74b000d416e0f4e4464542813667f3750  mlkem-native/mlkem/src/debug.h
fb0654c0b33c45c929fdb2af2287807a7b8f312ac7f3baa3ddfaef0c030ef0b2  mlkem-native/mlkem/src/fips202/fips202.c
a5efcf58893aa589dfe25a1069e11ab58408b2ad14e389014a785205300468a1  mlkem-native/mlkem/src/fips202/fips202.h
2e481ccbafc00765b210725c8e454f2b4dc622844eccf6a0c1e025657fcb5fe8  mlkem-native/mlkem/src/fips202/fips202x4.c
c8d2e50f7ccce19822030786a1a99f0892db63ebe5b020fa91a5383c9700dd22  mlkem-native/mlkem/src/fips202/fips202x4.h
461c278b0abb9fde098ee6b34a47056445573cb7aa5c4362d59e35041d0997e5  mlkem-native/mlkem/src/fips202/keccakf1600.c
88c06d4c546f46920a65001bcfad8cabb6173fb01ae071e02412c46043bdf54f  mlkem-native/mlkem/src/fips202/keccakf1600.h
aae13759c11d342a2431f8b03f4d3026fb3e4b07357dc9589be60ddc222e5fac  mlkem-native/mlkem/src/fips202/native/api.h
590e5d36ce4e9d699501b458ea0653a365e12bbd9f63a6abc7bbdaab2d6b2cfd  mlkem-native/mlkem/src/fips202/native/auto.h
526505cbf09ab80e9644563eb808373c502aad2270a0b53c8370510ea4c0b70d  mlkem-native/mlkem/src/indcpa.c
ac6f47c959fdc97aa1882bca5f78ecfcee595ff21769f937866f2c1d1bf29109  mlkem-native/mlkem/src/indcpa.h
3922156826521429eb250ccb3d54d7beacce14adce26bb3be6d1ef7864342dd3  mlkem-native/mlkem/src/kem.c
ba22bd6de66141138329aaf53aedc73350c4ceaa241a341d0e5136c5df493dbd  mlkem-native/mlkem/src/kem.h
320b8cb7fd40caa77294a6bca54421b103c9c058df4ef8d0d88610ee978c7781  mlkem-native/mlkem/src/native/api.h
c0b008cc2e0031dfd734b6c00f32a7c3c66eb8135e3545c9589c62454ad722d7  mlkem-native/mlkem/src/native/meta.h
450fe3e0e50496921920473ae4321660f178c23d51f1453f3c537ee63c4158cb  mlkem-native/mlkem/src/params.h
f0e0643a2c3743181b7927a9d47af0930ce7870a9d79671f64e6089fe028982e  mlkem-native/mlkem/src/poly.c
86224c6d1e7ec5a4c0dcd829ae90c0cb43b4855d84dd719973d47e71b8e962d4  mlkem-native/mlkem/src/poly.h
56130249963b5b62e97dc6413adeed21f4e542da6531e34cf640baca161fa5ff  mlkem-native/mlkem/src/poly_k.c
839852fd9c8d155f37a7a07dc1116c2ea18fade754b1d882f73c4affef5e62d3  mlkem-native/mlkem/src/poly_k.h
137eb6e5854b8e58c2caec6eea86530c8e7495706f6f5b3e007e25a9875e164f  mlkem-native/mlkem/src/randombytes.h
9263037c401d0cc4130a6f0dba76299d170b498c330f5d5c32b5ae5921bacd5f  mlkem-native/mlkem/src/sampling.c
d25df90afc96a02a794dcb737236b2cb42cbd2db411ccaf274cf5ea3bf5e3c87  mlkem-native/mlkem/src/sampling.h
168d0ec3bf50bd45cb0a103d7984e9ccf52f78697718b7e85efdf1beda7abc95  mlkem-native/mlkem/src/symmetric.h
0ead27bb8c8879b62b29b9bb86c36aeaac270598d42ee2f2258a478eed8efa5c  mlkem-native/mlkem/src/sys.h
f8f89f1b8c8e5811ce134c0d90fb7828027336afeed231dc3ccb7ee6b73b9335  mlkem-native/mlkem/src/verify.c
76fd0af32577f9118d1ea90a66ff77e18cdf99d08dcf1c9df137833041e8492f  mlkem-native/mlkem/src/verify.h
303d1abccc4757d49a5c8df5f88080fefef617b7360056db5311bd94354096d4  mlkem-native/mlkem/src/zetas.inc
```

### `mldsa-native/`
```
22f7173776d0a36d3f13ec1f45d51e0502e67d17e384bddf39690d833974ff93  mldsa-native/LICENSE
825bacae1b60e7d0bb91d4a514b33f4ea1d7e70724d6e42742fb987e27efd0ef  mldsa-native/mldsa/README.md
0d7e19d651e629430b1923f3cba2fb2579639d8bd1f156a2dcd2696aa83f846f  mldsa-native/mldsa/mldsa_native.c
b30c04756599aaa15301a13bc6025955f275c1e573fc92f96778a7c972fc4d4d  mldsa-native/mldsa/mldsa_native.h
ed0bc5791c9d62df36a7e146cd919ef6d608dd115a5e1c3c1b1df76bc26a6f0d  mldsa-native/mldsa/mldsa_native_config.h
355704060689792c1ef7307844b486ac54bd397db5474a912cdf1d5ee4e0d2e7  mldsa-native/mldsa/src/cbmc.h
340e0a7c22f807ba020362d3cca7babadec0214c59329ea88c1f6e307686af84  mldsa-native/mldsa/src/common.h
c75d2d0f6555230e5fa6ea9147d523d5375fba247b314fe080d0454f89267719  mldsa-native/mldsa/src/context.h
5d5641f89ee92f8d33f4489b7746ca20d4226cf6ecfcc9895bf644d4bc8871e5  mldsa-native/mldsa/src/ct.c
706be081c657b742247b25588b6eb75f399f73b84b33ef20179ceeb721731078  mldsa-native/mldsa/src/ct.h
0200f23feaebfd12d35b39232c2ae566f7322d86993f981a705ddc6355a42edc  mldsa-native/mldsa/src/debug.c
4effd0eef9b63bdf434369b69ec5d14293ad7d22031b4f5ba237c69ce2fda383  mldsa-native/mldsa/src/debug.h
b8984253addafa72a0a127abbc80007509a36f23e63472d3ca54bed1d6e08d85  mldsa-native/mldsa/src/fips202/fips202.c
cdae2eb8a2d8970d8cb715e1c34230ee65ed35c75311525eca5232435d93c4e2  mldsa-native/mldsa/src/fips202/fips202.h
9c3f07deeee40277df068a7a6e28b57df7ea7fc9efefc400c6279f3781620f82  mldsa-native/mldsa/src/fips202/fips202x4.c
dc0a3bb0664ea664147df7f4ada4354114627f0fdbbe72981216c59387071a13  mldsa-native/mldsa/src/fips202/fips202x4.h
52b5134e53054d0e4dda93a48471d2d72cffc4b048f918450ac63b613e736328  mldsa-native/mldsa/src/fips202/keccakf1600.c
0b8b54a10be173d712d29ac806acb73924b38b4d06c68d35b1dde06a08ed005c  mldsa-native/mldsa/src/fips202/keccakf1600.h
f9dd14f8bf522fced013537986f3ed75ac0d237f27c9ebafc0f143bf2384b51f  mldsa-native/mldsa/src/fips202/native/api.h
7f1400021af42bca1e2e200b50b08633ad0ca11d10d6ef401196da61d4fb71d5  mldsa-native/mldsa/src/fips202/native/auto.h
c9d8dd51c3041fd029b3112a18ab18f632574af5b628d86dde9f460c8ad19be4  mldsa-native/mldsa/src/native/api.h
48d4b1890cd86cbf3ab72f4621fe38822b532af5c005df84f36a152d1e195c5b  mldsa-native/mldsa/src/native/meta.h
bc410982aca199135550eebdae4da4533d9c76e1a8ea0b8e0b02df9dccdd7632  mldsa-native/mldsa/src/packing.c
d095e2960591186bb7ae843381dbce5e407d41b62ceef2e46ce494f90348ccf1  mldsa-native/mldsa/src/packing.h
fd15d4b12747bffc17a78dd7e361fff7973e4455e3d3a162e83237f2d3029941  mldsa-native/mldsa/src/params.h
3bfb1b8f3c2e9fe1f00ca37d0fc64c57f70d0c3f845504989f39e9014799bdc3  mldsa-native/mldsa/src/poly.c
f753c1efdf41252198b2681a5e1f04252ed5707aa325e50c17f2846a2c12f18e  mldsa-native/mldsa/src/poly.h
2073e8e6230957f39feabc5d204f8055ae25e60dedb161eff3f3e4894832abe5  mldsa-native/mldsa/src/poly_kl.c
c2a52797f36d676c4f2c889986cddae06cbbd1004e23e09064353137e4cf2a44  mldsa-native/mldsa/src/poly_kl.h
bdca075b7f0d7741c649d59350e6eb94884b7160e0baefc5e50be492c1b2070a  mldsa-native/mldsa/src/polyvec.c
707341d7ea21f599dacad64845a7afbf4f78ce8f49b2b24986c7e5cc95813e4d  mldsa-native/mldsa/src/polyvec.h
5bc5b115b16393ca79f44540f5561da544496bda5b5514e1552015a0b83f0a73  mldsa-native/mldsa/src/polyvec_lazy.c
08d8f9fda9bc3f27cbbae40ff861f623576c0a2b520b991d666db0d20c3dce52  mldsa-native/mldsa/src/polyvec_lazy.h
b8ccb9a34818d8e1cd0cfea9b24829ab9949cb88c20ed0648459c5c7d36aa492  mldsa-native/mldsa/src/randombytes.h
c726a493163756a9746c71c247e8d8dfdfc96ce5ba85be4bcc0acdebf689e010  mldsa-native/mldsa/src/reduce.h
2fc8dd6f1ce513104287255e35a6087b2d17d2d8a9327eb1791e874c86f5a206  mldsa-native/mldsa/src/rounding.h
b4fddbdd7c0e6acb50683dc96319ef9a77c34d7fc6ea24c7ec40bf1a567afe66  mldsa-native/mldsa/src/sign.c
5392dd11d538563c8e89ae449e02fd78eb7c909393bc0596477e21e36e1b7abd  mldsa-native/mldsa/src/sign.h
4ed2df211b960a747e29aec04055100470def1ae8c481feb203c27fbc61a0b70  mldsa-native/mldsa/src/symmetric.h
a3bbf630c21751783547d21126af6e8162e6e0687d96e2d0f1aa14fd2c60d38f  mldsa-native/mldsa/src/sys.h
f27b371b78b05875d22e9b98f541f265c5f9358c3ece72dc91137114556d45ee  mldsa-native/mldsa/src/zetas.inc
```

### `argon2/`
```
220f8736a89ff51c92ef3d497f413b48e6cf1df3d6278bc909c6308c78e1718e  argon2/LICENSE
df20cb726a2fe6bccc736b81ea0d86219766a0d17f1794c19e048a34830ca1cc  argon2/argon2.h
72a93deebc5fd76bec0c6d300a2d92b500fb354b26babbddbc6d8b88681e663d  argon2/src/argon2.c
8ac91f1f57d94235f8de0069b7809be1deb365c3b37adb8e30423cd364aff09a  argon2/src/blake2/blake2-impl.h
3c2de197dd23179f78c57deac7b73a6049778bf651c5db57e508b2cfce5e7559  argon2/src/blake2/blake2.h
52519ccbc1e48f489ff444091f630d05dadcc8f1ee61c5b7361e3a9618299c9a  argon2/src/blake2/blake2b.c
bcfdcf785218cf897f05b144e80b659e611188fee3887d533bdcb7a6aa4c336b  argon2/src/blake2/blamka-round-ref.h
0f53eb2370f8971f04fcf043b8083bf81d1530c48b2ead71c0b6c4e22a01aeec  argon2/src/core.c
c9665623cb3d306f63b6a3effd87bcfab971c28253c77e111445e42c1523235e  argon2/src/core.h
e20124ec0f780f88527e79cf00825e839624ed1f469c7e231596bf070f8c7b14  argon2/src/encoding.c
42f283d12ec445cfb423ac2f1f5a5d1a7f152ce2b9363f6ec3e1d5b61cd4ee6d  argon2/src/encoding.h
4ec47b080c22f4ee416b9dbcfae70ee6007fcfba427f870d7b84395846fa1bc7  argon2/src/ref.c
1402085e2f54b021ea31b0d301f4b689a8b300832e2d11901502e62835185e3e  argon2/src/thread.c
9eab7f9ff356862a00a3075478dd0059344953ed79daab8b29185be2010efe78  argon2/src/thread.h
```

### `monocypher/`
```
a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0  monocypher/LICENCE.md
ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453  monocypher/monocypher-ed25519.c
3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce  monocypher/monocypher-ed25519.h
f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123  monocypher/monocypher.c
fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3  monocypher/monocypher.h
```

