# Graph Coloring via Constraint Programming and Column Generation

This repository contains an experimental C++ implementation for studying the
Graph Coloring Problem (GCP) with two complementary optimization ideas:

- Constraint Programming (CP), used to search for feasible colorings with fewer
  colors.
- Column Generation, represented by stable-set column data structures that can
  be extended into a full set-partitioning master problem.

The project is designed as an academic research prototype. Its main goal is not
to provide a production solver, but to make it easy to run controlled
experiments on standard DIMACS graph-coloring instances and compare how
heuristics, preprocessing, and CP search interact.

## Research Motivation

Given an undirected graph `G = (V, E)`, the Graph Coloring Problem asks for the
minimum number of colors needed to assign one color to every vertex such that no
two adjacent vertices share the same color. This minimum value is the chromatic
number of the graph.

Graph coloring is NP-hard, so exact solving becomes difficult on large
instances. In practice, research solvers usually combine several methods:

- fast heuristics to obtain good upper bounds;
- lower bounds from clique information;
- exact or constraint-based search to prove or improve a coloring;
- column generation or branch-and-price models based on stable sets.

This repository explores that hybrid direction experimentally.

## Current Approach

The current benchmark pipeline is:

1. Read DIMACS `.col` graph instances.
2. Generate clique information for lower bounds and CP strengthening.
3. Run DSATUR to obtain a fast initial coloring and an upper bound.
4. Run a Gecode CP model that tries to improve the DSATUR upper bound.
5. Export benchmark results to CSV.

The implemented components are:

- `DSATUR`: a classic saturation-degree heuristic for fast feasible colorings.
- `CP-UB`: a Constraint Programming upper-bound improvement phase using Gecode.
- `Clique preprocessing`: clique generation used as a lower bound and as
  `AllDifferent` constraints in the CP model.
- `StableColumn` and `ColumnPool`: data structures for stable-set columns,
  intended as the basis for a future column generation master problem.

At this stage, the column generation part is a foundation rather than a complete
branch-and-price implementation.

## Repository Structure

```text
.
+-- src/
|   +-- graph/             # Graph representation and DIMACS .col parser
|   +-- heuristic/         # Greedy coloring, DSATUR, randomized greedy
|   +-- preprocessing/     # Clique generation and lower-bound support
|   +-- cp_coloring/       # Gecode CP model for graph coloring
|   +-- master/            # Stable-set columns and column pool
|   +-- main.cpp           # Automatic benchmark runner
+-- tests/                 # Sample DIMACS .col instances
+-- results/               # Optional experiment outputs
+-- notebooks/             # Optional analysis notebooks
+-- benchmark.csv          # Example benchmark output
+-- CMakeLists.txt         # CMake build configuration
+-- .vscode/tasks.json     # VS Code build task
+-- LICENSE
```

## Requirements

- Windows
- CMake 3.16 or newer
- A C++17 compiler, typically Visual Studio with the x64 toolchain
- Gecode 6.2.0

The CMake configuration expects Gecode at:

```text
C:/Program Files/Gecode
```

If Gecode is installed somewhere else, update these variables in
`CMakeLists.txt`:

```cmake
set(GECODE_DIR "C:/Program Files/Gecode")
set(GECODE_INCLUDE "${GECODE_DIR}/include")
set(GECODE_LIB_DIR "${GECODE_DIR}/lib")
set(GECODE_BIN_DIR "${GECODE_DIR}/bin")
```

## Build

The usual workflow for this project is to build from VS Code:

```text
Ctrl + Shift + B
```

The default VS Code build task first configures CMake and then builds the Debug
configuration into the `build/` directory.

The same build can be run manually with:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

For a Release build:

```powershell
cmake --build build --config Release
```

The executable is generated at one of these paths:

```text
build/Debug/main.exe
build/Release/main.exe
```

## Running Experiments

Command format:

```powershell
.\build\Debug\main.exe [tests_dir] [time_limit_seconds] [csv_output]
```

Arguments:

- `tests_dir`: directory containing `.col` files. Default: `tests`.
- `time_limit_seconds`: CP time limit per instance. Default: `100`.
- `csv_output`: output CSV file. Default: `benchmark.csv`.

Example:

```powershell
.\build\Debug\main.exe tests 30 benchmark.csv
```

For Release:

```powershell
.\build\Release\main.exe tests 30 benchmark.csv
```

## Input Format

The program reads graphs in DIMACS `.col` format:

```text
c Example graph
p edge 4 3
e 1 2
e 2 3
e 3 4
```

DIMACS vertices are numbered from `1`. Internally, the parser converts them to
zero-based vertex indices.

## Output

The benchmark runner prints a summary table in the terminal and writes a CSV
file with the following columns:

- `instance`: input file name
- `n`: number of vertices
- `m`: number of edges
- `clique_lb`: clique-based lower bound
- `dsatur_k`: number of colors found by DSATUR
- `dsatur_time`: DSATUR running time in seconds
- `dsatur_valid`: whether the DSATUR coloring is valid
- `cp_k`: best number of colors found by CP
- `cp_time`: CP running time in seconds
- `cp_stopped`: whether CP stopped because of the time limit
- `cp_valid`: whether the CP coloring is valid

Example terminal output:

```text
============================================
 Auto Benchmark : DSATUR vs CP-UB
 Tests dir   : tests
 Time limit  : 30 s (per instance, CP only)
 Instances   : 10
============================================
Instance              |V|     |E|     LB      DSATUR_k  DSATUR_t(s) CP_k    CP_t(s)     CP_stop   Best
...
CSV exported -> benchmark.csv
```

## Implementation Notes

- `DSATUR_coloring` gives the first feasible solution and therefore an initial
  upper bound.
- `generate_clique` builds clique candidates by coloring the complement graph.
- `ColoringCP` creates one integer decision variable per vertex, with edge
  constraints enforcing different colors on adjacent vertices.
- Clique information is used for symmetry breaking and additional
  `AllDifferent` constraints.
- `cp_upper_bound` tries to reduce the number of colors from `dsatur_ub - 1`
  down to the clique lower bound until infeasibility or timeout.
- `ColumnPool` stores unique stable-set columns using a bitset representation,
  which is useful for extending the project toward column generation.

## Experimental Interpretation

The main comparison is between a fast heuristic upper bound and a CP-improved
upper bound. A smaller value of `cp_k` than `dsatur_k` means the CP phase found a
better coloring within the time limit. A tie does not prove optimality; it only
means no improvement was found during the configured search budget.

The clique lower bound helps interpret solution quality. If `cp_k` equals
`clique_lb`, then the instance is solved to optimality for that run because the
upper and lower bounds match.

## License

This project is licensed under the Apache License 2.0. See `LICENSE` for
details.
