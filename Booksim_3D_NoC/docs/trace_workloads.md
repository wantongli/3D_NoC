# Trace-Driven Workloads

This guide explains how to run both the stock synthetic workload and the new JSON trace-driven workload, how to inspect their outputs, and what behavior to expect from each. It also captures the practical bits that help keep the repository in production shape.

---

## 1. Running the Synthetic Baseline

```
cd Booksim_3D_NoC/src
./booksim examples/3D_Mesh_BFT/1_3D_Mesh/m3 injection_rate=0.0001 > synthetic_run.log
```

- **What it does**: Spins up the enlarged 7×7×7 mesh (343 nodes, enough to cover every trace ID), injects uniform Bernoulli traffic at 1e-4 packets/cycle (packet size 5), and logs stats once per `sample_period`. Expect packet latency in the low 50-cycle range because packets are tiny and load is uniform.
- **Where to look**: Scroll to the “Overall …” block at the end of the log (`rg -n "Overall" synthetic_run.log`). You should see steady latencies in the low 30-cycle range, nearly identical injected/accepted rates, and minimal fragmentation—exactly what a light, uniform synthetic load should produce.

---

## 2. Running the JSON Trace

Before running, decompress whichever trace you plan to use (the repo only ships the compressed `.jsonl.xz` blobs to stay under GitHub’s 100 MB limit):

```
cd Booksim_3D_NoC/src
xz -dk traces/flash_attn_base.jsonl.xz    # or flash_attn_sample.jsonl.xz
./booksim examples/3D_Mesh_BFT/1_3D_Mesh/m3 \
  sim_type=trace \
  workload=jsontrace \
  jsontrace_file=traces/flash_attn_base.jsonl \
  jsontrace_map_file=traces/flash_attn.map.json \
  jsontrace_limit=-1 > trace_run.log
```

- **Parameters**:
  - `sim_type=trace` automatically switches to the trace-friendly traffic manager, turns off warm-up/sample phases, and runs until the trace completes.
  - `workload=jsontrace` selects the JSON-trace workload and instructs it to pull filenames from the `jsontrace_*` options.
  - `jsontrace_file` / `jsontrace_map_file` are passed as normal command-line overrides—no parentheses or braces required. The bundled `flash_attn.map.json` is an identity map that covers every node ID present in the trace (0…271). For quick turnaround you can point `jsontrace_file` at `traces/flash_attn_sample.jsonl` (decompress the `.xz` first) or append `trace_packet_limit=N` (or the legacy `jsontrace_limit=N`) to stop after N packets regardless of file size.
  - `traces/flash_attn_base.jsonl` – the truncated flash-attention trace produced by gunzipping/xz-decoding the shipped `.jsonl.xz`.
  - `traces/flash_attn.map.json` – identity mapping for every node ID used in the trace (0–271) so it drops directly onto the 7×7×7 mesh.
  - Optional extras:
    - add `jsontrace_limit=N` to stop after N packets (handy for quick smoke tests),
    - add `jsontrace_scale=S` to divide timestamps (`S=2` halves every `t` value before scheduling).
- **What to expect**:
  - Arrival bursts: Multiple packets often share the same timestamp and source after mapping, so the instantaneous injection rate spikes.
  - Congestion pockets: Average packet latency climbs into the tens of thousands of cycles because each trace packet carries 4 096 flits and packets queue behind one another on the same sources.
  - Throughput tied to the trace: The accepted packet/flit rate in the “Overall …” block mirrors the trace offers, not `injection_rate`.
- **Verifying success**:
  - Tail of `trace_run.log` should list `Trace packets consumed = …` and `Packets pending injection = 0` once the run drains. Search with `rg -n "Trace packets" trace_run.log`.
  - Grep for `WARNING` to ensure no assertion tripped.

---

## 3. Inspecting Results Quickly

```
rg -n "Overall" synthetic_run.log | head
rg -n "Overall" trace_run.log | head
```

Compare the `Overall average packet latency`, `accepted packet rate`, and `average hops` lines between the two logs. Synthetic traffic should stay flat; the trace should show more variability and potentially higher hop counts if mapped nodes favor longer routes.

---

## 4. Maintaining Trace + Map Assets

- **Map files**: Keep `src/traces/*.map.json` in sync with whichever topology you simulate. Each entry is simply `"original_id": mapped_id`. Mapped IDs must be `< nodes` or the loader will stop with an explanatory error.
- **Trace storage**: Large `.jsonl` traces are best left outside the repo or tracked with Git LFS. If you regenerate a trace, document the provenance and keep a note of the packet count in `docs/trace_workloads.md`.
- **Regression sanity**: For CI or quick smoke tests, drop `sim_count` and `sample_period` in `m3` so both workloads complete in seconds. The JSON trace loader respects the `jsontrace_limit` config knob, which you can set to a small number in automated runs.

---

## 5. Reading the Logs

Every `WorkloadTrafficManager` run prints three useful regions:

1. **Per-sample block** – instantaneous latency/throughput sampled every `sample_period`.
2. **Power summary** – Orion’s power breakdown (unchanged by the new workload type).
3. **Overall stats** – aggregated min/avg/max for latency, injection/acceptance rates, and packet lengths.

The JSON trace workload adds a short section at the end of each class’s stats:

```
Trace packets consumed = 95
Future packets buffered = 0
Packets pending injection = 0
```

Confirm the “pending” value returns to zero before ending the run—otherwise the trace stopped early (likely due to a `jsontrace_limit` or a configuration mismatch).

---

## 6. Production Tips

- Keep the feature plan (`docs/jsontrace_plan.md`) up to date as you expand trace support.
- Note any assumptions about trace format (currently JSON lines with `head`, `tail`, `flits_in_packet`, `dest_id`/`dest_ids`, etc.) so future traces can be validated quickly. When a trace uses `dest_ids` for multicast, the simulator now replicates the packet once per mapped destination and logs a warning the first time it happens.
- When adding new traces, generate their node-ID histogram (see `python3 scripts/trace_stats.py` in progress) and create a companion map file in the same directory.

With these steps in place you can swap between synthetic and real workload experiments without touching the simulator code, and reviewers can audit both the plan and the implementation with minimal friction.
