#!/usr/bin/env python3
"""Mechanically enforce the parts of docs/STYLE.md that a script can.

This is a *checker*, never a reformatter. Every finding is reported for a human
to act on, because auto-fixing style in a codebase that vendors upstream code
creates exactly the unreviewable churn we are trying to avoid.

HiTuxShare is greenfield, so there is deliberately no baseline file of
grandfathered findings: a clean run means clean, and the gate requires zero
findings rather than zero *new* ones. Please keep it that way. The moment a
baseline appears, the real standard silently becomes "whatever was already
there".

The rules worth running this for are not the formatting ones. core-qt-include,
protocol-literal, html-unescaped and core-nullptr encode architectural
decisions from docs/STYLE.md that would otherwise only be caught by somebody
remembering to look -- and the first of those is the invariant the whole
core/front-end split rests on.

third_party/ is never checked. It is upstream MUSCLE, built unmodified.

Usage:
  python3 scripts/style-check.py                  # everything tracked
  python3 scripts/style-check.py --changed        # only files changed vs HEAD
  python3 scripts/style-check.py --changed=REF    # ... vs an arbitrary ref
  python3 scripts/style-check.py --list-rules
  python3 scripts/style-check.py --self-test      # verify the checker itself

Exit status is 0 with no findings and 1 otherwise, so it can gate a commit or a
release directly.
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join("tests", "style")

TAB_WIDTH = 4
MAX_COLUMNS = 100

SOURCE_EXT = (".c", ".cpp", ".h")
SCRIPT_EXT = (".py", ".sh")
PROSE_EXT = (".md", ".svg")
TEXT_EXT = SOURCE_EXT + SCRIPT_EXT + PROSE_EXT + (".qrc", ".desktop", ".txt")

# Scope decides what a rule applies to, and it matters more than it looks. The
# column limit is a rule about *code*: applied to SVG (machine-emitted, one long
# path per line) or to markdown prose it would bury the real findings under
# hundreds of false ones. Trailing whitespace is likewise excluded from
# markdown, where two trailing spaces are a meaningful hard line break.
#   "text"   - every checked text file
#   "code"   - C/C++ plus the shell and Python we wrote
#   "source" - C/C++ only
RULES = {
    "eol-crlf":
        ("text", "Line endings must be LF, not CRLF"),
    "no-final-eol":
        ("text", "File does not end with a newline"),
    "line-too-long":
        ("code", f"Line exceeds {MAX_COLUMNS} columns at tab width {TAB_WIDTH}"),
    "trailing-space":
        ("code", "Trailing whitespace"),
    "tab-indent":
        ("source", "Indentation must use tabs, not spaces"),
    "if-zero":
        ("source", "#if 0 block - delete it, git has the history"),
    "pragma-once":
        ("source", "Use an #ifndef header guard, not #pragma once"),
    "true-false":
        ("source", "Use true/false, not TRUE/FALSE"),
    "yoda-condition":
        ("source", "Constant on the left of a comparison"),
    "pointer-space":
        ("source", "Asterisk binds to the type: 'Type* name'"),
    "todo-owner":
        ("source", "TODO must not name an author - git knows"),
    "missing-copyright":
        ("source", "Missing MIT copyright header"),
    "header-guard":
        ("source", "Header guard missing or does not match the filename"),
    "long-type":
        ("source", "'long' varies by platform - use int32/int64"),
    "core-qt-include":
        ("source", "core/ must not include Qt or any GUI toolkit header"),
    "core-nullptr":
        ("source", "core/ talks to MUSCLE's NULL-based API - use NULL there,"
            " nullptr in qt/"),
    "protocol-literal":
        ("source", "BeShare wire string must come from BeShareProtocol.h,"
            " not a literal"),
    "html-unescaped":
        ("source", "Peer-supplied text interpolated into HTML without"
            " toHtmlEscaped()"),
    "raw-printf":
        ("source", "printf outside tools/ - the GUI has no stdout"),
}


def describe(rule):
    return RULES[rule][1]


# Build output and generated trees. third_party is upstream MUSCLE and is never
# ours to restyle. FIXTURES holds deliberately-broken files and is excluded from
# a normal run, then checked explicitly by --self-test.
PRUNE_DIRS = {".git", "build", "build-clean", "third_party", "__pycache__"}

# Any of these in an #include from core/ means the toolkit has leaked in.
GUI_INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s*[<"](?:Q[A-Z]\w*|qt/|QtCore|QtGui|QtWidgets|gtk|gtkmm|'
    r'FL/|SDL|wx/)')

# Wire strings that must come from BeShareProtocol.h. Getting one of these wrong
# by a character means silently failing to interoperate, which no test we can
# run locally would catch -- that is why it is a lint rule and not a code
# review item.
#
# Only distinctive strings belong here. "userstatus", "filecount" and
# "installid" were tried and removed: they collide with ordinary local field
# names -- ApplicationSettings has its own unrelated "userstatus" settings key --
# and a rule that cries wolf is a rule people learn to skip.
PROTOCOL_LITERALS = (
    "beshare/name", "beshare/userstatus", "beshare/filecount",
    "beshare/bandwidth", "beshare/uploadstats", "beshare/files/",
    "beshare/fires/", "SUBSCRIBE:beshare", "version_name", "version_num",
    "supports_partial_hashing", "supports_ranges", "supports_ssl",
)

# The one file allowed to spell them: it is the definition site.
PROTOCOL_DEFINITION = "core/BeShareProtocol.h"

# printf is the job in a command-line tool; anywhere else it is a leftover.
PRINTF_ALLOWED_PREFIXES = ("tools/",)

# Types common enough here that a following " *name" is a declaration rather
# than a multiplication.
POINTER_TYPES = (r"char|void|u?int(?:8|16|32|64)|size_t|off_t|status_t|bool|"
    r"String|Message|Queue|Hashtable|ServerConnection|ServerConnectionListener|"
    r"UserRecord|UserRegistry|ChatMessage|ApplicationSettings|"
    r"ICallbackMechanism|MainWindow|ChatLogView|ChatInputLine|UserListModel|"
    r"Q[A-Z]\w+")

CAST_TYPES = (r"u?int(?:8|16|32|64)|char|short|long|float|double|"
    r"size_t|ssize_t|off_t|status_t")


def git(*arguments):
    """Run git, or return None if this is not a usable checkout.

    A release may be built from an unpacked tarball with no .git at all, so
    every git use has to degrade rather than raise.
    """
    try:
        result = subprocess.run(["git", "-C", REPO, *arguments],
            capture_output=True, text=True)
    except OSError:
        return None
    return result.stdout if result.returncode == 0 else None


def is_checked(path):
    if not path.endswith(TEXT_EXT):
        return False
    parts = path.split(os.sep)
    return not any(part in PRUNE_DIRS for part in parts)


def walked_files():
    """Every checkable text file under the repo, for use without git."""
    found = []
    for root, directories, names in os.walk(REPO):
        directories[:] = [d for d in directories if d not in PRUNE_DIRS]
        for name in names:
            full = os.path.join(root, name)
            relative = os.path.relpath(full, REPO)
            if is_checked(relative):
                found.append(relative)
    return sorted(found)


def tracked_files():
    """Tracked files plus new ones not yet committed.

    Untracked-but-not-ignored files are included deliberately: a brand new
    source file is exactly where a style check earns its keep, and leaving it
    out would mean the gate never saw it until after it was committed.
    """
    listed = git("ls-files")
    if listed is None:
        print("style-check: not a git checkout - walking the filesystem",
            file=sys.stderr)
        return walked_files()

    names = set(listed.split("\n"))
    untracked = git("ls-files", "--others", "--exclude-standard")
    if untracked is not None:
        names |= set(untracked.split("\n"))

    return sorted(name for name in names if name and is_checked(name))


def changed_files(reference):
    """Files that differ from (reference), for checking between commits."""
    listed = git("diff", "--name-only", "--diff-filter=ACMR", reference)
    if listed is None:
        print(f"style-check: cannot diff against {reference}", file=sys.stderr)
        return []

    names = set(listed.split("\n"))
    untracked = git("ls-files", "--others", "--exclude-standard")
    if untracked is not None:
        names |= set(untracked.split("\n"))

    return sorted(name for name in names
        if name and is_checked(name) and os.path.exists(os.path.join(REPO, name)))


def visual_width(line):
    """Column count with tabs expanded, so indentation costs what it looks."""
    width = 0
    for character in line:
        if character == "\t":
            width += TAB_WIDTH - (width % TAB_WIDTH)
        else:
            width += 1
    return width


def strip_comments(line, in_block):
    """Blank out comment text, returning (stripped, still_in_block).

    Every rule about code structure has to see code only. Doxygen blocks in this
    codebase quote protocol strings and use words like "long-running" in prose,
    and matching those produced 16 false findings out of 19 on the first run --
    which is how a checker teaches people to ignore it.
    """
    result = []
    index = 0
    while index < len(line):
        if in_block:
            end = line.find("*/", index)
            if end < 0:
                return "".join(result), True
            index = end + 2
            in_block = False
            continue

        start_line = line.find("//", index)
        start_block = line.find("/*", index)

        if start_line >= 0 and (start_block < 0 or start_line < start_block):
            result.append(line[index:start_line])
            return "".join(result), False

        if start_block >= 0:
            result.append(line[index:start_block])
            index = start_block + 2
            in_block = True
            continue

        result.append(line[index:])
        break

    return "".join(result), in_block


def strip_literals(line):
    """Blank out string and char literal contents.

    Rules about code structure -- yoda conditions, pointer spacing, TRUE/FALSE
    -- must not fire on prose inside a string. Comments are left alone, because
    a rule like todo-owner is specifically about comment text.
    """
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', line)


def in_core(path):
    return path.replace(os.sep, "/").startswith("core/")


def in_qt(path):
    return path.replace(os.sep, "/").startswith("qt/")


def expected_guard(path):
    """The header guard a file should carry, derived from its name.

    ChatLogView.h -> CHAT_LOG_VIEW_H, matching the existing headers.
    """
    stem = os.path.splitext(os.path.basename(path))[0]
    spaced = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', stem)
    spaced = re.sub(r'(?<=[A-Z])(?=[A-Z][a-z])', '_', spaced)
    return spaced.upper() + "_H"


def check_file(path, text, report):
    """Run every rule whose scope covers (path) over (text)."""
    relative = path.replace(os.sep, "/")
    is_source = path.endswith(SOURCE_EXT)
    is_code = is_source or path.endswith(SCRIPT_EXT)
    is_markdown = path.endswith(".md")

    if "\r\n" in text:
        report(path, text[:text.index("\r\n")].count("\n") + 1, "eol-crlf")

    if text and not text.endswith("\n"):
        report(path, text.count("\n") + 1, "no-final-eol")

    lines = text.split("\n")

    # Whole-file source rules.
    if is_source:
        if "Copyright" not in text or "MIT License" not in text:
            report(path, 1, "missing-copyright")

        if path.endswith(".h"):
            # Compared without underscores, because where a CamelCase name
            # splits into words is a judgement call -- BeShareProtocol.h is
            # reasonably BESHARE_PROTOCOL_H or BE_SHARE_PROTOCOL_H. The rule
            # exists to catch a guard copy-pasted from another file, which this
            # still does.
            wanted = expected_guard(path).replace("_", "")
            found_guard = re.search(r'^\s*#\s*ifndef\s+(\w+)\s*$', text, re.M)
            if found_guard is None \
                    or found_guard.group(1).replace("_", "") != wanted:
                report(path, 1, "header-guard")

    in_block_comment = False
    for number, line in enumerate(lines, start=1):
        # (uncommented) keeps string literals but drops comment prose;
        # (code) drops both, and is what the structural rules look at.
        uncommented, in_block_comment = strip_comments(line, in_block_comment)
        code = strip_literals(uncommented)

        if is_code:
            if visual_width(line) > MAX_COLUMNS:
                report(path, number, "line-too-long")
            if line != line.rstrip() and not is_markdown:
                report(path, number, "trailing-space")

        if not is_source:
            continue

        # A block-comment continuation line is space-aligned by design: the
        # asterisks in a /** ... */ block line up under the opening slash.
        if re.match(r'^ +\S', line) and not re.match(r'^ *\*', line):
            report(path, number, "tab-indent")

        if re.match(r'^\s*#\s*if\s+0\b', line):
            report(path, number, "if-zero")

        if re.match(r'^\s*#\s*pragma\s+once\b', line):
            report(path, number, "pragma-once")

        if re.search(r'\b(?:TRUE|FALSE)\b', code):
            report(path, number, "true-false")

        # A literal or an ALL_CAPS constant on the left of == or !=.
        if re.search(r'[\(\&\|]\s*(?:\d+|NULL|[A-Z][A-Z0-9_]{2,})\s*[=!]=', code):
            report(path, number, "yoda-condition")

        if re.search(rf'\b(?:{POINTER_TYPES})\s+\*\s*\w', code):
            report(path, number, "pointer-space")

        if re.search(r'\b(?:TODO|FIXME|XXX)\b\s*[\(:]?\s*[A-Z][a-z]+\b', line):
            report(path, number, "todo-owner")

        if re.search(r'\b(?<!unsigned )long\b(?!\s*long\b)', code) \
                and "long long" not in code:
            report(path, number, "long-type")

        if in_core(relative) and GUI_INCLUDE_PATTERN.match(line):
            report(path, number, "core-qt-include")

        if in_core(relative) and re.search(r'\bnullptr\b', code):
            report(path, number, "core-nullptr")

        if relative != PROTOCOL_DEFINITION:
            for literal in PROTOCOL_LITERALS:
                if f'"{literal}' in uncommented:
                    report(path, number, "protocol-literal")
                    break

        # Peer text reaching rich text without escaping. Conservative on
        # purpose: only fires when an HTML tag, a .arg() and a converted
        # muscle::String all appear together with no toHtmlEscaped in sight.
        if in_qt(relative) and ".arg(" in uncommented \
                and "ToQString(" in uncommented \
                and re.search(r'<(?:span|b|i|a|div|p|img)\b', uncommented) \
                and "toHtmlEscaped" not in uncommented:
            report(path, number, "html-unescaped")

        if re.search(r'\b(?:printf|fprintf)\s*\(', code) \
                and not relative.startswith(PRINTF_ALLOWED_PREFIXES):
            report(path, number, "raw-printf")


def read_text(path):
    try:
        with open(os.path.join(REPO, path), "rb") as handle:
            return handle.read().decode("utf-8", errors="replace")
    except OSError:
        return None


# Rules each fixture must trip. A rule absent from here has no test, and this
# list is checked for completeness by --self-test: adding a rule without a
# fixture fails the self-test rather than shipping untested.
# Several rules are scoped by path -- core-qt-include only fires under core/,
# html-unescaped only under qt/ -- so each fixture is checked under the path it
# is pretending to live at rather than its real one in tests/style/.
FIXTURE_EXPECTATIONS = {
    "bad.h": ("core/bad.h", {
        "missing-copyright", "header-guard", "pragma-once", "line-too-long",
        "trailing-space", "tab-indent", "long-type", "pointer-space",
    }),
    "bad.cpp": ("core/bad.cpp", {
        "if-zero", "true-false", "yoda-condition", "todo-owner",
        "core-qt-include", "core-nullptr", "protocol-literal", "raw-printf",
        "no-final-eol",
    }),
    "bad_qt.cpp": ("qt/bad_qt.cpp", {
        "html-unescaped",
    }),
}

GOOD_FIXTURES = {"good.h": "core/good.h", "good.cpp": "core/good.cpp"}


def self_test():
    """Verify the checker against fixtures before trusting a clean run.

    This exists because a regex anchored slightly wrong matches nothing and
    reports a clean tree, which is indistinguishable from correct code. Every
    rule needs a fixture that proves it can fire, and a good/ counterpart that
    proves it does not fire on compliant code.
    """
    failures = []

    untested = set(RULES) - {r for _, rules in FIXTURE_EXPECTATIONS.values()
        for r in rules}
    # eol-crlf needs a CRLF file, which git checkout normalisation would undo,
    # so it is exercised in-memory below instead of from a fixture on disk.
    untested.discard("eol-crlf")
    if untested:
        failures.append(f"rules with no fixture: {', '.join(sorted(untested))}")

    for name, (pretend_path, expected) in FIXTURE_EXPECTATIONS.items():
        path = os.path.join(FIXTURES, name)
        text = read_text(path)
        if text is None:
            failures.append(f"{path}: fixture missing")
            continue

        found = set()
        check_file(pretend_path, text, lambda p, n, rule: found.add(rule))

        for rule in sorted(expected - found):
            failures.append(f"{path}: expected {rule}, not reported")

    # eol-crlf, in memory.
    found = set()
    check_file("tests/style/bad.cpp", "int main()\r\n{\r\n}\r\n",
        lambda p, n, rule: found.add(rule))
    if "eol-crlf" not in found:
        failures.append("in-memory CRLF sample: expected eol-crlf")

    for name, pretend_path in GOOD_FIXTURES.items():
        path = os.path.join(FIXTURES, name)
        text = read_text(path)
        if text is None:
            failures.append(f"{path}: fixture missing")
            continue

        found = []
        check_file(pretend_path, text, lambda p, n, rule: found.append((n, rule)))
        for number, rule in found:
            failures.append(f"{path}:{number}: {rule} fired on compliant code")

    if failures:
        print("style-check --self-test FAILED:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"style-check --self-test passed "
        f"({len(RULES)} rules, "
        f"{len(FIXTURE_EXPECTATIONS) + len(GOOD_FIXTURES)} fixtures)")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Check HiTuxShare sources against docs/STYLE.md")
    parser.add_argument("--changed", nargs="?", const="HEAD", metavar="REF",
        help="only check files that differ from REF (default HEAD)")
    parser.add_argument("--list-rules", action="store_true",
        help="print the rule table and exit")
    parser.add_argument("--self-test", action="store_true",
        help="verify the checker against its own fixtures")
    parser.add_argument("paths", nargs="*",
        help="specific files to check instead of the whole tree")
    arguments = parser.parse_args()

    if arguments.list_rules:
        width = max(len(name) for name in RULES)
        for name in sorted(RULES):
            scope, description = RULES[name]
            print(f"  {name:<{width}}  [{scope:>6}]  {description}")
        return 0

    if arguments.self_test:
        return self_test()

    if arguments.paths:
        paths = [p for p in arguments.paths if is_checked(p)]
    elif arguments.changed:
        paths = changed_files(arguments.changed)
    else:
        paths = tracked_files()

    findings = []

    def report(path, number, rule):
        findings.append((path, number, rule))

    for path in paths:
        # The fixtures are broken on purpose.
        if path.replace(os.sep, "/").startswith(FIXTURES.replace(os.sep, "/")):
            continue
        text = read_text(path)
        if text is not None:
            check_file(path, text, report)

    if not findings:
        scope = f" changed vs {arguments.changed}" if arguments.changed else ""
        print(f"style-check: clean ({len(paths)} files{scope})")
        return 0

    findings.sort()
    for path, number, rule in findings:
        print(f"{path}:{number}: {rule}: {describe(rule)}")

    counts = {}
    for _, _, rule in findings:
        counts[rule] = counts.get(rule, 0) + 1

    print(f"\nstyle-check: {len(findings)} finding(s) in {len(paths)} file(s)")
    for rule in sorted(counts, key=lambda r: -counts[r]):
        print(f"  {counts[rule]:>4}  {rule}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
