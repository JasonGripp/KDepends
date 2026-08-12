# KDepends — development guide

KDepends is a native KDE/Qt6 application that statically inspects Linux ELF
binaries: it shows a file's recursive dynamic dependency tree, the imports and
exports of any module in that tree, and a flattened table of every module in the
closure. It is a Linux answer to the Windows tool *Dependencies* / Dependency
Walker. It never loads or executes the target.

See [README.md](README.md) for the user-facing description.

---

## Build, run, install

The project is CMake + extra-cmake-modules (ECM), standard KDE layout. Ninja is
the configured generator; `build/` is git-ignored.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
./build/bin/kdepends /usr/bin/some-binary     # run without installing
sudo cmake --install build                    # binary, .desktop, XmlGui rc
```

Incremental builds: `ninja -C build`. `compile_commands.json` is exported for
clangd.

Requirements: C++20 compiler, Qt ≥ 6.5 (Core, Gui, Widgets), KDE Frameworks ≥ 6
(CoreAddons, I18n, XmlGui, Config, ConfigWidgets, WidgetsAddons, KIO), and ECM.

Notes when running from the build tree:

- `setupGUI` looks for the installed `kdependsui.rc`. Uninstalled runs fall back
  to the compiled-in defaults, so menu/toolbar layout may differ from an
  installed run. Install before judging menu structure.
- Settings live in `kdependsrc` (options) and the state config (recent files,
  geometry, splitter state) under `~/.config`.

Always build before considering a change done. Prefer verifying UI-visible
changes by actually running the app on a real binary with a deep closure
(`/usr/bin/kdepends`, `/usr/bin/dolphin`, `/usr/lib64/libQt6Widgets.so.6`).

---

## Repository layout

```
CMakeLists.txt         # project, ECM, Qt6/KF6 find_package
src/
  CMakeLists.txt       # the single executable target, sources, links, install
  kdependsui.rc        # KXmlGui menu/toolbar definition
  kdepends.desktop     # desktop entry
  *.h / *.inlines.h / *.cpp
.claude/
  coding-style.md      # C++ style rules (included below — they are binding)
  design-feature.md    # phased design-first protocol for adding a feature
```

Everything is one flat `src/` directory and one executable target. When adding a
file, add it to `src/CMakeLists.txt`'s `target_sources` list.

---

## Architecture

Four layers, each depending only on the ones above it. Keep this direction of
dependency intact — it is the main structural invariant of the codebase.

**1. Core analysis — pure C++20, no Qt.** Unit-testable in isolation, never
touches the UI.

| File | Role |
| --- | --- |
| `elfstructs.h` | On-disk ELF layouts and constants, self-contained (no `<elf.h>`) |
| `binaryreader.*` | Bounds-checked little-endian cursor over a byte buffer; the only place raw bytes are touched |
| `moduledata.h` | Plain data: module metadata, import/export symbols, closure containers |
| `demangler.*` | Thread-safe cached wrapper over `abi::__cxa_demangle` |
| `elfparser.*` | Read-only ELF parser producing `moduledata`, plus the cheap candidate "sniff" |
| `ldcache.*` | `/etc/ld.so.cache` parser |
| `pathresolver.*` | glibc dynamic-linker search order, one needed name → one path |
| `dependencyresolver.*` | Incremental closure construction, one module expanded at a time |
| `importresolver.*` | Breadth-first ELF global lookup: which module provides each import |

**2. Concurrency bridge — Qt Core.** `analysisengine.*` only. Owns the thread
pool, takes requests from the UI thread, runs core analysis on background
threads, and delivers copies back via queued signals. Per-session (per-tab)
grouping and cancellation.

**3. UI models and widgets — Qt Widgets / KF6.** `icons.*` (state → icon/colour/
display text, centralised so every view renders a state identically),
`filteredtable.*` (table + live filter box + copy/select-all, shared by three
panels), `dependencytreemodel.*`, `importsmodel.*`, `exportsmodel.*`,
`modulesmodel.*`, `moduletab.*` (the per-file four-panel splitter), and
`mainwindow.*` (KXmlGui window, tabs, actions, KConfig persistence).

**4. Entry point.** `main.cpp` — about data, i18n, command line, window.

### Invariants — do not break these

- **The UI thread never parses a file and never touches the disk for analysis.**
  All parsing, resolution, and symbol work goes through `CAnalysisEngine` and
  runs on its thread pool; results reach the UI as copies over queued
  connections. This is a hard requirement, not an optimisation.
- **The UI never reads a live closure.** It holds only what the engine handed
  it. Do not expose resolver state by reference across the thread boundary.
- **Cancellation is per session.** Closing a tab must abandon its outstanding
  work promptly, and in-flight results for a cancelled session are dropped.
- **Malformed input is never a crash.** Every read is range-checked; parse
  failures are structured errors. A missing library is a normal *missing* state,
  not an error.
- **Exceptions must not cross a thread boundary.** `KDECompilerSettings` builds
  with `-fno-exceptions`; the target re-enables them via
  `kde_target_enable_exceptions` purely so background tasks can wrap their work
  in `try`/`catch`. Do not use exceptions as ordinary control flow.
- **No libelf / ELFIO.** ELF parsing is in-project, from raw bytes.
- Little-endian ELF only, ELFCLASS32 and ELFCLASS64, any machine type.
- Resolution deliberately reflects *this* machine and KDepends' own environment
  (`LD_LIBRARY_PATH` included) — "what would load here, now".

### Non-goals

PE/COFF, `dlopen` discovery or anything requiring execution/tracing,
big-endian ELF, core dumps, kernel modules, static archives, disassembly or hex
viewing, non-C++ demangling, and network features beyond opening a web search in
the browser. Flag rather than silently absorb work that pushes against these.

---

## Conventions

- Qt does not constrain signal or slot names, so signals and public slots follow
  the project's PascalCase convention; private slots are camelCase like other
  private member functions.
- Qt/KF6 headers are included as project style dictates: project headers first,
  quoted, then Qt/KF6, then the standard library. Every file is self-contained.
- User-visible strings go through `i18n()` / `i18nc()`. There is no `pch.h`.
- `.clang-format` (tabs, ColumnLimit 0, custom brace wrapping) matches the style
  rules below; run it on files you touch, not on the whole tree.
