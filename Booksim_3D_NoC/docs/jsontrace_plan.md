# JSON Trace Workload – Feature & Implementation Plan

## Summary
Replay JSON trace files through BookSim’s workload traffic manager so recorded workloads can drive the simulator. Traces are read incrementally, packets are emitted only when complete, optional node-ID maps remap trace IDs onto the target topology, and a trace-specific simulation mode reuses the existing WorkloadTrafficManager without the synthetic warm-up loop.

## Architecture

### Configuration and entry point
- `booksim_config.cpp` exposes `jsontrace_file`, `jsontrace_map_file`, `jsontrace_limit`, `jsontrace_scale`, and `trace_driven`.
- When `sim_type=trace` is parsed, `main.cpp` forces `trace_driven=1`, `sim_count=1`, and disables warm-up/sample loops so the trace runs once, end-to-end.
- `Workload::New` recognizes `workload=jsontrace`, parses optional inline parameters, and falls back to config defaults.

### Trace reader (`jsontrace_loader.{hpp,cpp}`)
- Streams `.jsonl` files line by line, tracking flits until `tail=true` and then emitting a `JsonTracePacket {time, source, dest, size, class, packet_id}`.
- Supports optional node-ID maps: JSON dictionaries of `original_id -> mapped_id`. The loader validates every ID in the trace against the map (if provided) or against `_nodes`.
- Handles multicast entries (`dest_ids`) by cloning one packet per destination (with a one-time warning). Keeps at most one packet buffered at a time.

### JsonTraceWorkload (`workload.cpp/.hpp`)
- Mirrors `TraceWorkload`: maintains per-source queues, `_pending_nodes`, and `_deferred_nodes`.
- Pulls packets from the reader, enqueues those whose timestamps are <= current sim time, and exposes `dest()`, `size()`, and `time()` to `WorkloadTrafficManager`.
- `printStats` reports packets consumed, future packets buffered, and per-source pending counts.

### Workload traffic manager (`workloadtrafficmanager.{hpp,cpp}`)
- Stores `_trace_driven`, and when set, skips the warm-up loop: it simply runs `_Step()` until `_Completed()` and then drains.

### Artifacts and docs
- `traces/flash_attn_base.jsonl.xz` / `flash_attn_sample.jsonl.xz`: sample traces kept compressed to stay under Git/GitHub limits.
- `traces/flash_attn.map.json`: example identity map; replace or regenerate as needed to match your trace + topology.
- `docs/trace_workloads.md`: how to run the synthetic baseline and trace workloads, including decompression, packet limits, log inspection, etc.
- `docs/jsontrace_plan.md`: this design summary.

## Validation Workflow
1. Build BookSim and run the synthetic baseline (`./booksim … injection_rate=… examples/.../m3`) to ensure the topology behaves as expected.
2. Decompress a JSON trace (`xz -dk traces/<trace>.jsonl.xz`) and run it with `sim_type=trace workload=jsontrace …`.
3. Confirm `trace_run.log` ends with `Trace packets consumed = …` and `Packets pending injection = 0`.
4. Adjust `trace_packet_limit`/`jsontrace_scale` or the mapping file as needed for other experiments.
