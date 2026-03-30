# PTL — Periodic Task Layer for FreeRTOS

> A real-time scheduling extension for FreeRTOS adding **periodic tasks**, **deadline enforcement**, **overrun policies**, and a **polling server** for aperiodic workloads — all running on QEMU Cortex-M3.

Built on top of FreeRTOS kernel commit [`5424d9d`](https://github.com/FreeRTOS/FreeRTOS-Kernel), as part of the *Computer Architecture and Operating Systems* course — Cybersecurity Engineering M.Sc. — Politecnico di Torino.

**Authors:** Riccardo Saccu, Enrico Parigi, Giovanni Capasso, Giada Rinaudo, Giuseppe Nardella, Umberto Catania

**Repository:** `https://github.com/parigi-182/freeRTOS-caos-scheduler`

---

## Table of Contents

- [What is PTL?](#what-is-ptl)
- [Architecture](#architecture)
  - [The PTL Singleton](#the-ptl-singleton)
  - [Task Wrapper Model](#task-wrapper-model)
  - [Scheduler Integration — Inside `task.c`](#scheduler-integration--inside-taskc)
  - [Polling Server](#polling-server)
- [Key Design Decisions](#key-design-decisions)
- [API Reference](#api-reference)
  - [Initialization & Start](#initialization--start)
  - [Configuration Structures](#configuration-structures)
  - [Aperiodic Task Submission](#aperiodic-task-submission)
- [Overrun Policies](#overrun-policies)
- [Timing Model](#timing-model)
- [Trace & Statistics](#trace--statistics)
- [Build & Run](#build--run)
- [Test Suite](#test-suite)
- [Project Structure](#project-structure)
- [License](#license)

---

## What is PTL?

FreeRTOS is a capable preemptive RTOS, but it has no native concept of **periodicity** or **deadlines**. PTL (Periodic Task Layer) is a thin, minimally intrusive extension that adds exactly that.

With PTL you declare tasks with a period `T` and a deadline `D`. The scheduler — embedded directly inside FreeRTOS's `task.c` tick handler — releases each task at the right moment, detects deadline misses when a job finishes late, and handles overruns (a new release arriving while the previous job is still running) according to a configurable policy.

On top of periodic tasks, PTL adds a **Polling Server**: a special periodic task that drains a FIFO queue of aperiodic (event-driven) jobs during its budget window, keeping aperiodic work from interfering with hard real-time guarantees.

Everything is configured **declaratively** — you describe the task set upfront, call `vPtlInit()` and `vPtlStart()`, and PTL handles the rest.

---

## Architecture

### The PTL Singleton

The entire PTL runtime state lives in a single heap-allocated structure `Ptl_t *pxSys`. It is a singleton: calling `vPtlInit()` twice is a no-op (guarded by a NULL check). It owns:

- a flat array of `PtlTask_t` control blocks (one per periodic task, plus one for the polling server at index 0);
- a pointer to the `PtlPollingServer_t` runtime block;
- global statistics and the simulation duration.

Each `PtlTask_t` is a **PTL-level TCB** that shadows the FreeRTOS TCB. The link between them is a `void *pxPtlTask` field added to FreeRTOS's internal `tskTCB` struct, set via `vTaskSetPtlInScheduler()` at creation time.

```
pxSys  ──►  Ptl_t
             ├── pxTasks[]  ──►  PtlTask_t[0]  (Polling Server)
             │                   PtlTask_t[1]  (T1)
             │                   PtlTask_t[2]  (T2)
             │                   ...
             └── pxPollingServer ──►  PtlPollingServer_t
                                       ├── xJobQueue  (FreeRTOS Queue)
                                       └── pxPtlPollingTCB ──► PtlTask_t[0]
```

### Task Wrapper Model

The user provides only the **job body** — a plain `void f(void*)` function. PTL wraps it inside `vPtlTaskWrapper`, which implements the following lifecycle for each job:

```
ulTaskNotifyTake()        ← blocks until the tick hook releases this job
  │
  ▼
record xJobFinished = pdFALSE, snapshot xLastSwitchedInTime
  │
  ▼
call user entry function  ← actual work happens here
  │
  ▼
finalize xCurrentExecTime, check deadline miss
  │
  ▼
xJobFinished = pdTRUE
  │
  ▼
ulTaskNotifyTake()        ← blocks again, waiting for next release
```

The task never manages its own timing. It simply does work and blocks. All release decisions are made inside the tick interrupt.

### Scheduler Integration — Inside `task.c`

PTL hooks into **two points** of `task.c` with `#if (configUSE_PTL_SYSTEM == 1)` guards, keeping all modifications strictly conditional and easy to audit.

**`xTaskIncrementTick()` — the tick hook (runs every tick):**

This is the heart of PTL. On every tick it does, in order:

1. **Execution time accounting (polling server).** If the currently running task is the polling server, accumulate elapsed ticks into `xCurrentExecTime` and check whether the active aperiodic job has exceeded its soft deadline — triggering a kill flag if the job's policy is `eAperiodicKill`.

2. **Server period replenishment.** If `xConstTickCount >= pxServerPtl->xNextArrivalTime`, reset `xCurrentExecTime` to zero, advance `xNextArrivalTime`, and unblock the server via `vTaskNotifyGiveFromISR()`.

3. **Server budget exhaustion.** If the server is running and has consumed `>= xMaxBudget` ticks, it is moved directly to the suspended list and a context switch is requested — no `vTaskSuspend()` call, just direct list manipulation for minimal overhead.

4. **Periodic task release loop.** For every periodic task (skipping index 0, the server), if `xConstTickCount >= xNextArrivalTime`:
   - If the previous job **finished** (`xJobFinished == pdTRUE`): normal release — reset exec time, advance deadlines, call `vTaskNotifyGiveFromISR()`.
   - If the previous job **did not finish** (`xJobFinished == pdFALSE`): **overrun detected** — apply the global policy (SKIP / KILL / CATCH_UP).

5. **Simulation end.** If `xConstTickCount >= xSimDuration`, call `vPtlExit()` to dump the trace and halt.

**`vTaskSwitchContext()` — context switch hooks:**

- **Switched-out hook:** When a task is preempted, `xCurrentExecTime += (xTickCount - xLastSwitchedInTime)` accumulates the time slice just consumed. Idle task time is tracked separately into `xGlobalStats.xIdleTime`.

- **Switched-in hook:** `xLastSwitchedInTime` is checkpointed to the current tick. Additionally, if `xJobKilled == pdTRUE`, the task's stack is **rewritten in place** using `pxPortInitialiseStack()` — redirecting execution to `vPtlTaskWrapper` (for periodic tasks) or `vKillAperiodicHandle` (for the polling server). This is how KILL is implemented: no `vTaskDelete`, no `vTaskSuspend` — the stack frame is simply replaced on the next context switch into the task, and it wakes up at the top of its wrapper as if starting fresh.

### Polling Server

The polling server is created as a regular PTL periodic task at index 0, with no deadline. Its period serves two purposes: **budget replenishment** (every `xPeriod` ticks, `xCurrentExecTime` resets to zero and the server is unblocked) and **restart after an empty queue** (the server blocks on `ulTaskNotifyTake()` when idle, and is woken by the next replenishment tick).

Each server activation calls `vPollingServerBody()`, which loops over the FreeRTOS queue dequeuing and executing aperiodic jobs one at a time, stopping when either the queue is empty or the budget (`xMaxBudget`) is exhausted.

Aperiodic job kill works through the same stack-rewrite mechanism: when the tick hook detects a soft deadline overrun with policy `eAperiodicKill`, it sets `xJobKilled = pdTRUE` on the server's PTL TCB. On the next context switch into the server, the stack is rewritten to jump to `vKillAperiodicHandle`, which updates statistics and then either drains the remaining budget or loops back to the wrapper.

---

## Key Design Decisions

**Declarative, not imperative.** The entire task set is described once in a `PtlConfig_t` struct before the scheduler starts. There are no runtime task creation calls after `vPtlStart()`. This makes the schedule statically analysable and behaviour reproducible.

**Minimal kernel intrusion.** All PTL logic is gated behind `configUSE_PTL_SYSTEM`. Only two functions in `task.c` are touched, and only within guarded blocks. The rest of FreeRTOS is unchanged. Porting to a new FreeRTOS version means re-applying two well-defined patches.

**Stack rewriting instead of task deletion.** Killing a job by rewriting its stack avoids the overhead and complexity of `vTaskDelete` / `xTaskCreate` cycles at runtime. The FreeRTOS TCB, stack allocation, and priority assignment are all preserved; only the program counter and saved registers are reset.

**Notification-based unblocking.** Tasks block on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` between jobs. The tick hook unblocks them via `vTaskNotifyGiveFromISR()`. This is the recommended FreeRTOS pattern for lightweight synchronisation with no queue overhead.

**Polling server as a first-class periodic task.** The server lives at index 0 in the same `pxTasks` array and participates in FreeRTOS priority-based preemption normally. If a higher-priority periodic task becomes ready, it preempts the server mid-budget. Budget accounting remains correct because execution time is charged per context switch, not per activation.

---

## API Reference

### Initialization & Start

```c
void vPtlInit(PtlConfig_t *conf);
```
Allocates the PTL singleton, creates all periodic tasks and the polling server as suspended FreeRTOS tasks, and links each one to its PTL TCB. Must be called before `vTaskStartScheduler`.

```c
void vPtlStart(void);
```
Records `t₀` and calls `vTaskStartScheduler()`. All tasks start together at `t₀` — the first release of every task is one period after `t₀`.

```c
void vPtlExit(void);
```
Disables interrupts, dumps the trace log and statistics over UART, calls `vTaskEndScheduler()`, then spins forever. Called automatically when `xSimDuration` is reached.

---

### Configuration Structures

```c
typedef struct {
    uint8_t                   ucNTasks;       // Number of periodic tasks (excluding server)
    PtlTaskParams_t          *pxTasks;        // Array of task descriptors
    TickType_t                xSimDuration;   // Simulation length in ticks
    PtlOverrunPolicy_t        eGlobalPolicy;  // ePtlPolicySkip | ePtlPolicyKill | ePtlPolicyCatchUp
    PtlPollingServerConfig_t *pxPollingServerConfig;
} PtlConfig_t;
```

```c
typedef struct {
    const char     *pcName;         // Task name (max 20 chars)
    TaskFunction_t  pxEntry;        // Job body: void f(void*)
    void           *pvParameters;   // Argument passed to job body
    uint32_t        ulStackDepth;   // Stack size in words — use BYTES_TO_WORDS()
    UBaseType_t     uxPriority;     // FreeRTOS priority level
    TickType_t      xPeriod;        // Period T in ticks
    TickType_t      xDeadline;      // Relative deadline D in ticks (0 → D = T)
} PtlTaskParams_t;
```

```c
typedef struct {
    TickType_t   xPeriod;           // Server period T_s in ticks
    TickType_t   xMaxBudget;        // Server capacity Q_s in ticks
    UBaseType_t  uxPriority;        // FreeRTOS priority of the server task
    uint32_t     ulStackDepth;      // Stack size in words
    uint32_t     ulNAperiodicTasks; // Max depth of the aperiodic job queue
} PtlPollingServerConfig_t;
```

**Minimal example:**

```c
static PtlTaskParams_t tasks[] = {
    { "T1", vTask1, NULL, BYTES_TO_WORDS(512), 3, 20, 20 },
    { "T2", vTask2, NULL, BYTES_TO_WORDS(512), 2, 50,  0 }, // D = T = 50
};

PtlPollingServerConfig_t server = {
    .xPeriod           = 10,
    .xMaxBudget        = 3,
    .uxPriority        = tskIDLE_PRIORITY + 1,
    .ulStackDepth      = BYTES_TO_WORDS(512),
    .ulNAperiodicTasks = 8
};

PtlConfig_t conf = {
    .ucNTasks              = 2,
    .pxTasks               = tasks,
    .xSimDuration          = 1000,
    .eGlobalPolicy         = ePtlPolicySkip,
    .pxPollingServerConfig = &server
};

vPtlInit(&conf);
vPtlStart(); // never returns
```

---

### Aperiodic Task Submission

```c
BaseType_t xPtlAddAperiodicTask(
    TaskFunction_t        pxCode,        // Job body
    void                 *pvArgs,        // Argument
    TickType_t            xSoftDeadline, // Relative deadline from arrival time (ticks)
    PtlAperiodicPolicy_t  ePolicy        // eAperiodicKill | eAperiodicOverrun
);
```

Enqueues an aperiodic job. Safe to call from tasks or from a FreeRTOS software timer callback (as used in the demo `main.c`). Returns `pdFAIL` if the polling server has not been initialised. If the queue is full the job is dropped and logged as `AP_DROP`. The `ePolicy` field is **per-job**: you can mix kill and overrun jobs in the same queue.

---

## Overrun Policies

An **overrun** occurs when a new periodic release arrives (`xNextArrivalTime` reached) while the previous job has not yet set `xJobFinished = pdTRUE`.

| Policy | Behaviour |
|---|---|
| `ePtlPolicySkip` | The new job is not released. `xNextArrivalTime` advances normally. The running job finishes undisturbed. Logged as `OVERRUN_SKIP`. |
| `ePtlPolicyKill` | `xJobKilled` is set to `pdTRUE`. On the next context switch into that task, its stack is rewritten to restart `vPtlTaskWrapper` from the top. A new job is immediately released. Logged as `OVERRUN_KILL`. |
| `ePtlPolicyCatchUp` | The new job is released immediately (a second notification is sent). The overrunning job continues running and will be preempted normally. The nominal cadence is maintained. Logged as `OVERRUN_CATCHUP`. |

The policy is **global** — set once in `PtlConfig_t.eGlobalPolicy`.

---

## Timing Model

```
t₀          R₁          R₂          R₃
│           │           │           │
▼           ▼           ▼           ▼
────────────┬───────────┬───────────┬──────────
            │  job 1    │  job 2    │  job 3
            └──────┐    └───┐       └──────┐
                   F₁       F₂             F₃

R_k = t₀ + (k × T)      release time of the k-th job
D_k = R_k + D            absolute deadline
F_k = actual finish time

Deadline miss : F_k > D_k
Overrun       : job k still running at R_{k+1}
```

All times are in **FreeRTOS ticks**. With the default `configTICK_RATE_HZ = 1000`, 1 tick = 1 ms. Release jitter is bounded to ≤ 1 tick, since releases are checked on every call to `xTaskIncrementTick()`.

---

## Trace & Statistics

PTL maintains a **circular RAM buffer** of 2048 `PtlLogTask_t` entries. Every significant scheduler event is written to this buffer inside a critical section. The buffer wraps around when full (oldest entries overwritten). No UART I/O happens during the simulation — logging is entirely in-memory and zero-copy.

At simulation end, `vPtlLogDump()` and `vPtlStatsDump()` print everything over UART.

**Trace event types:**

| Token | Meaning |
|---|---|
| `RELEASE` | Job k of task T_i released |
| `START` | Job began executing |
| `END` | Job completed normally |
| `DEADLINE_MISS` | Job finished after its absolute deadline |
| `OVERRUN_SKIP / KILL / CATCHUP` | Overrun detected, policy applied |
| `SRV_REPLENISH` | Server budget reset at period boundary |
| `SRV_EMPTY` | Server budget exhausted, server suspended |
| `AP_ENQUEUE` | Aperiodic job added to queue |
| `AP_START / AP_DONE` | Aperiodic job started / finished within deadline |
| `AP_DROP` | Job rejected (queue full or already past deadline on dequeue) |
| `AP_OVERRUN` | Aperiodic job finished after its soft deadline (policy: Overrun) |
| `AP_KILL` | Aperiodic job terminated by stack rewrite (policy: Kill) |

**Example trace output:**
```
===== PTL EVENT TRACE =====
Time(tk)   Task  Job     Event
==========================================
[     0]   T1      1       RELEASE
[     0]   T1      1       START
[     5]   T1      1       END
[    10]   T1      2       RELEASE
[    10]   T2      1       RELEASE
[    10]   T1      2       START
[    12]   T2      1       START
[    25]   T2      1       DEADLINE_MISS
[    30]   T1      4       OVERRUN_SKIP
```

**Per-task statistics** include total jobs released, deadline misses, overruns, WCET, and average execution time. **Polling server statistics** include total aperiodic jobs released, executed, dropped, killed, and total aperiodic CPU time. **Global statistics** include total busy time and CPU load percentage.

---

## Build & Run

**Prerequisites:**
- `arm-none-eabi-gcc` toolchain
- `qemu-system-arm` ≥ 7.x
- GNU Make

**Clone and build:**
```bash
git clone [your-repo-url]
cd [repo-root]
make TEST_ID=1          # build test scenario 1 (TEST_HEALTHY)
```

**Run on QEMU:**
```bash
make qemu_start         # run main app, serial output to stdout
make qemu_start_test    # run the Unity test suite
```

**Debug with GDB:**
```bash
make qemu_debug         # launch QEMU waiting for GDB on port 1234
# in a second terminal:
make gdb_arm
```

**Select a test scenario** by passing `TEST_ID` at build time (1–11):

| ID | Name | Description |
|---|---|---|
| 1 | `TEST_HEALTHY` | 88% load, 6 tasks, stable baseline |
| 2 | `TEST_QUEUE_FLOOD` | Burst > server capacity, tests queue drop |
| 3 | `TEST_OVERRUN_POLICY` | Job cost > budget, Kill vs Overrun comparison |
| 4 | `TEST_SERVER_STARVATION` | Server at low priority, starved by periodic tasks |
| 5 | `TEST_TOTAL_MELTDOWN` | 140% load, deterministic failure scenario |
| 6 | `TEST_STRESS_BOUNDARY` | 96% load, edge of schedulability |
| 7 | `TEST_RR_DANGER` | Round-robin pathology at equal priority |
| 8 | `TEST_CONTEXT_SWITCH_HELL` | 10 tiny tasks, context-switch overhead stress |
| 9 | `TEST_SERVER_AGGRESSION` | High-frequency server constantly interrupts periodic task |
| 10 | `TEST_EFFICIENCY_LIMIT` | 98% load, 2 idle ticks per 100 — overhead margin test |
| 11 | `TEST_WRITTEN` | Reference scenario for written analysis |

**Target:** QEMU `mps2-an385` machine, Cortex-M3, bare-metal (no host OS required).

---

## Test Suite

The test suite (`test/test.c`) is built separately with `make test` and uses the **Unity** framework. It runs on the same QEMU target as the main application and produces human-readable pass/fail output on UART, suitable for regression checking in CI.

Coverage includes stress tests (overlapping hard real-time tasks, oversubscribed CPU), edge-case tests (minimal time gaps, single-tick budgets), preemption consistency checks, and policy correctness verification.

```
Test 1 – Healthy Baseline:         PASSED
Test 2 – Queue Flood Drop:         PASSED
Test 3 – Overrun Kill Policy:      PASSED
Test 4 – Server Starvation:        PASSED
Test 5 – Total Meltdown (expect):  PASSED
```

---

## Project Structure

```
.
├── FreeRTOS/FreeRTOS/Source/
│   ├── tasks.c                        ← modified: PTL hooks in tick & context switch
│   ├── PtlProject/Source/
│   │   ├── ptl.c                      ← vPtlInit, vPtlStart, vPtlTaskWrapper, vPtlExit
│   │   ├── polling_server.c           ← server body, aperiodic kill handler
│   │   ├── ptl_log.c                  ← trace buffer dump, statistics dump
│   │   ├── uart.c                     ← bare-metal UART output
│   │   └── include/
│   │       ├── ptl.h                  ← public API (user-facing)
│   │       ├── ptl_pi.h               ← internal structures (PtlTask_t, Ptl_t, …)
│   │       ├── ptl_log.h              ← event enum, log macros, buffer declaration
│   │       └── polling_server_pi.h    ← polling server internals
├── main.c                             ← test scenario selector and task definitions
├── test/
│   ├── test.c                         ← Unity test suite
│   └── unity/                         ← Unity framework source
├── startup.c                          ← Cortex-M3 startup / vector table
├── mps2_m3.ld                         ← linker script
└── Makefile
```

---

## License

This project is a fork of the FreeRTOS kernel and is distributed under the **MIT License**, consistent with FreeRTOS licensing requirements. Every source file carries the standard FreeRTOS license header. See `LICENSE` for the full text.

---

*Politecnico di Torino — Computer Architecture and Operating Systems — Cybersecurity Engineering M.Sc.*



*AI Transparency: Generative AI assisted only in the writing of this README. All development and implementation were completed independently .*
