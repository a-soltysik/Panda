# Automated benchmark specification

Accepted benchmark specification. The smoke executable is implemented; full profiles
remain planned, and timing budgets remain provisional until measured on the reference
system.

## Execution model

Provide a standalone `panda-bench`, enabled with `PANDA_BUILD_BENCHMARKS=ON` and
disabled by default. The smoke profile is implemented; reference profiles remain
planned. Profiles select settings at runtime; changing window extent, duration,
workload size, or output directory must not require recompilation.

The current smoke profile accepts:

```text
panda-bench --profile smoke --output results --width 1280 --height 720 --warmup-seconds 1 --measure-seconds 3 --runs 1
```

Smoke profile version 2 creates a standard unit cube and uploads it once before the
warmup and measurement phases. Every frame in both phases renders the same static
`RenderView` with one mesh draw; geometry generation and upload are part of startup,
not the per-frame workload. The JSON workload is `renderStaticCube`. Smoke profile
version 1 rendered an empty view, so its results are not comparable with version 2.
This one-draw workload checks the live renderer path but is not a representative scene
benchmark. With a zero-second warmup, the first measured frame may include first-use
renderer or pipeline setup.

`--width` and `--height` set the requested window extent in pixels. The defaults are
1280 by 720; the report records both the requested and effective framebuffer extents.
Development and quality presets use Debug and are for functional smoke checks only.
Use an optimized Release build for performance comparisons. Use RelWithDebInfo when
profiling needs symbols, and treat those results as a separate build configuration.
Compare benchmark runs only when build type and compiler settings match.
The `reference-1080p` profile remains a design target, not an available command.
Engine logs are written to `panda-bench.log` in the selected output directory;
the completion summary stays on the console and prints the paths to both the JSON
report and engine log.
The smoke report obtains the host CPU model through Panda's public `getHostInfo()`
API and the selected Vulkan driver's name, description and vendor-specific version
through `Context::getDeviceInfo()`. Environment values that cannot be collected are
omitted from JSON.

The full reference run automatically:

1. Validates the environment/capabilities and reports effective settings.
2. Loads assets and prepares required shaders/pipelines.
3. Resets the case to its specified seed/state and warms up for 5 seconds.
4. Measures for 30 seconds without user timing or manual sample collection.
5. Drains the outstanding measurement results after the timed interval.
6. Repeats the reset/warmup/measurement procedure for three runs.
7. Writes reports and exits with a clear outcome.

Display progress and remaining time. A short smoke profile is separate from the full
reference profile; a long benchmark is not part of every ordinary build or test run.
Use the same renderer and synchronization path as applications, not a special fast
path that bypasses production behavior.

`panda-bench` is an ordinary Panda API consumer. It enables
`ContextOptions::enableFramePerformanceSampling`, obtains the Context-owned
`PerformanceManager` through `Context::getPerformanceManager()`, checks the public
capabilities, and drains samples through `takeAvailableSamples()`. Applications use
the same option and API; the benchmark has no private route to timestamp data.

## Public performance collection

Every valid Context exposes a `PerformanceManager`, including a headless Context.
Frame performance sampling is disabled by default. Set
`ContextOptions::enableFramePerformanceSampling` to enable it. `getCapabilities()` reports
both whether the selected graphics queue supports timestamp queries and whether
sampling is enabled for this Context. The manager handles counter wrapping and
converts device ticks to nanoseconds internally. A queue without timestamp support
reports the capability as unavailable, even if sampling was requested.

`getLatestSample()` peeks at a copy of the highest-frame-index sample currently ready
and leaves it buffered. `takeNextSample()` removes and returns the lowest-frame-index
ready sample, allowing FIFO processing one frame at a time. `takeAvailableSamples()`
removes and returns all currently ready samples in frame-index order. These calls
check query availability without waiting; each can return an empty result when
sampling is disabled, unsupported, or no sample is ready yet. Samples stay buffered
until a `take` method returns them, so long-running applications should drain samples
regularly.

Each sample pairs `cpuFrameDuration` with `gpuExecutionDuration` for the same
submitted frame. Both are returned in nanoseconds. `cpuFrameDuration` is host
wall-clock time from the start of Panda's frame call through its submission and
presentation call. It includes CPU-side acquisition, waits, command recording and
submission, so it exposes stalls that can limit frame throughput even when GPU time
is short. It excludes application work outside Panda's call and display latency; it
is elapsed time, not CPU execution time. `gpuExecutionDuration` is GPU time between
the frame's GPU timestamp markers. It excludes CPU work and presentation latency.
The paired sample becomes available when its GPU timestamp queries are ready.
Disabled sampling or unsupported timestamp queries produce no paired samples;
callers inspect capabilities instead of interpreting missing data as zero.

Vulkan timestamp results live in query pools. Panda creates one pool for each of its
two in-flight frame slots with `queryType = eTimestamp` and `queryCount = 2`. The
query type tells Vulkan what each pool entry stores; the recorded `writeTimestamp2`
commands choose the measurement points. Query 0 marks the beginning of the frame's
measured rendering work and query 1 marks its end. Both timestamps use the
color-attachment output stage. A slot's previous result is collected before its pool
is reset for a new frame. Before reading results, the manager nonblockingly confirms
completion of that slot's exact frame submission on the shared timeline. Availability
left by an earlier use of the query slot cannot be attributed to the new frame.
These are GPU-written device timestamps, not CPU stopwatch readings
([`vkCmdWriteTimestamp2`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html)).

The manager requests 64-bit results with an availability value for each query, without
the Vulkan wait flag. If either query is not ready, the sample stays pending. Once
both are ready, the manager subtracts the timestamps with the device counter's wrap
width, multiplies the tick difference by the device's timestamp period, and rounds
the result to nanoseconds. Query results are raw ticks, not nanoseconds;
`VkPhysicalDeviceLimits::timestampPeriod` gives the nanoseconds per tick. For
example, a 40-tick difference with a 2 ns period is returned as 80 ns. The selected
device's period stays internal to the manager, so callers receive
`std::chrono::nanoseconds`. Availability and result layout follow
[`vkGetQueryPoolResults`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html).
The CPU records `writeTimestamp2` commands, but the GPU writes their values when it
executes the submitted command buffer. The later result read requests availability
without the wait flag. CPU command recording, submission, polling and conversion are
outside the measured interval. The value is elapsed GPU device time between the two
markers; GPU-side stalls between them can contribute to that elapsed time.

## Measurement rules

- Measure CPU frame wall time across Panda's complete frame call, including
  CPU-side waits. Keep application solver time, tools, asset loading, and pipeline
  warmup outside that call and report them separately when measured.
- Measure GPU passes with supported timestamp queries and asynchronous result
  collection. Do not add a per-pass or per-frame global wait to obtain timings.
- Report shadow and camera/final-output work; presentation/display latency is a
  separate concern, not an invented GPU rendering duration.
- Do not add CPU and GPU durations as if they were sequential frame time. Report
  observed frame intervals and explicit wait categories separately.
- Buffer samples in memory; perform report/file I/O outside measured intervals.
- Keep all valid timed frames. Do not silently discard slow samples. Resize,
  minimization, or a configuration change invalidates a controlled run.
- Compute cases use fixed specified work per measured frame, not FPS-dependent
  catch-up steps that change the workload being compared.
- Collect timings without external profilers/validation for the baseline. Record
  built-in instrumentation settings; use separate diagnostic capture runs.

The profile/report rules below define sample boundaries and statistics. Keep
per-run results visible rather than hiding disagreement in one number.

## Reference system and provisional budget

Reference hardware: AMD Ryzen 5 7600, NVIDIA RTX 4070 Ti, 1920 x 1080 output.
This is a comparison system, not the engine's minimum hardware requirement.

The reference fixture below defines 100 objects, one million candidate triangles,
32 lights, one 2048-square directional shadow, requested 4x MSAA and 8x anisotropy.

| Metric | Provisional target |
| --- | --- |
| Active CPU render preparation p95, excluding GPU waits | At most 1 ms |
| GPU rendering p95, including shadows and final image work | At most 4 ms |

These are design targets, **not measured results or release guarantees**. They do
not include application simulation, loading, or optional tools. Do not lower quality
silently to meet a number. Record and compare actual effective settings.

## Cases

- A minimal scene to track fixed engine/frame overhead.
- The frozen lit/shadowed reference scene.
- GPU billboard clouds of 10,000, 100,000, and 1,000,000 instances with fixed camera,
  radii, distribution, seed, and an explicit overdraw-stress variant.
- Render-only, Vulkan-update, and CUDA-update variants with a small synthetic
  producer rather than a full fluid simulation.
- Shadow-enabled/disabled variants and mixed Vulkan/CUDA ordering checks.
- Short lifecycle scenarios for pause/resume, replacement, and shutdown; do not
  mix administrative stalls into a steady-state result without labeling them.

For GPU clouds, verify no per-particle CPU scene loop, no per-particle draw call,
no normal-path readback, no repeated interop import, no steady-state pipeline
creation, and no routine global wait. Diagnostic readback in a test is allowed and
must not be mistaken for the application rendering path.

## Reports and comparisons

Produce a concise console summary and a machine-readable JSON report; optional CSV
contains raw samples. Include case/profile/schema version and commit revision,
CPU/GPU, OS, driver, compiler/build mode, requested and effective quality, seeds,
durations, sample counts, per-run statistics, validation/profiler state, and outcome.

Only compare compatible workloads/configurations. Mark incompatible settings or
hardware instead of declaring a false regression/pass. Curated baseline records
and profile definitions belong in Git; raw runs and large captures ordinarily do not.

An unavailable optional CUDA case is a reported skip. If CUDA was explicitly required
for a requested case, fail that request clearly. Never silently substitute Vulkan
and label the result CUDA.

## Tools and questions

| Tool | Use it to answer |
| --- | --- |
| RenderDoc | Which draws, resources, attachments, and shader values produce this frame? |
| Nsight Systems | Where does CPU/GPU time go, and are Vulkan/CUDA queues waiting in the intended order? |
| Nsight Compute | What limits a selected CUDA kernel? |
| Nsight Graphics | What limits the Vulkan rendering/shader workload on supported hardware? |

Choose a tool for a specific question, reproduce the case, inspect evidence together,
then validate an improvement with an unprofiled benchmark. Capturing and replaying
external interop workloads has tool-specific limitations; do not assume one frame
capture reproduces an entire live cross-API simulation.

References: [Vulkan queries](https://docs.vulkan.org/spec/latest/chapters/queries.html),
[RenderDoc](https://github.com/baldurk/renderdoc),
[Nsight Systems](https://docs.nvidia.com/nsight-systems/UserGuide/index.html),
[Nsight Compute](https://docs.nvidia.com/nsight-compute/NsightCompute/index.html),
[Nsight Graphics](https://docs.nvidia.com/nsight-graphics/).

## Reproducible version-one profiles

Profile definitions and the JSON schema are versioned fixtures. Timing values
require measurements, never design estimates.

All cases render at 1920x1080, vertical FOV 60 degrees, near/far 0.05/1000 m,
requested MSAA 4x and AF 8x, HDR/SDR formats from the renderer contract, tools off,
validation off, pipeline warmup before sampling and no input-driven camera motion.
The report records effective formats/quality, actual present mode and why any
requested mode was downgraded. Keep a fixed window extent and no dynamic resolution.
VSync/present waits are separately timed and excluded from active CPU preparation;
GPU timestamps cover submitted shadow, camera and tone-map passes. Compare runs only
with equal effective settings and present mode. A missing optional timestamp feature
marks GPU duration unavailable rather than inventing zero; the GPU timing profile
enables `ContextOptions::enableFramePerformanceSampling`, requires nonzero timestamp
valid bits and fails clearly if unavailable.

The built-in `reference-1080p-v1` fixture generates one indexed static grid mesh:
101 by 51 vertices, 100 by 50 quads, two triangles per quad, therefore exactly
10,000 triangles. Vertex positions span a 1 m XZ square; Y is
`0.05*sin(2*pi*x)*cos(2*pi*z)` meters. It creates 100 entities at integer tile
centers `(i-4.5,0,j-4.5)` for i,j=0..9, hence exactly one million candidate
triangles in the camera pass, before frustum culling. Each has a distinct standard
material with linear tint `(0.35+0.06*k,0.55,0.85-0.05*k,1)`, where
`k=(i+3*j)%10`. All share one generated 512-square sRGB checker texture with
8 by 8 alternating tiles of sRGB `#c4c4c4` and `#404040`, plus a mip chain.
Distinct Material handles
intentionally produce approximately 100 mesh draws; frame diagnostics record actual
camera and shadow draws and culled entities. Camera is (0,12,16), looking at origin.
Lighting is one directional light with a 2048 shadow face and 31 unshadowed point
lights on a radius-7 horizontal ring at y=3: point k=0..30 has position
`(7*cos(2*pi*k/31),3,7*sin(2*pi*k/31))`, linear white color, intensity 2 and
range 8 m. Directional direction is normalized `(-0.4,-1,-0.3)`, linear white
at intensity 3; ambient/defaults follow the
[renderer specification](../design/contracts/rendering.md#first-version-renderer-contract).
These values are fixed by the profile, not generated from driver timing. Test-only geometry avoids external
model version drift. A separate `minimal-v1` profile uses the Studio camera/light,
one cube, and the 10 m Studio ground to track fixed overhead.

`cloud-v1` uses one entity with exactly N=10,000, 100,000 or 1,000,000 slots and
the accepted 16-byte center/radius stream. It omits the optional color stream, so
the defined white default applies; a separate color-stream correctness case uses
the accepted 16-byte linear white color records.
A fixed xorshift32 seed `0x50414e44` fills centers uniformly in [-2,2]^3,
radius 0.03 m and color white. Camera is (0,0,9), looking at origin; a separate
stress variant fills centers in [-1,1]^3 with radius 0.10 m to expose overdraw.
The domain is a Back-face unit primitive cube at the world origin with identity
rotation and uniform scale 4, independently posed from the cloud, and does not cast
shadows. Cloud shadow on/off is a profile parameter. Render-only reuses immutable
buffers; Vulkan-update and CUDA-update write all N positions with the same analytic
offset `x=xInitial+0.05*sin(2*pi*frameIndex/120)` once per measured frame. The GPU
producer must not read back to the CPU. The report records its kernel/dispatch
work and synchronization timings separately. CUDA-unavailable cases skip unless
requested as required. A mixed Vulkan/CUDA ordering case remains a correctness
profile and is not compared as a generic graphics FPS number.

The fixture PRNG updates a nonzero uint32 state by XOR with state shifted left 13,
then XOR with state shifted right 17, then XOR with state shifted left 5, all with
32-bit wrap. Each center coordinate uses the next state's high 24 bits divided by
2^24, mapped linearly to its stated interval. Instance order starts at index zero.
The synthetic update recomputes the same center from index/seed on GPU or reads an
application-owned initial stream; it never consumes the previous rendered position.

Sample one completed submitted frame at a time using asynchronous query retrieval,
with no measurement-time global wait. Its CPU wall duration begins when Panda starts
handling the frame and ends after submission and the presentation call return. This
includes frame-slot waits, image acquisition, command recording, submission and
queuing presentation; it does not include work the application performs outside
Panda's frame call or time until the display shows the image. A skipped frame with
no submitted work has no paired sample. This CPU value can include time when the GPU
is running or when the CPU is blocked, so do not add it to the GPU duration.
A GPU sample runs from the first shadow/camera command timestamp to the final
tone-map command timestamp, plus named per-pass intervals; do not add overlapping
intervals into frame duration.
The measurement timer covers 30 seconds of submissions after each 5-second warmup.
Retain all completed valid frames, including slow ones. Each of three runs resets
fixture state and records its own samples/count/mean/p50/p95/p99/min/max. Quantiles
use sorted nearest-rank index `ceil(q*N)-1`. Also report pooled statistics and
worst-run p95 for comparison; retain per-run values so disagreement is visible.

JSON schema version 3 stores profile/fixture versions and code revision,
OS/compiler/build, CPU/GPU/driver, Vulkan/CUDA availability, requested/effective
settings and formats, image extent, seed, warmup/measurement durations, per-run
paired per-frame CPU/GPU samples and statistics, skipped cases and reasons,
draw/submission counts, waits,
timestamp validity and calibration limitations, validation/profiler state, and
outcome. Raw samples may be saved separately, outside curated source baselines.
Exit codes: 0 complete, 2 unsupported requested capability, 3 invalid controlled
run (resize/minimize/quality change/missing query), 4 setup, backend or report
output failure.
An unavailable optional case is a reported skip with exit 0 for the remaining cases.

First-version acceptance criteria are structural: same production path; no
per-particle CPU loop, draw, readback, import or global wait; no steady-state pipeline
creation; and correct quality settings. The 1 ms CPU / 4 ms GPU p95 values stay provisional
investigation targets, not pass/fail thresholds, until curated runs on the named
reference hardware establish a baseline. A regression comparison requires identical
fixture/schema, requested/effective settings, driver/device class and instrumentation.
If the worst-run p95 differs by more than 10%, flag for investigation and repeat;
do not silently change quality or declare a result statistically significant.
Learning evidence includes a RenderDoc correctness capture and a separate Nsight
trace answering one concrete CPU/GPU or CUDA ordering question.
