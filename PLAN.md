# HiTuxShare — porting HiShare from Haiku to Linux

**Status:** Phase 0 and Phase 1 implemented and verified — see [README](README.md) for what works
**Date:** 2026-08-24 (plan), Phase 1 landed the same day
**Upstream:** [atomozero/HiShare](https://github.com/atomozero/HiShare) 1.2 (Haiku) · [jfriesne/muscle](https://github.com/jfriesne/muscle) 9.92
**Goal:** a light, fast, small native Linux client that is wire-compatible with BeShare 3.04 / HiShare 1.2

---

## 1. Executive summary

**Recommended stack: C++20 + upstream MUSCLE 9.92 + Qt 6 Widgets, with a toolkit-free `libhitux-core` in between.**

The reasoning in one paragraph: MUSCLE *is* the protocol, MUSCLE is C++, and MUSCLE already builds
clean on Linux today — I built it during this review in **9.9 seconds** producing an 8.2 MB
`libmuscle.a`, and linked its bundled `tools/chatclient` (a 355-line, BeShare-compatible chat client)
to a **1.5 MB stripped binary**. Any non-C++ language means reimplementing the `Message`
flatten/unflatten format, the `MessageIOGateway` framing, the glob matcher and the zlib encoding
before you can say "hello" in a chat room. That is real work with no payoff, because the C++ version
of that work is already written, tested against 25 years of deployed clients, and maintained by the
protocol's author.

Qt 6 wins the toolkit slot for one concrete reason beyond the usual ones: **MUSCLE ships official Qt
integration** (`platform/qt/` — `QMessageTransceiverThread`, `QSocketCallbackMechanism`,
`QPostEventCallbackMechanism`). The single hardest structural problem in this port — getting messages
from a background network thread onto the GUI thread safely, which on Haiku was
`BMessageTransceiverThread` handing you `BMessage`s — is *already solved and shipped* for Qt and for
nothing else. Everything else (multi-column sortable lists with tens of thousands of live-updating
query results, splitters, drag-and-drop of results out to a file manager) is off-the-shelf in Qt and
hand-built anywhere else.

"Light and fast" is preserved: the HiTuxShare binary itself lands around **1.5–2.5 MB**, idles at 0%
CPU, and starts in well under a second. Qt is a *runtime dependency* — it is not compiled into your
app and it is already on nearly every Linux desktop. That is a very different thing from bloat.

### The one number that matters

HiShare's app code is **27,416 lines**, but that headline is misleading:

| Component | Lines | Port cost |
|---|---:|---|
| `ShareWindow.cpp/.h` | 8,134 | **Rewrite** — Haiku Layout Kit, unportable |
| `ShareStrings.cpp/.h` | 7,431 | Mechanical — string tables → gettext |
| `ShareFileTransfer.cpp/.h` | 2,263 | **Port** — the real Phase 2/3 work |
| `ShareNetClient.cpp/.h` | 1,910 | **Port** — the real Phase 1 work |
| `ChatWindow.cpp/.h` | 1,486 | Rewrite (GUI) |
| `santa/` vendored widgets | 2,918 | **Delete** — Qt replaces all of it |
| `SplitPane.cpp/.h` | 814 | **Delete** — `QSplitter` |
| `PortMapper.cpp/.h` | 1,172 | **Reuse ~verbatim** (plain BSD sockets already) |
| `PirateDemo` + `pirate_asm.S` | 316 | Delete (x86-64 asm chiptune easter egg) |
| everything else | ~1,000 | Mixed |

So the genuinely *irreducible* port work is ~4,200 lines of network/transfer logic. The rest is either
deleted, mechanically converted, or rewritten as GUI — and rewriting the GUI is unavoidable no matter
which language or toolkit you pick, because the original is built on the Haiku Layout Kit and on
Santa's ColumnListView.

---

## 2. What we get for free (verified, not assumed)

Everything below I confirmed by building and reading the actual trees during this review:

- **MUSCLE 9.92 builds clean on Linux Mint 22.3 / gcc 13 with CMake.** No patches. 9.9 s wall with
  `-j`, `libmuscle.a` = 8.2 MB.
- **`muscle/tools/chatclient.cpp` is a maintained, BeShare-compatible chat client** — 355 lines, and
  its header comments literally say *"stolen from ShareNetClient.h"*. It speaks the same
  `NET_CLIENT_NEW_CHAT_TEXT` / `beshare/name` / `SUBSCRIBE:beshare/*` protocol HiShare does. This is
  our Phase 0 in a box. Stripped binary: **1.5 MB**.
- **`muscle/lang/python3/python_chat.py`** is a second reference implementation of the same thing in
  148 lines of Python — the clearest possible spec for Phase 1's message flow.
- **`muscle/tools/minichatclient.c` (738 lines) and `microchatclient.c` (625 lines)** are pure-C
  BeShare chat clients over MUSCLE's C `MiniMessage`/`MicroMessage` implementations. Useful as proof
  the protocol is small, and as a fallback if we ever want an absolutely minimal build.
- **`platform/qt/`** — `QMessageTransceiverThread` plus two ready-made `ICallbackMechanism`
  implementations, and a `qt_muscled_browser` example app that already does the
  "subscribe to a node tree and show it in a widget" thing we need.
- **`util/ICallbackMechanism.h`** is the documented toolkit-agnostic seam. Implementations exist for
  Qt, SDL, JUCE and Win32; writing one for GTK or FLTK is ~30 lines over an eventfd.
- **`beshare.tycomsystems.com:2960` is up and accepting TCP right now.** The ecosystem we are
  targeting still exists. (I only checked that the port accepts a connection — I did not log in or
  send anything, since that would publish a name into a public chat room.)
- **HiShare's application code is public domain**, per its LICENSE — Jeremy Friesner released BeShare
  that way and atomozero kept it. No licensing friction in lifting logic directly. MUSCLE is a
  permissive Meyer Sound licence. (Santa's ColumnListView has a fussy shareware-era licence — another
  reason to be glad we are deleting it rather than porting it.)

---

## 3. What actually has to be ported

See `docs/diagrams/porting-seams.svg` for the full map. The summary:

| Haiku dependency | Linux replacement | Difficulty |
|---|---|---|
| `BMessageTransceiverThread` | `CallbackMessageTransceiverThread` + `ICallbackMechanism` | Moderate |
| `AcceptSocketsThread` | `MessageTransceiverThread::PutAcceptFactory()` | Moderate |
| `node_monitor` tree watching | `inotify` (recursive) | Moderate |
| BFS attributes / `BMimeType` | display-only; publish basics from XDG mime | **Feature loss** |
| `BEOS:ICON` HVIF icons | ignore on receive, never publish | **Feature loss** |
| Santa's `ColumnListView`, `SplitPane` | `QTreeView` + `QSortFilterProxyModel`, `QSplitter` | Easy (delete) |
| Locale Kit `.catkeys` | gettext `.po` (mechanical conversion of existing ~20 languages) | Easy |
| `BNotification` | libnotify / `org.freedesktop.Notifications` | Easy |
| Tracker integration, `BFilePanel` | `xdg-open`, `QFileDialog`, `text/uri-list` drags | Easy |
| `BMessage` settings archive | `muscle::Message` flattened into `$XDG_CONFIG_HOME` | Easy |
| `PortMapper` (NAT-PMP/UPnP/PCP) | reuse; swap `BNetworkRoute` gateway lookup for netlink | Easy |
| MUSCLE 6.11 API idioms | 9.92 signatures — ~116 `Find*(name, &ptr)` call sites | Tedious, low risk |

### Two gotchas worth knowing up front

1. **MUSCLE 8.40 removed BeOS/Haiku support *and* `AcceptSocketsThread`.** HiShare uses both. The
   HISTORY entry is blunt about why: *"previous changes to the `status_t` type had rendered the
   codebase unbuildable on those platforms anyway."* For us this is good news — Linux is the
   first-class supported platform — but it means HiShare's own vendored MUSCLE 6.11 is not a shortcut.
   Build against upstream 9.92 and take the mechanical API hit once.

2. **`status_t` is now a class**, and `FindString(name, &charPtr)` became
   `FindString(name, charPtr&)`. Old `== B_NO_ERROR` comparisons still compile (there is an
   `operator==`), but the `Find*` pointer-to-reference change does not. ~116 call sites in HiShare
   match that pattern; a good chunk of those are `BMessage` calls that are being deleted anyway.

---

## 4. Language options

Ranked, with the honest case for and against each.

### 4.1 C++20 — **recommended**

**Pros**
- MUSCLE is C++. Zero protocol implementation work: `Message`, `MessageIOGateway`, `StringMatcher`
  globbing, zlib encoding, ref-counting, the reflector constants — all of it is a submodule away.
- `ShareNetClient.cpp` and `ShareFileTransfer.cpp` — the 4,200 lines that are genuinely hard to
  get right — can be lifted with their logic and control flow intact. That is 25 years of
  accumulated protocol edge cases (resume offsets, munge modes, checksum paranoia, connect-back
  fallback, per-host serialization) that you do not want to re-derive from scratch.
- Smallest and fastest result. No runtime, no GC, no interpreter.
- Fixes can flow back upstream to HiShare on Haiku, and vice versa — same language, same structure.
- `tools/chatclient.cpp` is a working Phase 0 you can compile in 10 seconds.

**Cons**
- Manual memory and thread safety in a network-facing app that parses data from untrusted peers.
  Mitigated by MUSCLE's `Ref`/`MessageRef` counting (the codebase is already RAII-shaped) and by
  running the transfer paths under ASan/UBSan in CI.
- MUSCLE's idioms are not std — `muscle::String`, `Hashtable`, `Queue`, `status_t`. You are writing
  MUSCLE-flavoured C++, not modern-std C++, at the seams.
- C++ build/dependency ergonomics are worse than Cargo or Go modules.

### 4.2 Rust

**Pros**
- Memory safety exactly where this app is most exposed: parsing `FILE_DATA` and node messages from
  arbitrary internet peers.
- Excellent tooling, easy static binaries, strong async story for the transfer engine.
- Would be a genuinely more robust program if it existed.

**Cons — and they are decisive here**
- **No MUSCLE for Rust.** You must reimplement: the `Message` flatten/unflatten binary format,
  `MessageIOGateway` framing, the `PathMatcher`/`StringMatcher` glob semantics that queries depend
  on, the zlib `Enc0`+ encodings, and the reflector command/result constants. The Python reference
  implementation of just the basics is 1,959 lines; a faithful Rust one with the transfer path is
  realistically 3,000–5,000.
- Getting the wire format subtly wrong is the worst possible failure mode — it fails against real
  Haiku peers, not in your unit tests.
- FFI to the C++ library is not a rescue: MUSCLE's API is template- and reference-counting-heavy,
  which is the exact shape `cxx`/`bindgen` handle worst. You would end up hand-writing a C shim that
  is itself most of the work.
- Linux GUI story is the weakest of any option (see §5.8).

**When it would be right:** if the goal were a from-scratch protocol-compatible client as a
learning exercise, or if you wanted a hardened server. For a port that should reach parity, no.

### 4.3 C (with MUSCLE's MiniMessage / MicroMessage)

**Pros**
- MUSCLE genuinely supports this: `lang/c/minimessage` (1,608 lines) and `lang/c/micromessage`
  (1,225 lines) are official C implementations, and `tools/minichatclient.c` is a working
  BeShare chat client in C.
- The absolute smallest binaries and lowest memory of any option. `MicroMessage` parses messages
  in-place with no allocation at all.

**Cons**
- The C side only covers *messages*. There is no C implementation of the file-transfer session
  logic, the reflect-server accept path, or the rate-limiting policies — Phases 2 and 3 would be
  written from nothing.
- A GUI in C means GTK, and GTK's C object system is a lot of boilerplate for the list-heavy UI
  this app is.
- Manual memory management with none of C++'s RAII help, in the same untrusted-input position that
  made us want Rust.

**Verdict:** the right tool for an embedded or headless BeShare bot. Wrong for a desktop client.

### 4.4 Go

**Pros**
- Goroutines map beautifully onto "one connection per peer, many concurrent transfers".
- Fast builds, easy static binaries, easy cross-compilation.

**Cons**
- Same fatal issue as Rust: no MUSCLE, full protocol reimplementation required.
- Every serious Linux GUI binding for Go is cgo (gotk3, qt bindings) — which discards Go's build
  simplicity, its main advantage — or it is Fyne, which is a self-drawn toolkit that is neither
  small nor native-looking.
- Binaries start ~10–20 MB before you add a GUI.

### 4.5 Python 3 + bindings

**Pros**
- **The fastest path to a working Phase 1 by a wide margin.** `muscle/lang/python3/` ships
  `message.py`, `message_transceiver_thread.py` and a working `python_chat.py`. You could have a
  chatting GUI in an afternoon.
- Genuinely useful as a *throwaway protocol explorer* even if the real client is C++.

**Cons**
- Directly contradicts "light, fast, small". Shipping means PyInstaller or a venv, ~40 MB+.
- The Phase 2/3 file transfer path would be the weak point: pushing hundreds of MB through
  Python-level message assembly, with checksums, is where it stops being fun.

**Verdict:** not the product. Do keep `python_chat.py` around as a reference oracle when debugging
the C++ implementation — being able to diff your bytes against a known-good third implementation is
worth a lot.

### 4.6 Zig, D, Nim, and friends

Same protocol-reimplementation problem as Rust/Go, plus smaller ecosystems, plus immature GUI
bindings, plus (for Zig) ongoing language churn. Zig's C++ interop is not yet good enough to consume
MUSCLE directly. No reason to choose one here.

### Language verdict

**C++20.** The decision is made by MUSCLE, not by taste: every other language starts by
reimplementing a 25-year-old binary protocol that already has a maintained, battle-tested C++
implementation, and none of them buys anything that offsets that.

---

## 5. GUI toolkit options

Assuming C++. Ranked.

### 5.1 Qt 6 Widgets — **recommended**

**Pros**
- **MUSCLE ships Qt integration.** `platform/qt/QMessageTransceiverThread` +
  `QPostEventCallbackMechanism` solve the network-thread → GUI-thread handoff that is the single
  biggest structural risk in this port. No other toolkit gets this for free.
- `QTreeView` + `QAbstractItemModel` + `QSortFilterProxyModel` is a near-exact functional match for
  Santa's ColumnListView — multi-column, sortable, icon-bearing — and it is *virtualized*, which
  matters because a broad query (`*`) on a busy server returns tens of thousands of live-updating
  rows. This is the feature that quietly makes or breaks the app.
- `QSplitter` replaces `SplitPane`. `QDrag` with `text/uri-list` gives drag-out-to-file-manager.
  `QFileDialog`, `QSystemTrayIcon`, `QSettings` — all the desktop plumbing is there.
- Already installed here (6.4.2). Theme-aware light/dark, HiDPI, IME and accessibility all work.
- Consistent with your existing direction: the NimblePDF multi-platform plan already picked Qt 6 for
  the Linux and Windows targets, so this is one toolkit to know, not two.
- Free Windows and macOS ports later, if that ever appeals.

**Cons**
- The heaviest runtime dependency — Qt6Core/Gui/Widgets is ~40–50 MB installed if a user does not
  already have it. On a Qt-based desktop (KDE) it is already there; on GNOME/Cinnamon it is one
  `apt install` that most systems have pulled in anyway.
- LGPLv3 obligations if you ever ship a statically linked binary. Dynamic linking (the normal case)
  is unencumbered.
- `moc` in the build. Trivial with CMake's `AUTOMOC`, but it is a code-generation step.

**Note on "bloat":** your *binary* stays ~1.5–2.5 MB and idles at 0% CPU. Qt is shared-library
weight on disk, not weight in your program. If the real objection is "I don't want to depend on
something huge", that is a legitimate but different concern from speed or footprint at runtime.

### 5.2 GTK4 / gtkmm-4

**Pros**
- Native on GNOME, and Mint's Cinnamon is GTK-based, so it is the most at-home look on your own
  desktop.
- `GtkColumnView` is a proper virtualized multi-column list — GTK4 finally has the widget this app
  needs (GTK3 did not, really).
- Already installed here (4.14.5), and the C library is genuinely well engineered.

**Cons**
- You write the `ICallbackMechanism` yourself. This is genuinely easy — an eventfd plus a `GSource`,
  ~30 lines — but it is the one piece Qt hands you finished.
- gtkmm-4's C++ bindings lag the C API and are less idiomatic than Qt's; you will drop to C
  occasionally.
- GTK4 has been an API-churn treadmill, and **Cinnamon is GTK3** — a GTK4 app on Mint looks
  slightly foreign in exactly the environment you would run it in.
- `GtkColumnView` with sorting, per-row icons and live updates is meaningfully fiddlier than
  `QTreeView` + a proxy model.

**Verdict:** the strongest runner-up, and the right pick if you want the app to feel GNOME-native or
you object to Qt on principle. Costs a bit more work for a bit less capability.

### 5.3 FLTK 1.4

**Pros**
- **The genuine "light and fast, not bloated" answer.** Statically links to ~1 MB total, near-instant
  startup, no runtime dependencies beyond X11/Wayland basics.
- Simple, stable C++ API that has not churned in decades.
- `Fl::awake()` is a one-line `ICallbackMechanism`.

**Cons**
- `Fl_Table` / `Fl_Browser` are primitive. Sortable multi-column lists with icons, live updates and
  10k+ rows must be hand-built on top of `Fl_Table_Row` — that is real, unglamorous work, and it is
  precisely the part of the UI this app lives in. You would be rebuilding ColumnListView, which is
  what we were happy to delete.
- Looks dated by default; HiDPI is only decent in 1.4; no native file dialogs (it shells out to
  zenity/kdialog or uses its own); weak IME and accessibility.
- Not packaged on this machine — you would build it from source.
- You would lose exactly the "modern, theme-aware GUI" that was HiShare 1.x's headline improvement
  over BeShare 3.04.

**Verdict:** philosophically the best fit for the stated goal, practically the worst fit for *this
particular app*, because file-sharing clients are ~80% list widget. Excellent choice for a small
tool; wrong for this one.

### 5.4 Dear ImGui (+ GLFW or SDL)

**Pros**
- Small, self-contained, and `ImGui::BeginTable` is genuinely excellent for sortable columns —
  better than FLTK for our core need.
- Total control over appearance; trivial to make it look like whatever you want.

**Cons**
- **Immediate mode means redrawing continuously.** A chat client that sits open all day should idle
  at 0% CPU and 0 GPU wakeups. ImGui's power-saving modes exist but are a fight, and you are pulling
  in a GL/Vulkan context for a text-and-lists app. This fails "fast and light" on the axis that
  actually matters for a daemon-like desktop app: energy at idle.
- No native text input, IME, clipboard conventions, accessibility, or file dialogs.
- Not a good desktop citizen — no window-manager integration, no drag-and-drop with the file manager.

### 5.5 wxWidgets

**Pros**
- Native look because it wraps GTK on Linux; `wxListCtrl` in virtual mode handles big lists; mature
  and stable; genuinely cross-platform.

**Cons**
- On Linux it is a wrapper *over* GTK — so you take GTK's dependency weight plus wx's, to get an
  older-feeling API and one more abstraction layer to debug through.
- No MUSCLE integration.
- Strictly dominated: if you want GTK, use GTK; if you want cross-platform, use Qt.

### 5.6 TUI (FTXUI or notcurses)

**Pros**
- The smallest and fastest possible thing that could work; runs over SSH; a delightful way to use
  the chat half.
- FTXUI is a clean modern C++ library with no dependencies.

**Cons**
- File browsing, transfer progress and drag-and-drop are awkward-to-hostile in a terminal.
- Not a replacement for the app HiShare is.

**Verdict — worth doing anyway, as a bonus target.** Because the core is toolkit-free, a
`hitux-tui` is a few hundred lines on top of it, and it doubles as the Phase 0 protocol harness and
a permanent regression-test client. Build it, just don't call it the product.

### 5.7 Web-based (Electron / Tauri / webview)

Electron contradicts every stated goal. Tauri is smaller but still renders through webkit2gtk (a
browser engine as a dependency, which is heavier than Qt) *and* drags in the Rust protocol problem
from §4.2. Not viable.

### 5.8 Note on non-C++ GUI options

If you were to pick Rust or Go after all, understand the toolkit picture is worse there, not just
different: Rust's mature options are `gtk4-rs` (good, but GTK), Slint or Iced (self-drawn, no native
integration), or Qt via `cxx-qt` (which reintroduces C++ anyway). This compounds §4.2's problem
rather than offsetting it.

### GUI verdict

**Qt 6 Widgets**, with the core kept strictly toolkit-free so that FLTK, GTK or TUI front-ends
remain possible without touching the network layer. If you want to reject Qt on dependency grounds,
**GTK4/gtkmm-4** is the fallback and costs perhaps 15% more work.

---

## 6. Recommended architecture

See `docs/diagrams/architecture.svg`.

```
hitux (Qt 6 GUI)   hitux-tui (optional)   hitux-probe (CLI, Phase 0)
──────────────────────── ICallbackMechanism ─────────────────────────
                    libhitux-core   (static, zero GUI deps)
   ServerConnection · UserRegistry · ChatSession · QueryEngine
   TransferEngine · ShareScanner · PortMapper · MimeResolver · Settings
─────────────────────────────────────────────────────────────────────
              MUSCLE 9.92  (git submodule, unmodified)
─────────────────────────────────────────────────────────────────────
   inotify · xattr · shared-mime-info · libnotify · xdg-open · sockets
```

The hard rule: **`libhitux-core` must never include a Qt header.** This is the same
core-plus-thin-seam shape as the NimblePDF multi-platform plan, and it is what keeps the FLTK/TUI
options alive and what makes the core unit-testable without a display.

Proposed layout:

```
HiTuxShare/
├── CMakeLists.txt
├── third_party/muscle/          # submodule, pinned to 9.92
├── core/                        # libhitux-core — no GUI headers, ever
├── qt/                          # the Qt 6 front-end
├── tui/                         # optional FTXUI front-end
├── tools/probe/                 # Phase 0 headless CLI
├── docs/diagrams/               # SVG
└── po/                          # gettext, converted from HiShare catkeys
```

---

## 7. Phased plan

See `docs/diagrams/roadmap.svg`. Each phase ends in something runnable against a live server.

### Phase 0 — Skeleton and protocol proof

Deliverables: repo, CMake, MUSCLE pinned as a submodule, CI that builds and runs clang-tidy, and
`hitux-probe` — `muscle/tools/chatclient.cpp` adapted into our tree as a headless client.

**Done when:** `hitux-probe beshare.tycomsystems.com` shows live chat and a user list in a terminal.

Why first: it proves the wire format before a widget exists, so every later bug is unambiguously a
GUI or logic bug. It is nearly free — the client already exists upstream.

### Phase 1 — Chat

- `ServerConnection`: connect, publish `beshare/name`, `SUBSCRIBE:beshare/*`, keepalive
  (`PR_COMMAND_NOOP` after 5 min idle), reconnect.
- `UserRegistry`: session ID → {name, status, host, port, client version, file count, firewalled,
  bandwidth, install ID, capability flags}.
- `ChatSession`: `what=2` send/receive, `/me` actions, private messages, `NET_CLIENT_PING`/`PONG`.
- Qt shell: splitter layout, menus, user list, chat view with per-type colouring and timestamps,
  input with tab-completion of nicknames.
- Private chat windows (tabs, probably, rather than HiShare's separate windows).
- Slash commands. Start with the ones that carry their weight: `/nick /msg /me /status /ping /clear
  /help /connect /disconnect /priv /ignore /watch /alias /away /quit`. HiShare has ~40; the rest can
  land opportunistically.
- Settings persisted as a flattened `muscle::Message` under `$XDG_CONFIG_HOME/hituxshare/`.

**Done when:** a Haiku HiShare user and a HiTuxShare user hold a conversation, public and private,
and each sees the other correctly in the user list.

**Deliberately deferred to Phase 4:** multi-server. It is HiShare 1.2's headline feature and it is
genuinely nice, but it multiplies the state model of every list in the app. Design the core so
`ServerConnection` is already a first-class object that things hang off (HiShare did this — every
callback carries a `ServerConnection*`), then adding connection #2 later is bookkeeping rather than
surgery.

### Phase 2 — Downloading

- `QueryEngine`: `SUBSCRIBE:/*/<sessionExp>/beshare/fi*/<fileExp>`, plus the `PR_COMMAND_PING`
  round-trip that signals "initial index sweep complete". Results arrive live and keep arriving.
- Results model: virtualized, sortable, columns from the `beshare:*` fields, with received BFS
  attributes shown as extra columns when present.
- `TransferEngine` download side: direct TCP to the peer's advertised host:port,
  `TRANSFER_COMMAND_PEER_ID` → `FILE_LIST` → `FILE_HEADER` → `FILE_DATA`, honouring the `chk`
  checksum and `mm` munge-mode fields.
- Resume via `beshare:StartOffset`, byte ranges via `maxbytes`/`beshare:SendLength`, and the
  partial-MD5 (`NUM_PARTIAL_HASH_BYTES` = 64 KB) handshake that decides whether a resume is valid.
- Connect-back path for firewalled peers, via `PutAcceptFactory()`.
- Transfer queue UI: per-host serialization, progress, rates, cancel/retry, download rate limiting
  via `RateLimitSessionIOPolicy`.

**Done when:** we download a multi-hundred-MB file from a Haiku peer, byte-identical, including a
resume after a deliberate mid-transfer kill.

### Phase 3 — Sharing

- `ShareScanner`: recursive inotify watch of the share folder, producing
  `PR_COMMAND_SETDATA beshare/files/<name>` nodes with `beshare:File Size`, `Modification Time`,
  `Path`, `Kind`. Batched, on a background thread, with the low-priority send queue HiShare uses so
  a big share does not stall chat.
- `beshare/filecount` and `beshare/uploadstats` publishing.
- Upload side of `TransferEngine`: accept factory, serve `FILE_HEADER` + `FILE_DATA`, upload queue
  with configurable simultaneous-upload limits, per-peer bans, upload rate limiting.
- Firewalled mode: publish under `beshare/fires/` instead of `beshare/files/`, honour
  `NET_CLIENT_CONNECT_BACK_REQUEST`.
- `PortMapper` ported (NAT-PMP, UPnP-IGD, PCP) with the external reachability probe.

**Done when:** a Haiku HiShare client queries, finds and successfully downloads a file from us —
both with and without NAT port mapping in play.

### Phase 4 — Polish

Notifications, gettext i18n (convert HiShare's ~20 existing `.catkeys` — the translations already
exist and should be honoured), drag-and-drop in both directions, `.desktop` + AppStream metadata,
app icon, packaging (`.deb` first, then Flatpak), and multi-server.

---

## 8. Risks and open questions

**Risks**

1. **Interop is the only acceptance test that counts.** Unit tests cannot tell you the wire format is
   right. Every phase must be validated against a real Haiku client or a real public server. Budget
   for that; it is not optional polish.
2. **BFS attributes are asymmetric.** We can *display* the rich attribute columns Haiku peers publish,
   but we cannot meaningfully *produce* them — Linux xattrs are ~4 KB and nobody populates them. Our
   shared files will look plainer to Haiku users than theirs do to us. This is inherent, not a bug,
   and the protocol treats those fields as optional so nothing breaks.
3. **TLS: do not inherit HiShare's.** `BESHARE_TLS_ENABLED` is 0 in HiShare 1.2 because two-peer TLS
   crashed the downloader in MUSCLE's ByteBuffer pool teardown. We are on 9.92, not 6.11, so the code
   is different — but keep TLS off for v1 regardless and revisit deliberately.
4. **IPv6.** HiShare's Makefile hard-codes `-DMUSCLE_AVOID_IPV6` with a comment that dropping it broke
   IPv4 connections on Haiku. That was a Haiku socket-stack issue; on Linux we should build dual-stack
   and test it properly rather than inheriting the workaround.
5. **inotify watch limits.** A large shared tree can exhaust `max_user_watches`. Needs graceful
   degradation (fall back to periodic rescan) rather than silent failure.
6. **Two firewalled peers can never transfer to each other.** Protocol limitation, present since
   BeShare. Not fixable; just needs a clear message in the UI.

### What implementation confirmed or corrected

Recorded here because the plan's predictions are only worth anything if the misses
are written down next to them.

- **The MUSCLE-does-the-hard-part bet paid off.** MUSCLE 9.92 built unmodified,
  `tools/chatclient.cpp` was a usable Phase 0 skeleton, and the Qt callback mechanism
  worked as advertised. Phase 1 needed no protocol code beyond reading and writing
  fields.
- **The `status_t` / `Find*` API churn was a non-issue** for new code — it only bites
  when lifting HiShare's existing files, which Phase 2 will do.
- **Two bugs were found only by running against a real server**, which is the whole
  argument for Phase 0 existing:
  - `muscled` reports a departing user as one removal per matched leaf node
    (`/host/1/beshare/name`, depth 4), *not* as a single removal at session depth.
    Both HiShare and this plan assumed session depth. Users would have accumulated
    in the list forever.
  - `FileDataIO(path, mode)` defers its `fopen()`, so the obvious "did it open?"
    check is always false. Settings silently never persisted.
- **The GUI needed no `ICallbackMechanism` work at all**, as predicted — but note the
  corollary: choosing GTK or FLTK later means writing and debugging that ~30-line
  bridge, and it is the piece most likely to produce rare, hard-to-reproduce
  cross-thread bugs.

**Open questions for you**

1. **Qt 6 or GTK4?** My recommendation is Qt 6 — MUSCLE's shipped Qt integration and `QTreeView` are
   worth real money here, and it matches the NimblePDF direction. GTK4 is a legitimate alternative if
   Cinnamon-nativeness matters more to you than the free thread bridge.
2. **Is a `hitux-tui` target worth carrying?** I think yes — it is cheap given a toolkit-free core and
   it makes an excellent permanent test client — but it is scope.
3. **Multi-server in Phase 1 or Phase 4?** I have it in Phase 4 with the data model prepared from day
   one. If interoperating with several servers at once is a day-one need for you, say so and it moves.
4. **Repo and licence.** Standalone `KevinAdams05/HiTuxShare`? HiShare is public domain, so we can
   pick anything — MIT would be my suggestion for a fork that adds substantial new code.
5. **Name.** `HiTuxShare` for the project; I would suggest `hitux` as the binary and `hituxshare` as
   the package name.

---

## 9. Size and performance budget

Targets to hold ourselves to, all achievable on the recommended stack:

| Metric | Target | Basis |
|---|---|---|
| Stripped binary | ≤ 2.5 MB | measured: muscle `chatclient` = 1.5 MB stripped |
| Runtime deps | Qt6 Core/Gui/Widgets + zlib | no bundled runtimes |
| Cold start to window | < 500 ms | HiShare 1.2 already shows the window before connecting; keep that |
| Idle CPU | 0.0% | event-driven only — no polling, no immediate-mode redraw |
| RSS, idle, connected | < 60 MB | Qt baseline is most of it |
| RSS with 20k query results | < 150 MB | virtualized model, results held as `MessageRef` |
| Build from clean | < 2 min | MUSCLE is 10 s of it |

---

## 10. References

- HiShare: https://github.com/atomozero/HiShare
- MUSCLE: https://github.com/jfriesne/muscle · https://public.msli.com/lcs/muscle/
- BeShare: https://public.msli.com/lcs/beshare/
- Key reading in the MUSCLE tree:
  - `tools/chatclient.cpp` — BeShare-compatible C++ chat client (Phase 0 starting point)
  - `lang/python3/python_chat.py` — the same protocol in 148 readable lines
  - `platform/qt/` — `QMessageTransceiverThread`, callback mechanisms, `qt_muscled_browser`
  - `util/ICallbackMechanism.h` — the toolkit seam
  - `html/Beginners Guide.html`, `html/Custom Servers.html`, `html/muscle-by-example/`
