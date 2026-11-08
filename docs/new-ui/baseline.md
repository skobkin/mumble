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

Build status: **executed successfully for the client**. The build was started
with `-j1` under the low-memory constraint and resumed with
`cmake --build build -j4` once more memory was available (ninja resumes
incrementally). Final state: client binary `build/mumble`, all position-audio
plugins, and 16 of 17 test executables built and linked. The single failure is
the `TestMumbleProtocol` compile described in section 7; `ninja -C build -j4
-k 0` was used to complete all other targets despite it. Runtime sanity
check: `./build/mumble --version` prints `1.6.0`, `./build/mumble --help`
renders the CLI11 help output.

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

Results: **16 of 17 suites passed** (`ctest --output-on-failure --test-dir
build`, total runtime ~5 s). `TestMumbleProtocol` was **not run** — its
executable could not be built (see section 7); ctest counts that as a failure
in its summary ("94% tests passed, 1 tests failed out of 17").

## 6. Protecting the original Mumble configuration

The redesigned client must support alternating use with the official client
and its existing configuration. For baseline purposes no runtime launch
against the user's normal profile was performed.

Relevant CLI options (`src/mumble/main.cpp`, CLI11-based parser):

- `-c,--config <config>` (main.cpp:265): use an alternative configuration
  file. It must be an **existing, writable JSON file**, with a `.json` suffix
  for saving (`main.cpp:422,664`; `Settings.cpp:144,220`). Explicit `--config`
  does not invoke the legacy INI importer. The JSON database setting is
  `misc.database_location`; `--config` alone does **not** guarantee a separate
  SQLite database.
- `--default-certificate-dir <dir>` (main.cpp:273): alternative default
  certificate path, only used when no certificate is loaded from settings.
- `-m,--multiple` (main.cpp:281): allow multiple instances.

For a native Linux development build, run the following from the repository
root in Bash (requires Python 3 with its standard-library `sqlite3` module).
Fully quit all Mumble clients first, including tray-resident processes, and
keep them closed during this check. Do not use `--multiple`: this recipe tests
one process at a time and avoids forwarding startup to an existing client.

The fresh JSON uses defaults for omitted settings. Pre-creating a writable
SQLite file avoids the missing-database prompt and accidental selection of
"Reset Configured Path", which would re-enable database discovery.

```bash
umask 077
mumble_test_binary="$(pwd)/build/mumble"
mumble_test_root=$(mktemp -d /tmp/mumble-new-ui.XXXXXXXX) || exit 1
mkdir -p "$mumble_test_root"/{profile,config,data,cache,state,tmp,certificates} || exit 1

python3 - "$mumble_test_root" <<'PY' || exit 1
import json
import sqlite3
import sys
from pathlib import Path

root = Path(sys.argv[1])
database = root / "profile" / "mumble.sqlite"
with sqlite3.connect(database) as connection:
    connection.execute("PRAGMA user_version = 0")
    assert connection.execute("PRAGMA integrity_check").fetchone() == ("ok",)
connection.close()
settings = {
    "settings_version": 1,
    "mumble_has_quit_normally": True,
    "misc": {"database_location": str(database)},
}
(root / "profile" / "mumble_settings.json").write_text(
    json.dumps(settings, indent=4) + "\n", encoding="utf-8"
)
PY

# Reuse this function and root for subsequent launches in this shell.
run_mumble_test() (
    cd "$mumble_test_root/profile" || exit 1
    env XDG_CONFIG_HOME="$mumble_test_root/config" \
        XDG_DATA_HOME="$mumble_test_root/data" \
        XDG_CACHE_HOME="$mumble_test_root/cache" \
        XDG_STATE_HOME="$mumble_test_root/state" \
        TMPDIR="$mumble_test_root/tmp" \
        "$1" --config "$mumble_test_root/profile/mumble_settings.json" \
        --default-certificate-dir "$mumble_test_root/certificates"
)

printf 'Disposable test profile: %s\n' "$mumble_test_root"
run_mumble_test "$mumble_test_binary"
```

`Global` uses the explicit config's directory for Plugins/Overlay/Themes and
skips legacy data migration on this path (`Global.cpp:144–167`). The XDG
overrides also isolate Qt's application configuration/data/cache paths;
`TMPDIR` isolates the fixed `mumble_settings.json.tmp` used by the shared saver.
The certificate directory keeps automatic certificate backups in the test
profile. Desktop/session variables remain inherited for native GUI/audio use.

Quit through the application's Quit action and wait for the process to exit.
After each run, check that JSON still points to the exact test SQLite path,
that the expected database contains the client tables and passes
`PRAGMA integrity_check`, and that generated profile files stay beneath the
printed directory. Treat any database-open error or fallback path as a failed
test; do not continue with another database. Retain this directory for review
or remove only this generated directory after all test processes have exited.

**Restart limitation:** the inspected `main.cpp:1020–1054` drops `--config`
and `--default-certificate-dir` on automatic restart. Until the foundation fix
is implemented, decline automatic restarts; quit and call `run_mumble_test`
again manually. This recipe isolates a normal native Linux launch, not every
failure path: database fallback can search the real home/executable directory.
Use a disposable OS account or VM for fallback, legacy migration and automatic
restart tests, and for Windows/macOS or sandboxed-package path certification.

The initial compatibility target is **official 1.6.x versus the 1.6.x-based
fork**. For alternation, reuse this same generated JSON, SQLite file and launch
function, supplying each executable's absolute path in turn after full exit;
do not recreate the fixture between clients. Separate independent test profiles
must each have their own SQLite file and data directories. Follow
[architecture.md §7.3](architecture.md#73-alternating-client-release-test) for
the assertions. The documented [1.5.x limitations](architecture.md#57-version-compatibility-is-bounded-not-assumed)
remain unresolved; this recipe does not certify cross-version losslessness.
No runtime launch was performed as part of this documentation correction.

## 7. Known environmental limitations and build blockers

- **Build blocker (test suite only): `TestMumbleProtocol` fails to compile.**
  The system protobuf (36.1) marks `SerializeWithCachedSizesToArray()` and
  `ParseFromArray()` as `[[nodiscard]]` (`PROTOBUF_FUTURE_ADD_EARLY_NODISCARD`
  in `google/protobuf/message_lite.h`), while
  `src/tests/TestMumbleProtocol.cpp:283` and `:309` ignore those return
  values. With GCC 16 and `warnings-as-errors=ON` this is a hard
  `-Werror=unused-result` error. The client itself and all other test suites
  compile and link **warning-free** under GCC 16.2.1 / Qt 6.12.0. Fix for
  separate handling: use or explicitly discard the return values in the test.
- RAM: the build was started with `-j1` because only ~4 GiB of 15 GiB were
  available; it was resumed with `-j4` once ~5 GiB were free and completed
  without memory problems.
- Fresh configure requires network access once (RNNoise model download,
  section 4).
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
