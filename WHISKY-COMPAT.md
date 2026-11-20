# OpenKoGaMa Whisky compatibility launcher

**Target**: Whisky 3.10.2 / WhiskyWine 4.8.3 (Wine 11.19), macOS on Apple Silicon.

The existing launcher crashes during Qt GUI initialization in both an existing installation and a fresh Windows 10 bottle. Both supplied debugger reports identify `qt6gui+0x60397`, reached through `qwindows`. This narrows the investigation to Qt/platform initialization but does not identify the failing function or establish that dark-mode integration caused it. Offsets require the exact matching Qt DLL and symbols to interpret.

## Getting test executables

1. Use the fork [w2140404-oss/openkogama-launcher](https://github.com/w2140404-oss/openkogama-launcher), branch `codex/whisky-compat`. A push to this branch runs `.github/workflows/whisky-compat.yml`. The workflow can also be dispatched manually once it exists on the repository's default branch.
2. Open **Actions → Whisky compatibility launcher builds** and select a successful run. Under **Artifacts**, download `OpenKoGaMa-Whisky-Qt-6.8.3` and `OpenKoGaMa-Whisky-Qt-6.10.2` separately.
3. Extract each ZIP into its own folder. Keep the executable, DLLs, and plugin subfolders together. Run `openkogama-launcher.exe` through the same Whisky bottle for each test. Do not mix DLLs between artifacts.

These are launcher-only test builds from a fork. They contain no Unity game client, .NET server, or mods and are not official OpenKoGaMa releases. The workflow never invokes `release.yml`, packages an installer, creates a release, or rebuilds other repositories.

Each matrix job uses **Windows Server 2022, MSVC 2022 x64**, and the selected Qt SDK. It builds with `RelWithDebInfo`, runs the launcher regression tests, deploys Qt and app-local MSVC runtime DLLs, then runs the deployed smoke test with the SDK removed from `PATH`. A successful job checks native Windows startup and deployment, not Wine compatibility. Launcher PDBs, `build-info.json`, and `SHA256SUMS.txt` accompany the executable. Separate `Whisky-build-diagnostics-Qt-*` artifacts contain test and startup logs.

## Review of the DevKit

- The supplied patch passed `git apply --check` against source commit `78fea7706e81f249577932753ffcdede08c7f85b` and was applied once, then adapted.
- The original workflow relied on MinGW tool paths while the updater imports an MSVC Velopack library. This test workflow explicitly selects matching MSVC Qt binaries and disables the release updater entirely, removing that dependency and preventing replacement by an upstream release.
- CMake now requires Qt 6.8 because the existing theme code uses `QStyleHints::setColorScheme()`. Private ZIP-reader headers are resolved across the two Qt package layouts. Qt SVG plugins, Windows TLS support, and app-local MSVC runtime files are checked during deployment.
- Startup traces now include UTC timestamps, process IDs, source revision, compile/runtime Qt versions, Wine version, relevant Qt environment settings, Qt messages, and initialization phases. This also removes the patch's duplicated `Z` timestamp suffix.
- Asset-installer failures report `QProcess::FailedToStart`. Cleanup tracks the specific process, resets buffered output before retries, and checks for cancellation during progress delivery. A regression test exercises an invalid executable, retry, and missing-server fallback.

## Reproducing the Windows build

From an x64 MSVC 2022 developer shell with the chosen Qt SDK in `CMAKE_PREFIX_PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=cl -DCMAKE_INSTALL_BINDIR=. -DOPENKOGAMA_PORTABLE_TEST_BUILD=ON -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --timeout 120
cmake --install build --prefix "$PWD/portable"
```

The portable option bypasses Velopack bootstrap, downloads, and updates, including manual update requests. It uses the separate data directory `%LocalAppData%\OpenKogama\OpenKogama Launcher Whisky Test` and does not register itself as the installed launcher. Data remains in that directory rather than beside the executable. The regular launcher UI and game-launch code remain available, but game/server binaries must be supplied separately in a later testing phase.

## Tests (same Whisky 3.10.2 / Wine 11.19 environment)

- **Default launch**: use Whisky's Run action on `openkogama-launcher.exe`. The launcher defaults to `QT_QPA_PLATFORM=windows:darkmode=0` under Wine unless that variable was already supplied.
- **Smoke test**: `openkogama-launcher.exe --qt-smoke-test` shows a minimal label without launcher initialization, styles, translations, networking, or server processes. Add `--diagnostic` to capture Qt plugin discovery. The window stays open until closed; CI additionally uses `--smoke-test-timeout-ms=1500` to exit after entering the event loop. That timeout is accepted only with `--qt-smoke-test` and must be between 1 and 60000 milliseconds.
- **Diagnostic mode**: `openkogama-launcher.exe --diagnostic` opens the regular UI while skipping automatic server startup and update checking. It enables Qt plugin diagnostics unless `QT_DEBUG_PLUGINS` was explicitly supplied. The normal UI can still fetch launcher metadata; this is not an offline mode.
- **Startup log**: `%TEMP%\openkogama-startup.log` inside the bottle. The log appends, so group lines by PID and `process entry`. Set `OPENKOGAMA_STARTUP_LOG` to a different writable Windows path for individual runs. No log can be written if the Windows loader fails before `main()` or that path is unwritable.
- **Control test**: set `QT_QPA_PLATFORM=windows` externally to use Qt's default Windows platform behavior, or `windows:darkmode=2` for an explicit dark-mode comparison. This overrides the Wine-only `windows:darkmode=0` default. The default is an experiment, not a confirmed crash fix.

For each Qt version, first test the minimal window, then the full diagnostic UI, then normal startup. Compare the default platform option and the control using the same bottle and executable. Record the Qt version, `build-info.json` commit, arguments, environment overrides, bottle Windows version, observed window, exit status, complete startup log, and Wine debugger report if it crashes.

## Interpreting results

| Qt 6.8.3 | Qt 6.10.2 | Interpretation |
|---|---|---|
| Opens | Crashes | Qt version regression candidate: use 6.8.3 while investigating. |
| Crashes | Crashes | Inspect smoke test and phase logs; likely not solved by Qt version change alone. |
| Opens | Opens | The original runtime/configuration or QPA option may explain the difference. |

If the last phase is `before QApplication`, the failure precedes all launcher UI code. If the minimal window works but the full UI fails, use the theme, language, main-window construction, and show markers to narrow the application-specific path. Preserve the matching Qt DLLs and debugger report rather than assigning a function name to an unmatched offset.

Once launcher startup is verified, asset installation can be tested separately. A child that cannot start now reports its error instead of leaving the launch dialog at 0%; a child that starts but deadlocks still needs investigation.

## Limitations

- **Wine compatibility remains unverified until these exact executables are tested on the target Mac.** Successful Windows compilation or a native Windows smoke test does not establish Whisky/Wine compatibility.
- The Qt version matrix is intentionally exploratory. Resolving a crash may require symbols, a test build, or upstream Qt/Wine changes.
- Do not distribute test builds as official OpenKoGaMa releases. This is a fork for debugging.
