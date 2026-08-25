# Coding style

HiTuxShare follows the [Haiku coding
guidelines](https://www.haiku-os.org/development/coding-guidelines/), because that is
the house style of the project this is ported from and of the person maintaining it.
Where Qt or MUSCLE have a conflicting convention of their own, theirs wins locally --
the point of a style is to stop code looking foreign, and Qt code written in Haiku
style looks as foreign as the reverse.

## The Haiku rules we keep

- Tabs for indentation, 100 columns.
- `UpperCamelCase` for types and functions, `lowerCamelCase` for variables.
- `f` prefix on class member variables, `k` on constants, `s` on statics.
- Return type on its own line above the function name in definitions.
- Two blank lines between functions.
- Single-statement `if`/`else`/`for` bodies take no braces and go on their own line.
- Explicit comparisons: `if (pointer != NULL)`, `if ((flags & kMask) != 0)`.
- No abbreviations. `message`, not `msg`; `index`, not `idx`. The short forms that
  stay are the universally understood ones -- `id`, `url`, `tcp`, `i`/`j`/`k` for
  tight loop indices.
- C++ casts, const correctness, no `goto`, files end with a newline.

## Where we deviate, and why

- **`nullptr`, not `NULL`, in Qt code.** The Qt front-end is modern C++ and Qt's own
  headers use `nullptr`. Core code that talks to MUSCLE keeps `NULL`, because MUSCLE's
  API is written against it and mixing the two within one expression reads badly.
- **Qt overrides keep Qt's names.** `paintEvent`, `keyPressEvent`, `rowCount` and
  slots are lowerCamelCase because they are Qt's identifiers, not ours. Our own
  methods on the same class stay UpperCamelCase.
- **Private methods take a leading underscore**, e.g. `_HandleDataItems()`. Haiku uses
  this and it earns its keep here: it makes the boundary between the class's contract
  and its internals visible at every call site.
- **Plain data records use bare public members.** `UserRecord` and `ChatMessage` have
  no `f` prefixes and no accessors. They are assembled field by field from separate
  server messages that arrive in no fixed order, so accessors would be fifteen pairs
  of one-line functions buying nothing.
- **MUSCLE types where MUSCLE is involved.** `int32`, `uint64`, `status_t` and
  `muscle::String` rather than the `std` equivalents, because that is what every
  MUSCLE signature takes and converting at each boundary would be noise. This happens
  to match Haiku's own type conventions.

## Rules that are not about formatting

- **`core/` must never include a Qt header.** This is what keeps the FLTK, TUI and
  headless front-ends possible and the core testable without a display. It is checked
  by reading, not by tooling, so it needs saying out loud.
- **Every byte from a peer is untrusted.** Message text is escaped before it reaches
  the rich-text log. When Phase 2 lands, file names and paths out of `FILE_HEADER`
  must be sanitised before touching the filesystem -- `..`, absolute paths and
  embedded separators are all things a hostile peer can send.
- **Comments say why, not what.** A comment that restates the code is worse than none;
  a comment recording a measurement, a protocol constraint or a rejected alternative
  is worth more than the code it sits above.

## The linter

`scripts/style-check.py` mechanically enforces the parts of this document a
script can. It is a **checker, never a reformatter** — auto-fixing style in a
tree that vendors upstream code creates exactly the unreviewable churn we are
trying to avoid.

```sh
python3 scripts/style-check.py                 # everything tracked
python3 scripts/style-check.py --changed       # only what differs from HEAD
python3 scripts/style-check.py --changed=REF   # ... or from any ref
python3 scripts/style-check.py --list-rules
python3 scripts/style-check.py --self-test
```

Exit status is 0 when clean, so it gates a commit or a release directly. Run
`--changed` between commits; run the full sweep before a release.

**There is deliberately no baseline file.** This is a greenfield repo, so a
clean run means clean and the gate requires zero findings rather than zero *new*
ones. The moment a baseline appears, the real standard silently becomes
"whatever was already there".

**`--self-test` is not optional ceremony.** A regex anchored slightly wrong
matches nothing and reports a clean tree, which is indistinguishable from
correct code. Every rule must have a fixture in `tests/style/` that proves it
can fire, and the good fixtures prove it does not fire on compliant code — the
self-test fails if any rule lacks one. It earned this on its first run by
catching `tab-indent` firing on every Doxygen continuation line.

The formatting rules are the cheap half. The ones worth the run are the four
that encode decisions from this document:

| Rule | What it protects |
|---|---|
| `core-qt-include` | The invariant the whole core/front-end split rests on |
| `protocol-literal` | Wire strings must come from `BeShareProtocol.h` — a typo'd node path fails silently against real peers and no local test would catch it |
| `html-unescaped` | Peer text reaching the rich-text log without escaping |
| `core-nullptr` | `NULL` in core (MUSCLE's API), `nullptr` in Qt |

### A finding is a claim about the code, not about the rule

The first full run produced 19 findings, and **all 19 were false positives** —
rules matching prose inside comments ("long-running", a `"beshare/name"` node
quoted in a doc comment) and a guess about where `BeShareProtocol.h` splits into
words. The fix was to the linter, not the code. If a rule fires on something
correct, fix the rule and add a fixture, because a checker people learn to
ignore is worse than no checker at all.
