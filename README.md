# Graph Coloring via Constraint Programming and Column Generation

A C++ research prototype implementing two complementary exact/heuristic approaches to the Graph Coloring Problem (GCP): a Constraint Programming (CP) upper-bound phase using Gecode, and a Column Generation LP relaxation using Gurobi. The project serves as a hands-on study of the branch-and-price paradigm for graph coloring.

---

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [Algorithmic Framework](#2-algorithmic-framework)
   - [2.1 CP Upper Bound](#21-cp-upper-bound)
   - [2.2 Set-Partitioning LP and Column Generation](#22-set-partitioning-lp-and-column-generation)
   - [2.3 Maximum Weight Stable Set Pricing](#23-maximum-weight-stable-set-pricing)
3. [Repository Structure](#3-repository-structure)
4. [Requirements](#4-requirements)
5. [Build](#5-build)
6. [Running Experiments](#6-running-experiments)
   - [6.1 CP Benchmark (main)](#61-cp-benchmark-main)
   - [6.2 Column Generation (main_master)](#62-column-generation-main_master)
7. [Input Format](#7-input-format)
8. [Output](#8-output)
9. [Implementation Notes](#9-implementation-notes)
10. [References](#10-references)

---

## 1. Problem Statement

Given an undirected graph $G = (V, E)$, the **Graph Coloring Problem** asks for the minimum number of colors $\chi(G)$ (the chromatic number) such that every vertex receives a color and no two adjacent vertices share the same color.

GCP is NP-hard. Research solvers typically combine:

- fast heuristics for an initial upper bound $\bar{\chi}$,
- clique-based lower bounds $\underline{\chi}$,
- exact search (CP or B&B) to close the gap,
- LP relaxations based on independent-set / stable-set formulations for fractional lower bounds.

This repository implements and experiments with all four components.

---

## 2. Algorithmic Framework

### 2.1 CP Upper Bound

The CP model (`src/cp_coloring/`) assigns one integer variable $x_v \in \{0,\ldots,k-1\}$ per vertex. Constraints:

- **Edge constraints**: $x_u \neq x_v$ for every $(u,v) \in E$.
- **Clique AllDifferent**: for each discovered clique $C$, `distinct(x[C])` (domain-consistent propagation via `IPL_DOM`).
- **Symmetry breaking**: vertices of the largest clique are fixed to colors $0, 1, \ldots, |C|-1$ by degree order, eliminating equivalent colorings. Colors not used by the clique are handled by a `ValueSymmetry` Gecode symmetry breaker.

The search strategy is `INT_VAR_SIZE_MIN` (smallest domain first) with `INT_VAL_MIN`.

The **`cp_upper_bound`** function iterates downward from `dsatur_ub - 1` to the clique lower bound, splitting the remaining time budget adaptively (`max(5s, remaining/3)` per call). It returns the best feasible coloring found or signals timeout.

### 2.2 Set-Partitioning LP and Column Generation

The column-generation approach (`src/master/`) is based on the set-partitioning formulation of Mehrotra & Trick (1996).

**Master problem (LP relaxation):**

Let $\mathcal{S}$ be the family of all maximal stable sets of $G$. Define binary indicator $a_{vs} = 1$ if vertex $v \in S$. The integer program for chromatic number is:

$$\min \sum_{S \in \mathcal{S}} \lambda_S$$

$$\text{subject to} \quad \sum_{S \ni v} \lambda_S \geq 1 \quad \forall v \in V$$

$$\lambda_S \in \{0,1\} \quad \forall S \in \mathcal{S}$$

The LP relaxation relaxes $\lambda_S \in [0,1]$. Its optimal value $z^*_{\text{LP}}$ satisfies $z^*_{\text{LP}} \leq \chi(G)$ and is at least as tight as the clique lower bound.

Because $|\mathcal{S}|$ is exponential, the LP is solved by **column generation**:

1. Start with a **Restricted Master Problem (RMP)** containing a small initial set of columns (stable sets).
2. Solve the RMP LP to get dual values $\pi_v \geq 0$ for each vertex constraint.
3. Solve the **pricing problem**: find a stable set $S^*$ with reduced cost $\bar{c}_{S^*} = 1 - \sum_{v \in S^*} \pi_v < 0$.
4. If such $S^*$ exists, add column to RMP and repeat.
5. If no improving column exists, the RMP LP optimal equals the full LP optimal — convergence.

**`RMPSolver`** builds the Gurobi LP model. Each column is added via `GRBColumn`, linking it to the vertex coverage constraints. Dual values are extracted after each solve via `GRBConstr::get(GRB_DoubleAttr_Pi)`.

**Column initialization**: `ColumnPool::initialize` runs `num_trials` random-order greedy colorings; each color class is a stable set, then **maximalized** (greedily extend to maximal stable set) before insertion. Deduplication uses a bitset hash (`BitsetHash`).

### 2.3 Maximum Weight Stable Set Pricing

The pricing problem is itself NP-hard (it is the Maximum Weight Independent Set problem, MWIS). It is formulated as a binary program and solved with Gurobi:

$$\max \sum_{v \in V} \pi_v \cdot y_v$$

$$\text{subject to} \quad y_u + y_v \leq 1 \quad \forall (u,v) \in E$$

$$y_v \in \{0,1\} \quad \forall v \in V$$

`solve_mwss` returns `true` (new column added) iff the optimal weight exceeds $1 + \varepsilon$, i.e., reduced cost $< -\varepsilon$.

---

## 3. Repository Structure

```
.
├── src/
│   ├── graph/                   # Graph representation and DIMACS parser
│   │   ├── graph.h
│   │   └── graph.cpp
│   ├── heuristic/               # Greedy, DSATUR, random-sequential greedy
│   │   ├── heuristic.h
│   │   └── heuristic.cpp
│   ├── preprocessing/           # Clique generation (complement-graph coloring)
│   │   ├── clique_processing.h
│   │   └── clique_processing.cpp
│   ├── cp_coloring/             # Gecode CP model for coloring
│   │   ├── cp.h
│   │   └── cp.cpp
│   ├── master/                  # RMP, MWSS pricing, column pool
│   │   ├── master.h
│   │   └── master.cpp
│   ├── main.cpp                 # Benchmark: DSATUR vs CP-UB (Gecode)
│   └── main_master.cpp          # Column Generation loop (Gurobi)
├── tests/                       # DIMACS .col benchmark instances
├── CMakeLists.txt
└── README.md
```

| Component                  | Role                                                         |
| -------------------------- | ------------------------------------------------------------ |
| `Graph`                    | Adjacency set, complement, DIMACS parser                     |
| `greedy_coloring`          | Largest-degree-first greedy upper bound                      |
| `DSATUR_coloring`          | Brelaz saturation-degree heuristic                           |
| `random_sequential_greedy` | Random-order greedy (used to seed column pool)               |
| `generate_clique`          | Multiple cliques via complement-graph DSATUR + random greedy |
| `ColoringCP`               | Gecode CP model with clique AllDifferent + symmetry          |
| `cp_upper_bound`           | Iterative CP improvement from DSATUR bound downward          |
| `StableColumn`             | Stable set column: vertex list + uint64 bitset               |
| `ColumnPool`               | Dedup-safe stable-set repository                             |
| `RMPSolver`                | Gurobi LP for set-partitioning RMP                           |
| `solve_mwss`               | Gurobi MILP pricing: maximum weight stable set               |

---

## 4. Requirements

| Dependency                        | Version          | Used by                       |
| --------------------------------- | ---------------- | ----------------------------- |
| Windows                           | —                | Platform                      |
| CMake                             | ≥ 3.16           | Build system                  |
| Visual Studio (MSVC)              | 2019 / 2022, x64 | C++17 compiler                |
| [Gecode](https://www.gecode.org/) | 6.2.0            | CP solver (`main`)            |
| [Gurobi](https://www.gurobi.com/) | 13.0.2           | LP/MIP solver (`main_master`) |

**Default install paths** (edit `CMakeLists.txt` to override):

```cmake
set(GECODE_DIR "C:/Program Files/Gecode")
set(GUROBI_DIR "C:/gurobi1302/win64")
```

Gurobi requires a valid license (free academic licenses available at gurobi.com).

---

## 5. Build

Two executables are produced:

| Executable    | Solver | Purpose                                   |
| ------------- | ------ | ----------------------------------------- |
| `main`        | Gecode | CP benchmark across all `.col` instances  |
| `main_master` | Gurobi | Column generation LP on a single instance |

**VS Code (recommended):**

```
Ctrl + Shift + B
```

**Manual CMake:**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Debug build:

```powershell
cmake --build build --config Debug
```

Output paths:

```
build/Release/main.exe
build/Release/main_master.exe
build/Debug/main.exe
build/Debug/main_master.exe
```

---

## 6. Running Experiments

### 6.1 CP Benchmark (`main`)

Runs DSATUR and CP-UB on every `.col` file in a directory and exports results to CSV.

```powershell
.\build\Release\main.exe [tests_dir] [time_limit_sec] [csv_output]
```

| Argument         | Default         | Description                           |
| ---------------- | --------------- | ------------------------------------- |
| `tests_dir`      | `tests`         | Directory of `.col` instances         |
| `time_limit_sec` | `100`           | CP time budget per instance (seconds) |
| `csv_output`     | `benchmark.csv` | Output CSV path                       |

Example:

```powershell
.\build\Release\main.exe tests 60 results.csv
```

### 6.2 Column Generation (`main_master`)

Runs the full column-generation loop on a single instance: initializes the column pool with random greedy colorings, then alternates between solving the RMP LP and calling the MWSS pricing oracle until convergence.

```powershell
.\build\Release\main_master.exe [instance_path] [num_trials] [seed]
```

| Argument        | Default              | Description                                    |
| --------------- | -------------------- | ---------------------------------------------- |
| `instance_path` | `tests/queen5_5.col` | Path to `.col` instance                        |
| `num_trials`    | `20`                 | Random greedy trials for column initialization |
| `seed`          | `40`                 | RNG seed for reproducibility                   |

Example:

```powershell
.\build\Release\main_master.exe tests/myciel4.col 30 42
```

**Expected output:**

```
============================================
 Column Generation
 Instance : tests/myciel4.col
 |V|      : 23
 |E|      : 71
 Trials   : 30
 Init cols: 87
============================================
[iter    0] obj = 3.000000 | reduced_cost = -0.333333 | cols = 88
[iter    1] obj = 3.142857 | reduced_cost = -0.142857 | cols = 89
...
============================================
Converged after N iterations
RMP status: OPTIMAL
LP objective : 4.000000
Lower bound  : 4
Active lambdas: K / M
```

The **LP objective** at convergence is a valid lower bound on $\chi(G)$. `ceil(obj)` gives the integer lower bound from the LP relaxation.

---

## 7. Input Format

DIMACS `.col` format (1-indexed vertices, internally converted to 0-based):

```
c Comment line
p edge <num_vertices> <num_edges>
e <u> <v>
...
```

Example:

```
c myciel3
p edge 11 20
e 1 2
e 1 4
...
```

Sample instances in `tests/`:

| File            | $   | V    | $   | $   | E   | $   | Known $\chi$ |
| --------------- | --- | ---- | --- | --- | --- | --- | ------------ |
| `myciel3.col`   | 11  | 20   | 4   |
| `myciel4.col`   | 23  | 71   | 5   |
| `myciel6.col`   | 95  | 755  | 7   |
| `queen5_5.col`  | 25  | 160  | 5   |
| `queen8_8.col`  | 64  | 1456 | 9   |
| `DSJC125.9.col` | 125 | 6961 | ≥44 |

---

## 8. Output

### CP Benchmark CSV

```
instance,n,m,clique_lb,dsatur_k,dsatur_time,dsatur_valid,cp_k,cp_time,cp_stopped,cp_valid
```

| Column       | Meaning                                          |
| ------------ | ------------------------------------------------ |
| `clique_lb`  | Max clique size found — lower bound on $\chi(G)$ |
| `dsatur_k`   | DSATUR upper bound                               |
| `cp_k`       | Best CP upper bound (≤ `dsatur_k` if improved)   |
| `cp_stopped` | `1` if CP hit the time limit                     |
| `*_valid`    | Coloring validity check                          |

**Interpreting results**: if `cp_k == clique_lb`, the instance is solved to optimality. If `cp_stopped == 1` and `cp_k > clique_lb`, there remains a gap that needs more time or a tighter lower bound.

### Column Generation Terminal

Each iteration prints:

- `obj`: current RMP LP objective value (lower bound on $\chi$)
- `reduced_cost`: $1 - \text{MWSS weight}$ for the best new column; negative means improving column found
- `cols`: total columns in RMP

At convergence, `ceil(LP objective)` is printed as the LP lower bound.

---

## 9. Implementation Notes

**Clique finding** (`generate_clique`): a stable set in the complement graph $\bar{G}$ is a clique in $G$. The function runs DSATUR on $\bar{G}$ to find an initial large clique, then runs randomized greedy colorings on $\bar{G}$ for `number_cliques - 1` additional cliques. All cliques are sorted by size descending. The largest clique is used for CP symmetry breaking; all cliques become `AllDifferent` constraints.

**Stable set maximalization** (`maximalize_stable_set`): after extracting color classes from a greedy coloring, each class is extended greedily by scanning vertices not adjacent to any current member. This produces larger columns and a better-quality initial pool, reducing the number of CG iterations needed.

**Bitset representation**: each `StableColumn` stores a `vector<uint64_t>` bitset (one bit per vertex, 64 vertices per word). Deduplication in `ColumnPool` hashes this bitset with a FNV-inspired mix (`BitsetHash`), giving O(n/64) insert and lookup.

**RMP column addition**: Gurobi columns are added incrementally via `GRBColumn::addTerm`, linking each column variable to the relevant vertex coverage constraints. No model rebuild needed — `model.update()` is called implicitly by the next `optimize()`.

**Time budget in CP**: `cp_upper_bound` splits the global time limit adaptively. Each sub-call receives `min(remaining, max(5s, remaining/3))`, preventing any single k-value attempt from consuming the full budget while still giving sufficient time to the first few attempts.

**Known limitations**:

- MWSS pricing solves an NP-hard problem exactly via Gurobi MILP, which is the computational bottleneck. For large graphs, heuristic pricing (e.g., greedy or tabu search on the complement) would be faster but may miss improving columns.
- The column generation currently solves only the LP relaxation. Obtaining an integer solution requires branching (branch-and-price), which is not yet implemented.
- CP search is depth-first with no restarts; adding LNS or restart strategies may improve performance on hard instances.

---

## 10. References

1. Brelaz, D. (1979). _New methods to color the vertices of a graph_. Communications of the ACM, 22(4), 251–256.
2. Mehrotra, A., & Trick, M. A. (1996). _A column generation approach for graph coloring_. INFORMS Journal on Computing, 8(4), 344–354.
3. Trick, M. A. (2003). _A dynamic programming approach for consistency and propagation for knapsack constraints_. Annals of Operations Research, 118(1), 73–84.
4. Van Hoeve, W.-J. (2001). _The alldifferent constraint: A survey_. Sixth Annual Workshop of the ERCIM Working Group on Constraints.
5. Schulte, C., Stuckey, P. J. (2008). _Efficient constraint propagation engines_. ACM TOPLAS, 31(1).
6. Gecode Team. _Gecode: Generic Constraint Development Environment_. [https://www.gecode.org/](https://www.gecode.org/)
7. Gurobi Optimization. _Gurobi Optimizer Reference Manual_. [https://www.gurobi.com/](https://www.gurobi.com/)

---

## License

Apache License 2.0. See `LICENSE` for details.
