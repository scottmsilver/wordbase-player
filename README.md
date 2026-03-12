# wordbase-player
Plays the game of wordbase.

This is a game solver for Wordbase with a minimax/alpha-beta search engine in C++ and a web UI built with React Native (Expo). The engine uses iterative deepening, transposition tables, killer moves, history heuristic, countermove heuristic, staged move generation, and lazy flood fill to search the game tree efficiently. On a 13x10 board with ~200 legal moves per position, it reaches 8-10M nodes/second.

The project includes:
- **C++ engine** (`src/`) -- minimax search with alpha-beta pruning, TT, move ordering
- **Interactive shell** (`wordbase-driver`) -- terminal-based board explorer and move suggester
- **Web UI** (`web/`) -- React Native (Expo) web app with human vs computer play
- **Engine server** (`src/server.cpp` + `web/engine-server.js`) -- JSON-over-HTTP bridge for the web UI
- **Benchmark tools** (`perf-test`, `run-benchmarks.sh`) -- self-play, profiling, A/B comparison

Build

The default build now prefers system-installed dependencies and ignores ambient environment prefixes such as Conda unless you opt in explicitly.

System packages expected on Linux are:
- C++ toolchain and CMake
- Boost with the `timer` component
- OpenSSL development headers and libraries
- SQLite3 development headers and libraries
- readline or libedit development headers and libraries

Typical Debian or Ubuntu packages are:

```
sudo apt install build-essential cmake libboost-all-dev libssl-dev libsqlite3-dev libreadline-dev
```

To build as a debug version (use `-DCMAKE_BUILD_TYPE=Release` for Release):

```
cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --output-on-failure --test-dir build
```

To build the release benchmark binary used for search tuning:

```
cmake -S src -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target perf-test
```

To build the engine server for the web UI:

```
cmake -S src -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target wordbase-server
```

If you intentionally want to use a non-system prefix such as Conda, pass it explicitly:

```
cmake -S src -B build -DCMAKE_BUILD_TYPE=Debug -DWORDBASE_DEPENDENCY_PREFIX=$CONDA_PREFIX
cmake --build build
ctest --output-on-failure --test-dir build
```

Web UI

The web UI lets you play Wordbase against the C++ engine in a browser. It uses React Native with Expo SDK 52 and React Native Web.

```bash
# Install dependencies (first time only)
cd web && npm install

# Terminal 1: Start the engine server (wraps the C++ engine as HTTP on port 3001)
node engine-server.js

# Terminal 2: Start the Expo dev server
npx expo start --web
```

The engine server spawns `build/wordbase-server` as a child process, communicating via JSON lines over stdin/stdout. The web UI sends board state to the engine when it's the computer's turn (Player 2 / blue) and applies the returned move.

If the engine server isn't running, the game shows "Engine unavailable" and you can play both sides manually.

Interactive Shell

To use the interactive board shell:
```
./build/wordbase-driver ./src/twl06_with_wordbase_additions.txt
```

Create a new board. The preceding *, means a bomb at the letter after the *. A + means a super-bomb.
```
boardshell> nb gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves
nb gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves
```
Print the state of the board. Each grid sqaure contains two pieces of information.

The first is either ".", "*", "+" or empty.

"." means owned
"*" or "+" means there's a bomb there (and it won't be owned)
empty means it's un owned.

The second is the letter. If it's upper-case, it's owned by player 1, the player moving from the top. If it's lower-case AND there's a "." next to it, it's owned by player 2, the player moving from the bottom.

```
boardshell> ps
ps
player(1): h=0
   0 1 2 3 4 5 6 7 8 9
 0.G.R.E.G.M.I.P.E.R.S
 1 l m a v n e t l a e
 2 c a o s r n o w y k
 3 o s b r i l f a k o
 4 s a l a g z l*e i c
 5 v e o n r e d g m d
 6 a m e p u m s e l o
 7 m r t l e i p c r a
 8 d s n d l n o i h u
 9 i a i*e o i s a t x
10 e r h c t p t e r o
11 u s t u p s y a l c
12.o.p.a.e.a.m.h.v.e.s

```
Show me all the words at position 0, 0.
It's (row, column) starting at 0
```
boardshell> words 0 0
words 0 0
gram: (0,0),(0,1),(1,2),(1,1)
grama: (0,0),(0,1),(1,2),(1,1),(2,1)
gramas: (0,0),(0,1),(1,2),(1,1),(2,1),(3,1)
grav: (0,0),(0,1),(1,2),(1,3)
grave: (0,0),(0,1),(1,2),(1,3),(0,2)
gravs: (0,0),(0,1),(1,2),(1,3),(2,3)
graal: (0,0),(0,1),(1,2),(2,1),(1,0)
glam: (0,0),(1,0),(2,1),(1,1)
glamor: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3)
glamors: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(2,3)
glamorize: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5)
glamorizer: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(5,4)
glamorized: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(5,6)
glamorizes: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
glass: (0,0),(1,0),(2,1),(3,1),(4,0)
```
Suggest a move, using 30 seconds to suggest one.
```
boardshell> sm 30
sm 30
goodness: 208 time: 0.00s move: lw(174) nodes: 306 leafs: 305 beta_cuts: 0 cutBF: nan tt_hits: 0 tt_exacts: 0 tt_cuts: 0 tt_size: 1 max_depth: 1
444102.78 nodes/s
goodness: -58 time: 0.01s move: lw(174) nodes: 1194 leafs: 888 beta_cuts: 140 cutBF: 1.00 tt_hits: 163 tt_exacts: 13 tt_cuts: 150 tt_size: 143 max_depth: 2
226647.31 nodes/s
goodness: 772 time: 0.23s move: lw(265) nodes: 75196 leafs: 73070 beta_cuts: 1203 cutBF: 1.40 tt_hits: 796 tt_exacts: 14 tt_cuts: 756 tt_size: 1167 max_depth: 3
325467.45 nodes/s
goodness: -261 time: 6.03s move: lw(13) nodes: 237550 leafs: 94573 beta_cuts: 55128 cutBF: 1.07 tt_hits: 123270 tt_exacts: 1079 tt_cuts: 86388 tt_size: 19825 max_depth: 4
39365.64 nodes/s
suggested move: glamorizes:(0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
lw(13)
player(2): h=-168
   0 1 2 3 4 5 6 7 8 9
 0.G.R.E.G.M.I.P.E.R.S
 1.L.M a v n e t l a e
 2 c.A.O s r n o w y k
 3 o s b.R.I l f a k o
 4 s a l a g.Z l*e i c
 5 v e o n r.E d g m d
 6 a m e p u m.S e l o
 7 m r t l e i p c r a
 8 d s n d l n o i h u
 9 i a i*e o i s a t x
10 e r h c t p t e r o
11 u s t u p s y a l c
12.o.p.a.e.a.m.h.v.e.s
```
Now show me where that word appears - legal word move (lwm).
```
boardshell> lwm glamorizes
lwm glamorizes
glamorizes: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6): h=-168
```
Now make the move.
```
boardshell> m (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
m (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
making move: "glamorizes": lw(13)
```
Show me the state again.
```
boardshell> ps
ps
player(2): h=-168
   0 1 2 3 4 5 6 7 8 9
 0.G.R.E.G.M.I.P.E.R.S
 1.L.M a v n e t l a e
 2 c.A.O s r n o w y k
 3 o s b.R.I l f a k o
 4 s a l a g.Z l*e i c
 5 v e o n r.E d g m d
 6 a m e p u m.S e l o
 7 m r t l e i p c r a
 8 d s n d l n o i h u
 9 i a i*e o i s a t x
10 e r h c t p t e r o
11 u s t u p s y a l c
12.o.p.a.e.a.m.h.v.e.s

```

Benchmarking

Use the checked-in benchmark scenarios instead of ad hoc command lines when comparing search changes:

```
./scripts/run-benchmarks.sh short
./scripts/run-benchmarks.sh long
./scripts/run-benchmarks.sh short-no-tt
./scripts/run-benchmarks.sh profile
./scripts/run-benchmarks.sh profile-suite
```

The current intended meanings are:
- `short`: primary two-turn comparison baseline for small A/B changes
- `long`: longer confirmation run with two warm-up turns
- `short-no-tt`: same short baseline with the transposition table disabled
- `profile`: repeated searches on the same post-warmup position so sampling profilers see steady-state search work instead of startup/setup
- `profile-suite`: the same repeated-search measurement across several checked-in board texts so we can catch heuristics that only help one board

The suite is deterministic on purpose. We keep one fixed-board benchmark for tight A/B comparisons, then use the checked-in suite in `scripts/benchmark-board-suite.txt` to make sure a change generalizes beyond the README board.

To generate self-play data for ML experiments, `perf-test` has a JSONL self-play mode:

```bash
./build-release/perf-test ./src/twl06_with_wordbase_additions.txt \
  --selfplay-games 10 \
  --selfplay-out /tmp/wordbase-selfplay.jsonl \
  --max-turns 80 \
  --seconds 0.05
```

Each JSONL line contains either a `move` record (board text, owner grid, move word/path, search stats) or a `summary` record with the final winner.
