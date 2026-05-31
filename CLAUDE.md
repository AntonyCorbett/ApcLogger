# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

ApcLogger is a minimal Windows-only C++ static library (`ApcLogger.lib`) that provides thread-safe file logging. It is intended to be copied into other projects as a dependency, not consumed via a package manager.

## Build

Open `ApcLogger.sln` in Visual Studio 2022, or build from a Developer Command Prompt:

```
msbuild ApcLogger.sln /p:Configuration=Release /p:Platform=x64
```

Supported configurations: `Debug|Win32`, `Release|Win32`, `Debug|x64`, `Release|x64`. Output `.lib` lands in e.g. `x64\Release\ApcLogger.lib`.

- x64 targets use C++20 and the static CRT (`/MT` / `/MTd`)
- Win32 targets use C++17 with the default (dynamic) CRT
- Toolset: MSVC v145 (VS 2022)

There are no automated tests in this repository.

## Architecture

All public API is in `ApcLogger/Logger.h` (namespace `Logger`) with convenience macros (`LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`). The implementation is entirely in `ApcLogger/Logger.cpp`.

Key internals (`Logger.cpp` anonymous namespace):
- A single `std::mutex` guards the file stream and all writes
- `MinLevel` is an `std::atomic<Logger::Level>` — default `Debug` in debug builds, `Info` in release
- Log rotation triggers at 512 KB; the current file is renamed to `<name>.log.1` (one backup only)
- Every write also calls `OutputDebugStringW` so output appears in the VS debugger Output window
- Log files are written as UTF-8 with BOM; wide strings are converted via `WideCharToMultiByte`
- `LogLastError` calls `GetLastError()` (or uses the supplied code) and formats it with `FormatMessageW`

Typical consumer usage:
```cpp
Logger::Init(L"MyApp", L"C:\\ProgramData\\MyApp\\Logs");
LOG_INFO(L"Started version %ls", version.c_str());
// ...
Logger::Shutdown();
```
