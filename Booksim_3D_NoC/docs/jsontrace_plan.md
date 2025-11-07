# JSON Trace Workload – Feature & Implementation Plan

## Goal
Add first-class support for replaying JSON trace files (for example `traces/flash_attn_base.jsonl`) through BookSim’s `WorkloadTrafficManager`, including an optional mapping from sparse trace node IDs to the simulator’s dense node index space.

## Scope
- Introduce a new workload type `jsontrace(...)` that can be selected via the existing `workload` configuration string.
- Stream `.jsonl` traces and emit whole-packet descriptors without buffering the entire trace.
- Accept an optional node-ID mapping file so traces with IDs up to 268 can run on smaller topologies (e.g., 64-node meshes).
- Ship a sample map plus documentation explaining how to run both the synthetic and trace-driven workloads and how to interpret their outputs.

## Implementation Steps

1. **Configuration Plumbing**
   - Extend `booksim_config.cpp` with `jsontrace_file`, `jsontrace_map_file`, `jsontrace_limit`, `jsontrace_scale`, and a `trace_driven` flag (default `0`).
   - Update `Workload::New` in `workload.cpp` so `workload=jsontrace` (with or without parentheses/braces) is accepted, falling back to the `jsontrace_*` config keys whenever parameters are omitted.
   - Recognize `sim_type=trace` in `main.cpp`, automatically switch to the workload traffic manager, enable `trace_driven`, and disable warm-up/sample loops for trace replays.

2. **Mapping & Trace Reader**
   - Create `jsontrace_loader.{hpp,cpp}` with a lightweight parser that:
     - Loads an optional JSON dictionary mapping (original ID → simulator ID) and validates mapped IDs are within `[0, _nodes)`.
     - Streams the `.jsonl` trace line by line, tracking flits until `tail=true` to emit `JsonTracePacket {time, source, dest, size, class, packet_id}` records. Only one packet is buffered at a time and multicast records that use `dest_ids` are replicated into one unicast packet per destination (with a one-time warning).
     - Provides `NextPacket` and `Reset` helpers plus descriptive error messages for malformed traces or unmapped IDs.

3. **`JsonTraceWorkload`**
   - Implement a new workload class (modeled after `TraceWorkload`) that:
     - Maintains per-source queues, `_pending_nodes`, and `_deferred_nodes`.
     - Requests packets from the reader, enqueues any whose (scaled) timestamps match the current simulation time, and exposes `dest/size/time` to `WorkloadTrafficManager`.
     - Offers `printStats` summaries (`packets_read`, pending queues, whether future packets remain).

4. **Artifacts & Documentation**
   - Add `src/traces/flash_attn.map.json`, mapping the 38 IDs present in `flash_attn_base.jsonl` to a contiguous 0–37 range so it fits within the 64-node 3D mesh.
   - Document how to run:
     - Synthetic baseline (`./booksim injection_rate=1e-4 examples/.../m3`).
     - Trace workload (`./booksim examples/.../m3 sim_type=trace workload=jsontrace jsontrace_file=… jsontrace_map_file=…`), including explanations of the new command-line overrides.
   - Explain how to inspect the “Overall …” block in the logs and what to expect from synthetic (steady) vs. trace-driven (bursty) runs.
   - Capture production notes: keep large traces out of git (or use LFS), version-control map files, and add regression scripts where practical.

5. **Validation**
   - Rebuild BookSim, rerun the synthetic workload to ensure behavior matches previous baselines.
   - Run the JSON trace workload (possibly with a reduced `sim_count`) to confirm packets inject correctly and that the mapper prevents out-of-range node references.

This document doubles as the high-level design reference so future contributors can confirm that the implementation matches the intended behavior.
