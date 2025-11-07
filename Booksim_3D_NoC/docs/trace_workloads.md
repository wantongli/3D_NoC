# Trace-Driven Workloads

This guide explains how to run the standard synthetic benchmark and any JSON trace-driven workload with BookSim. Follow these instructions when you want repeatable measurements or when you plug in a real trace (e.g., the flash-attention sample that ships with the repo).

---

## 1. Synthetic Baseline

```
cd Booksim_3D_NoC/src
./booksim examples/3D_Mesh_BFT/1_3D_Mesh/m3 injection_rate=0.0001 > synthetic_run.log
```

- The provided `m3` config currently instantiates a 7×7×7 mesh (343 nodes). It injects uniform Bernoulli traffic at 1e‑4 packets/cycle with 5‑flit packets.
- Expect packet latency in the ~50‑cycle range and almost identical injected/accepted rates because the offered load is low and evenly distributed.
- Inspect the “Overall …” block (`rg -n "Overall" synthetic_run.log`) to confirm latency, throughput, and hop-count numbers before running traces.

---

## 2. JSON Trace Replay

1. **Decompress the trace** (the repo only stores compressed `*.jsonl.xz` artifacts to satisfy GitHub’s size limits):
   ```
   xz -dk Booksim_3D_NoC/src/traces/flash_attn_base.jsonl.xz
   # or xz -dk .../flash_attn_sample.jsonl.xz for the short sample
   ```
2. **Run BookSim with the trace workload**:
   ```
   cd Booksim_3D_NoC/src
   ./booksim examples/3D_Mesh_BFT/1_3D_Mesh/m3 \
     sim_type=trace \
     workload=jsontrace \
     jsontrace_file=traces/flash_attn_base.jsonl \
     jsontrace_map_file=traces/flash_attn.map.json \
     trace_packet_limit=-1 > trace_run.log
   ```

Key parameters:
- `sim_type=trace` switches to the workload-aware traffic manager, disables warmup/sample loops, and runs until the trace (or the configured limit) finishes.
- `workload=jsontrace` activates the JSON reader. Use `jsontrace_file` to point at any trace in the documented format. `jsontrace_map_file` should contain a mapping for every node ID referenced in the trace; the bundled `flash_attn.map.json` is an identity mapping that works for the sample trace on the 7×7×7 mesh.
- `trace_packet_limit=N` (or the legacy `jsontrace_limit=N`) stops after `N` packets—handy for quick experiments. `jsontrace_scale=S` divides every timestamp by `S` before scheduling.

What to expect:
- Arrival bursts: multiple packets often share the same timestamp/source, so instantaneous injection spikes.
- Serialization-heavy packets: traces frequently contain 4 096‑flit bursts, so packet latency can jump into the tens of thousands of cycles as packets serialize and queue behind one another.
- Throughput mirrors the trace, not `injection_rate`: the “Overall …” block simply reports what the trace provided.
- Verify success by checking the tail of `trace_run.log` for lines such as `Trace packets consumed = …` and `Packets pending injection = 0`.

---

## 3. Inspecting Results Quickly

```
rg -n "Overall" synthetic_run.log | head
rg -n "Overall" trace_run.log | head
```

Compare the average packet latency, accepted packet rate, and hop-count lines. Synthetic runs should be flat and low-latency; trace-driven runs should show higher variance and much longer packets.

---

## 4. Maintaining Trace + Map Assets

- Keep `src/traces/*.map.json` aligned with your workload. Each entry is a simple `"original_id": mapped_id` pair, and every ID in the trace must be covered.
- Store large traces outside the repo or keep them compressed (`xz -9`) to stay below GitHub’s 100 MB limit. Document the packet count and provenance if you add new traces.
- For CI/smoke tests, use the short sample (`flash_attn_sample.jsonl`) or enable `trace_packet_limit` to cap runtime.

---

## 5. Reading the Logs

Every `WorkloadTrafficManager` run prints:
1. Per-sample stats (latency/throughput sampled every `sample_period`).
2. Power summary (Orion output).
3. Overall stats (min/avg/max latency, rate, packet size, hops).

The JSON trace workload adds a short section per class:
```
Trace packets consumed = …
Future packets buffered = …
Packets pending injection = …
```
If “Packets pending injection” is non-zero when the run ends, the trace stopped prematurely (usually because `trace_packet_limit` was hit).

---

## 6. Production Tips

- Keep `docs/jsontrace_plan.md` updated as you evolve the workflow.
- Validate new traces early (check for required fields, run a histogram of node IDs, regenerate map files as needed).
- Consider adding the log artifacts you create locally (`synthetic_run.log`, `trace_run*.log`) to your personal `.gitignore` if you do not want Git to surface them.

With these steps in place you can swap between synthetic and trace-driven studies without modifying the simulator code, and reviewers can reproduce both workloads quickly.
