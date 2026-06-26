# Dynamic Shortest-Path Routing

**Grupo 31** — Bosque, Lissandro; Galassi, Franco; Pairetti, Joaquín  
*Networks and Distributed Systems — FaMAF, Universidad Nacional de Córdoba, 2026*

---

## Overview

This project simulates the network layer of an 8-node ring in OMNeT++. The cátedra provided a *kickstarter* baseline with a brutally simple routing rule: always forward through `lnk[0]` (clockwise), regardless of the destination. Every packet from `node[2]` to `node[5]`, for example, travels five hops clockwise when the optimal path is only three hops counterclockwise — and all traffic from all sources funnels through the same arc, leaving half the ring's capacity permanently idle while the merge point saturates.

We redesigned the network layer to discover the topology dynamically via `HELLO` control packets and route each packet through whichever direction yields the fewest hops. The constraint: no use of OMNeT++ introspection APIs (`cTopology`, `cGate` iterators) — every node starts blind and must learn the network purely from the packets it receives.

---

## Algorithm Design

### Neighbor Discovery via HELLO/Echo

At startup (and every `helloInterval` seconds, 20 s by default), each node floods a small `HELLO` packet — 64 B, roughly 0.03% link utilization overhead — through **every** one of its interfaces. The packet carries two fields that matter: the originating node's ID (`source`) and a hop counter that starts at 1 (`hopCount`).

When a node receives a `HELLO` arriving on gate `i`:

1. **If `source == myId`**: the packet has completed a full lap of the ring — discard it.
2. **Otherwise**: call `learnRoute(source, i, hopCount)` — update the routing table if this path is strictly shorter than any previously known route to `source`. Then increment `hopCount` and forward through gate `1 - i` (the opposite interface).

The "forward through the opposite interface" rule is what makes flooding loop-free **by construction** on a ring topology: each HELLO travels exactly once in each direction around the ring and stops when it reaches its origin. No sequence numbers, no TTL — the topology itself provides the termination condition.

The routing table is a `std::map<int, Route>` (see `Net.cc:14–18`), where each `Route` holds the exit gate and hop count of the best-known path to that destination.

### Data Forwarding

When a data packet arrives and `myId ≠ destination`, the node looks up the destination in `routingTable` and forwards through the recorded gate. If no route has been learned yet — only possible in the very first seconds before any HELLO has completed a full lap — the node falls back to the kickstarter rule (`lnk[0]`), giving the discovery phase time to converge.

**Pseudocode:**

```
on receiving HELLO via gate i, origin s, hopCount h:
    if s == myId: discard
    else:
        if unknown route to s, or h < known hopCount:
            routingTable[s] = { gate=i, hops=h }
        forward via gate (1 - i), hopCount = h + 1

on receiving data packet p:
    if destination(p) == myId: deliver to App
    else:
        p.hopCount += 1
        if destination(p) in routingTable:
            outGate = routingTable[destination(p)].gate
        else:
            outGate = 0   // kickstarter fallback while converging
        forward p via outGate
```

**Implementation note:** we reuse the same `Packet` class for both data and control, distinguished by a boolean `isHello` field (`packet.msg:5`). Using `setKind()`/`getKind()` (our first attempt) caused collisions because `App.cc` already uses the `cMessage` kind field for something else — the explicit boolean field sidesteps the issue cleanly.

---

## Experimental Setup

### Traffic Scenarios

All runs: 200 s simulation time, seed 0, packets of 125,000 B (exactly 1 second of transmission at 1 Mbps), interarrival times drawn from `exponential(1)` unless stated otherwise.

| Scenario | Sources | Destination |
|---|---|---|
| **Case 1** | `node[0]`, `node[2]` | `node[5]` |
| **Case 2** | All 7 other nodes (0–4, 6–7) | `node[5]` |

Case 1 is a light-load scenario where the baseline's merge point is pushed to ρ ≈ 2. Case 2 is a saturation scenario (ρ ≈ 7 under the baseline) used to find the stability threshold via a parameter sweep over `interArrivalTime ∈ {2, 3, 4, 5, 6, 7, 8, 10, 15, 20}` s.

### Baseline vs. Proposed — One Parameter, Two Algorithms

Both algorithms live in the same `Net` module, controlled by a single boolean:

```ini
; omnetpp.ini

[Caso1]                               ; baseline: useShortestPath defaults to false
network = Network
sim-time-limit = 200s
Network.node[0].app.interArrivalTime = exponential(1)
...

[Caso1Propuesto]
extends = Caso1                       ; identical traffic, channels, and seed
Network.node[*].net.useShortestPath = true   ; only this changes
```

The `Caso2Propuesto` configuration follows the same pattern. Every pair of runs shares the exact same generated traffic, link parameters, and random seed — any observed difference is **solely attributable to the routing decision**. The professor explicitly highlighted this as a pattern worth emulating: we were the only group with a clean baseline/proposal pair in `omnetpp.ini`.

---

## Results

### Case 1 — Light Load

| Metric | Baseline | Proposed | Improvement |
|---|---|---|---|
| Mean delay | 51.16 s | 6.85 s | **−87%** |
| Max delay | 105.56 s | 14.42 s | −86% |
| Avg hops | 3.92 | 3.00 (constant) | −23% |
| Peak buffer occupancy (`node[0].lnk[0]`) | 190 pkts | 18 pkts | **−91%** |

Under the baseline, `node[0].lnk[0]` concentrates both flows (ρ ≈ 2λ = 2 at a 1-packet/s server), and its queue grows from 0 to 190 packets over 200 s — never reaching steady state. All 16 counterclockwise interfaces sit at 0% utilization.

With our algorithm, `node[2]` routes to `node[5]` via `lnk[1]` (3 hops: 2→3→4→5) instead of the 5-hop clockwise path. The merge point disappears, each flow runs on its own half-ring at ρ ≈ 1, and hop count drops to a constant 3 for all 375 delivered packets (standard deviation: 0).

### Case 2 — Saturation and Stability Threshold

At `interArrivalTime = 1 s`, both networks remain overloaded (ρ ≫ 1) and mean delay is nearly unchanged (−2%). The relevant metric here is **throughput**: our algorithm delivered **398 packets** in 200 s vs. **199 for the baseline** — exactly double.

The reason is structural: the proposed algorithm splits the seven flows across both directions according to their actual distances to `node[5]` (three clockwise: `node[0]`, `node[6]`, `node[7]`; four counterclockwise: `node[1]`–`node[4]`), reducing the worst-case bottleneck from ρ ≈ 7 to ρ ≈ 4. This shifts the **stability threshold**:

| | Baseline | Proposed |
|---|---|---|
| Stability threshold (`interArrivalTime`) | ≈ 7 s | ≈ 4–5 s |
| Theoretical critical ρ | 7/IAT = 1 at IAT = 7 s | 4/IAT = 1 at IAT = 4 s |

The empirical threshold matches M/M/1 theory (ρ = λ/μ = 1 at the critical point) in both cases — the model is consistent with queueing fundamentals.

No routing loops were observed in any run. This is structurally guaranteed: data packets always move strictly closer to their destination (ring distances are monotone), and HELLO packets travel each direction exactly once before being discarded.

---

## Stretch Goal — Generalization to Arbitrary Topologies

`NetworkStar` (a 57-node topology with up to 4 interfaces per node) is defined but commented out in `network.ned`. The `Node` module is already generic — it accepts an `interfaces` parameter and connects dynamically — but our routing algorithm is ring-specific: the "forward through the opposite interface" termination rule does not generalize beyond degree-2 topologies, and the assumption that interfaces come in pairs (0 and 1) breaks in nodes with 3 or 4 links.

We discuss this limit explicitly in the paper: the natural extension would be to replace our termination rule with classic flooding with sequence numbers, which generalizes to any topology. This was identified as the top priority for future work but fell outside the scope of this lab.

---

## Paper & Poster

[![Paper preview](paper/preview-1.png)](paper/lab4-paper.pdf)

The work is accompanied by a full scientific paper ([`paper/lab4-paper.pdf`](paper/lab4-paper.pdf)) and a visual poster (`poster/lab4-poster.tex`). The paper covers the formal algorithm description, an M/M/1 bottleneck analysis of both scenarios, and the full experimental results with figures exported from OMNeT++. Both were evaluated and approved with positive feedback from the course instructors; the professor specifically called out the writing quality, analytical rigor, and the explicit baseline/proposal comparison structure in `omnetpp.ini` as highlights.

---

## Build & Run

Requires OMNeT++ 6.x. From the project root:

```bash
# Compile
make

# Run baseline — Case 1
opp_run -s -c Caso1 omnetpp.ini

# Run proposed algorithm — Case 1 (same traffic, same seed)
opp_run -s -c Caso1Propuesto omnetpp.ini

# Run baseline — Case 2
opp_run -s -c Caso2 omnetpp.ini

# Run proposed algorithm — Case 2
opp_run -s -c Caso2Propuesto omnetpp.ini

# Stability sweep — Case 2, baseline
opp_run -s -c Caso2Sweep omnetpp.ini

# Stability sweep — Case 2, proposed
opp_run -s -c Caso2SweepPropuesto omnetpp.ini
```

Results land in `results/` as `.sca` (scalars) and `.vec` (time-series vectors), viewable in the OMNeT++ IDE Analysis Tool.

---

## Course Context

**Lab 4** — Networks and Distributed Systems  
Facultad de Matemática, Astronomía, Física y Computación (FaMAF)  
Universidad Nacional de Córdoba, Argentina — 2026
