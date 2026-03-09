# Repository Guidelines

## Project Structure & Module Organization
This repository is a small C++ solver for Wordbase. Core sources live in [`src/`](/home/ssilver/development/wordbase-player/src): [`driver.cpp`](/home/ssilver/development/wordbase-player/src/driver.cpp) builds the interactive CLI, shared game logic is mostly in headers such as [`board.h`](/home/ssilver/development/wordbase-player/src/board.h), [`wordbase-move.h`](/home/ssilver/development/wordbase-player/src/wordbase-move.h), and [`gtsa.hpp`](/home/ssilver/development/wordbase-player/src/gtsa.hpp), and support code sits beside them. Test and utility programs also live in `src/`, including `example.cpp`, `perf-test.cpp`, `test-namespace.cpp`, and `decrypt.cpp`. Third-party CMake helpers are under [`src/cmake/Modules/`](/home/ssilver/development/wordbase-player/src/cmake/Modules), and the bundled dictionary file is [`src/twl06_with_wordbase_additions.txt`](/home/ssilver/development/wordbase-player/src/twl06_with_wordbase_additions.txt).

## Build, Test, and Development Commands
Configure from a separate build directory:

```bash
mkdir -p build
cd build
cmake ../src -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

Use `-DCMAKE_BUILD_TYPE=Release` for search-performance runs. Main binaries are `./wordbase-driver`, `./example`, `./perf-test`, `./test-namespace`, and `./decrypt`. Run tests with `ctest --output-on-failure`; today, only the `example` GoogleTest target is registered with CTest. A clean configure needs network access to clone GoogleTest, plus local development packages for SQLite3, OpenSSL, and readline.

## Coding Style & Naming Conventions
Follow the existing C++11 style used in `src/`: two-space indentation, opening braces on the same line, and standard-library includes grouped before local includes. Types use `UpperCamelCase` (`WordBaseState`), functions use `lowerCamelCase` (`calculateDifferences`), and member fields commonly use an `m` prefix (`mLegalWordId`). Prefer small, header-driven additions over introducing new translation units unless link boundaries require them. No formatter is configured in-repo, so keep changes visually consistent with surrounding code.

## Testing Guidelines
GoogleTest is fetched during CMake configure via `googletest-download`, so network access is required on a clean build. Add functional tests in `src/example.cpp` or register new test executables in [`src/CMakeLists.txt`](/home/ssilver/development/wordbase-player/src/CMakeLists.txt) with `add_test(...)`. Name test cases for behavior, not implementation details, for example `SimpleWordsAtGridSquare`. If configure fails before test compilation, check dependency discovery first, especially `sqlite3`.

## Commit & Pull Request Guidelines
Recent history uses short, imperative commit subjects such as `Fix typos.`, `Add comments`, and `Make spreadsort work.` Keep commits focused and describe the behavior change, not the development process. Pull requests should summarize the scenario changed, list the commands run (`cmake`, `cmake --build`, `ctest`), and include CLI transcripts or screenshots when the user-facing shell behavior changes.
