# 1.6.x-new-ui build baseline

This document records the baseline for the incremental native Qt Widgets UI
modernization project (`1.6.x-new-ui` branch): the exact upstream base, how the
branch was created, a reproducible build, the test baseline and observations
relevant for the subsequent architecture investigation.

## 1. Repository and upstream information

| Item | Value |
| --- | --- |
| Upstream repository | <https://github.com/mumble-voip/mumble> |
| Upstream base branch | `1.6.x` |
| Upstream base commit | `535bb98ec4fb550f3b8f6b03028d44431c80a724` |
| Base commit date | 2026-11-08T13:58:21Z |
| Base commit subject | Backport "Merge PR #7483: FIX(server): Prevent server startup if (self-signed) certificate is empty" to 1.6.x (#7486) |
| Development branch | `1.6.x-new-ui` (tracks `upstream/1.6.x`) |
| Fork (`origin`) | `git@github.com:skobkin/mumble.git` |

At preparation time the fork's `origin/1.6.x` pointed at the same commit as
`upstream/1.6.x`. The upstream history is kept intact; all project work happens
on top of `1.6.x-new-ui`.

## 2. Branch preparation

```bash
git remote add upstream https://github.com/mumble-voip/mumble.git
git fetch upstream 1.6.x
git checkout -b 1.6.x-new-ui upstream/1.6.x
git submodule update --init --recursive
```

Submodules used by this branch: `3rdparty/minhook`, `3rdparty/speexdsp`,
`3rdparty/rnnoise-src`, `3rdparty/tracy` (only if `tracy=ON`),
`3rdparty/cmake-compiler-flags` (required by `cmake/compiler.cmake`),
`3rdparty/flag-icons`. Other `3rdparty/` entries (CLI11, nlohmann_json, soci,
spdlog, utfcpp, qqbonjour, smallft, ...) are vendored directly in the tree.

## 3. Development environment

- OS: CachyOS (Arch Linux), kernel 7.3.1-1-cachyos, x86-64, 16 CPUs
- cmake 4.4.5, ninja 1.13.2
- GCC 16.2.1 (default compiler), clang 23.1.4 (installed, not used)
- Qt 6.12.0 (qt6-base, qt6-tools, qt6-svg, qt6-translations)
- protobuf 36.1, openssl 3.6.5, libsndfile, libsodium, libspeechd, libpulse,
  alsa-lib, jack2, pipewire 1.6.10, portaudio, avahi 0.9rc5 (with
  compat-libdns_sd), boost 1.92.0
- Python 3.14.9
- RAM: 15 GiB total

Missing package that had to be installed: `libsndfile` (required by
`src/mumble/CMakeLists.txt`, `find_pkg("SndFile;LibSndFile;sndfile" REQUIRED)`).
Note: on Arch/CachyOS there is no separate `avahi-compat` package — the
installed `avahi` package already provides `libdns_sd.so` and the
`avahi-compat-libdns_sd.pc` pkg-config module, so the default `zeroconf=ON`
works out of the box. `grpc`/`zeroc-ice` are not needed because the server is
not built.

## 4. Build configuration and commands

The build follows `docs/dev/build-instructions/build_linux.md`: regular
developer build with system dependencies (no dependency provider, no static
build), out-of-tree in `build/` (gitignored).

```bash
cmake -G Ninja -S . -B build \
    -Dserver=OFF \
    -Dtests=ON \
    -Doverlay-xcompile=OFF
cmake --build build
```

Option rationale:

| Option | Why |
| --- | --- |
| `-Dserver=OFF` | Client-only work; avoids missing `grpc`/`zeroc-ice` and the MySQL/PostgreSQL backends (their defaults follow `server`) |
| `-Dtests=ON` | Test baseline (default is `${packaging}`, i.e. OFF) |
| `-Doverlay-xcompile=OFF` | The 32-bit overlay library would require a multilib GCC toolchain; irrelevant for UI work (`build_linux.md` documents this) |

Everything else is left at default: `CMAKE_BUILD_TYPE=Release` (set by the
project when unset), SQLite database tests ON, online tests OFF,
`warnings-as-errors=ON`, unity build OFF, all audio backends (ALSA, OSS,
PulseAudio, PipeWire, JACK, PortAudio) ON, overlay (64-bit) ON, translations
ON, zeroconf ON.

Configure status: **verified working** (exit 0, cmake 4.4.5 + Ninja
generator). Resolved dependencies: gl 1.2, Qt 6.12.0 (Core, Network, Xml,
Concurrent, Sql, Svg, Widgets, DBus, LinguistTools, Test), OpenSSL 3.6.5,
Protobuf 36.1.0, Poco (XML, Zip) 1.15.4, SndFile 1.2.2 (Opus and MP3 support
detected), system opus 1.6.1, speech-dispatcher 0.12.1, ALSA 1.2.16.1, X11
(Xext, Xi), vendored speexdsp and rnnoise. Enabled features (from the actual
compile definitions): ALSA, PulseAudio, PipeWire, JACK and PortAudio backends,
zeroconf (Avahi compat), DBus, speech-dispatcher, 64-bit overlay, manual
plugin, RNNoise.

Note: a fresh configure fetches the RNNoise noise-reduction model via CMake
`FetchContent` into `build/_deps/`, so the first configure needs internet
access once.

Build status: **not executed** — deferred by operator decision, because only
~4 GiB of 15 GiB RAM were available at preparation time (swap already ~50%
used; a 16-way parallel build would exhaust memory). The commands above are
the recorded build procedure; if memory is still tight, cap the jobs
explicitly (e.g. `cmake --build build -j4`). Whether the tree compiles
warning-free with GCC 16 (`warnings-as-errors=ON`) and Qt 6.12 is unverified
until the first build runs (see section 7).

## 5. Test baseline

Tests are QtTest-based executables in `src/tests/`, registered with CTest and
run from the build directory (see `AGENTS.md`):

```bash
ctest --output-on-failure --test-dir build
```

With `client=ON, server=OFF, tests=ON`, `ctest -N` confirms 17 registered
suites:

- client-only: `TestXMLTools`, `TestSettingsJSONSerialization`
- shared: `TestCaseInsensitiveQString`, `TestCryptographicHash`,
  `TestCryptographicRandom`, `TestFFDHE`, `TestPacketDataStream`,
  `TestPasswordGenerator`, `TestMumbleProtocol`, `TestSelfSignedCertificate`,
  `TestServerAddress`, `TestSSLCiphers`, `TestSSLLocks`, `TestStdAbs`,
  `TestTimer`, `TestUnresolvedServerAddress`, `TestVersion`

Excluded by configuration: `TestCrypt`, `TestAudioReceiverBuffer` and
`TestDatabase` (server-only; `TestDatabase` sits in the "shared" section of
`src/tests/CMakeLists.txt` but early-returns when `server=OFF`),
`TestServerResolver` (online test, needs `-Donline-tests=ON`),
MySQL/PostgreSQL database test variants (`-Ddatabase-mysql-tests=ON`,
`-Ddatabase-postgresql-tests=ON`, need running database servers).

To run only client-related tests: `ctest --test-dir build -R "TestXMLTools|TestSettingsJSONSerialization"`.

Results: **not executed** (build deferred, see section 4).

## 6. Protecting the original Mumble configuration

The redesigned client must support alternating use with the official client
and its existing configuration. For baseline purposes no runtime launch
against the user's normal profile was performed.

Relevant CLI options (`src/mumble/main.cpp`, CLI11-based parser):

- `-c,--config <config>` (main.cpp:265): use an alternative configuration
  file. The option requires an **existing file** (`CLI::ExistingFile`). The
  help text explicitly warns that when running multiple instances, an
  alternative `database` value must also be set in that config — i.e.
  `--config` alone does **not** relocate the SQLite database.
- `--default-certificate-dir <dir>` (main.cpp:273): alternative default
  certificate path, only used when no certificate is loaded from settings.
- `-m,--multiple` (main.cpp:281): allow multiple instances.

Recommended isolated runtime check for development builds (keeps the normal
profile untouched):

```bash
mkdir -p /tmp/mumble-newui-profile
# create an INI config there; set database=<path inside that dir>
./build/mumble -c /tmp/mumble-newui-profile/mumble.ini -m
```

Actual shared-configuration compatibility changes will be designed after the
architecture investigation.

## 7. Known environmental limitations and build blockers

- RAM: at preparation time only ~4 GiB of 15 GiB were available, so the build
  was deferred by operator decision. The configure step was still verified
  (negligible memory use). Before the first build, either free memory or cap
  jobs (e.g. `cmake --build build -j4`).
- Fresh configure requires network access once (RNNoise model download,
  section 4).
- Bleeding-edge toolchain: GCC 16.2.1 (with `warnings-as-errors=ON`,
  i.e. `-Werror -Wall -Wpedantic -Wextra -Wconversion -Wsign-conversion`) and
  Qt 6.12.0 are newer than what the `1.6.x` branch is routinely validated
  against; the first build may surface new compiler diagnostics.
- OSS audio backend is not enabled (no `USE_OSS` definition); irrelevant here
  since PulseAudio/PipeWire are in use.
- Online tests and MySQL/PostgreSQL database tests are excluded (no
  network-dependent or external-service tests run in this environment).

## 8. Observations relevant to the architecture investigation

- The client is a native Qt Widgets application: 37 `.ui` files in
  `src/mumble/` plus additional widget/dialog classes under
  `src/mumble/widgets/` (also with `.ui` files); ~129 `.cpp` files in
  `src/mumble/` top level.
- Minimum Qt version is 6.2 (`src/CMakeLists.txt:21-28`), so the baseline
  branch supports distros older than the local Qt 6.12 — new UI code should
  stay compatible with that floor unless the branch policy changes.
- Theming infrastructure exists in-client (`Themes.cpp`, `ThemeInfo.cpp` in
  `src/mumble/`).
- Command-line parsing uses bundled CLI11 (`src/mumble/main.cpp`), which is
  the natural place for any future UI-selection or profile-related options.
- Tests are QtTest-based per-directory executables in `src/tests/`, each with
  its own `CMakeLists.txt`; adding UI tests follows that pattern.
- The protocol layer is generated from protobuf files (`src/Mumble.proto`,
  `src/MumbleUDP.proto`) and is UI-independent; UI work should not need to
  touch it.
