# HiTuxShare

A native Linux port of [HiShare](https://github.com/atomozero/HiShare) — itself the modernized
edition of Jeremy Friesner's BeShare — speaking the same [MUSCLE](https://github.com/jfriesne/muscle)
protocol, so it joins the same chat rooms, user lists and file queries as the Haiku clients.

**Status: planning. No code yet.**

Start with **[PLAN.md](PLAN.md)** — architecture, language and GUI toolkit options with pros and cons,
a phased roadmap, and the porting risk list.

Reference notes distilled from reading the HiShare and MUSCLE sources live in
**[docs/PROTOCOL.md](docs/PROTOCOL.md)**.

### Diagrams

| | |
|---|---|
| [`architecture.svg`](docs/diagrams/architecture.svg) | Layered design — toolkit-free core, MUSCLE below, front-ends above |
| [`protocol-flow.svg`](docs/diagrams/protocol-flow.svg) | What each phase actually puts on the wire |
| [`porting-seams.svg`](docs/diagrams/porting-seams.svg) | Every BeAPI dependency and its Linux replacement |
| [`roadmap.svg`](docs/diagrams/roadmap.svg) | Phases 0–4 with completion criteria |

### Goals

Light, fast and small. Event-driven, 0% CPU at idle, a binary under 2.5 MB, no bundled runtimes,
and wire compatibility with BeShare 3.04 / HiShare 1.2 as the acceptance test for every phase.

### Phases

1. **Chat** — connect, user list, public and private messages, slash commands
2. **Downloading** — live queries, peer-to-peer transfers, resume, connect-back
3. **Sharing** — inotify-driven share publishing, uploads, NAT port mapping
