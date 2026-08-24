# Testing HiTuxShare

The acceptance test for every phase of this port is **interoperability**, not unit
tests. Unit tests cannot tell you the wire format is right; only a real server and a
real peer can. This file records how to do that reproducibly.

---

## 1. Run a private server

Never iterate against the public server. `beshare.tycomsystems.com` is a real chat
room with real people in it, and a test client that joins, renames itself six times
and leaves is noise to them. Build MUSCLE's own `muscled` and run it on localhost:

```sh
cmake -S third_party/muscle -B /tmp/muscled-build -DWITH_MUSCLED=ON \
      -DWITH_TOOLS=OFF -DWITH_EXAMPLES=OFF -DWITH_TESTS=OFF
cmake --build /tmp/muscled-build --target muscled -j"$(nproc)"

/tmp/muscled-build/muscled port=12960
```

`muscled` is the same server the public network runs, so behaviour observed against
it is behaviour you can rely on. That is how the departing-user bug in `db1e010` was
found: the server reports a user leaving as one removal per matched leaf node
(`/host/1/beshare/name`, at path depth 4), not as a single removal at session depth.
No amount of reading the BeShare source would have told us that.

## 2. Drive clients from a script

`hitux-probe` reads commands on stdin, which makes a two-party conversation a shell
script. Give each client its own `XDG_CONFIG_HOME` so they do not fight over one
settings file, and so your real configuration is never touched:

```sh
export PROBE=build/tools/probe/hitux-probe

( sleep 3; echo "hello from bob"; sleep 6; echo "/msg alice psst"; sleep 5; echo "/quit" ) \
  | XDG_CONFIG_HOME=/tmp/bob-config $PROBE 127.0.0.1 bob 12960 &

( sleep 3; echo "hi everyone"; sleep 2; echo "/me waves"; sleep 2; echo "/ping bob"; \
  sleep 3; echo "/nick alice2"; sleep 4; echo "/quit" ) \
  | XDG_CONFIG_HOME=/tmp/alice-config $PROBE 127.0.0.1 alice 12960 &

wait
```

Watch out for the timing trap this exposes: if both clients quit at the same moment,
neither observes the other leaving, and it looks as though the leave notification is
broken when it is not. Give the observer a clearly longer lifetime than the client
whose departure you are testing.

## 3. What to check against the live server

Some things only exist out there. Connect briefly, observe, and disconnect --
**without sending chat**, so you are not posting test messages into a room with
people in it:

```sh
( sleep 15; echo /quit ) | build/tools/probe/hitux-probe beshare.tycomsystems.com YourName
```

What this is worth checking for:

- **Real client diversity.** The live server has BeShare 3.04 and JavaShare peers on
  it. They exercise the legacy bare-`version` field, where a leading digit means
  classic BeShare, rather than the modern `version_name`/`version_num` pair that our
  own clients send to each other. A local test with two HiTuxShare probes will never
  cover that path.
- **Real host names and session roots**, which differ from the IPv6 link-local
  addresses a localhost server hands out.

## 4. GUI checks

The GUI can be screenshotted without a window ever appearing on your desktop by
using Qt's offscreen platform, though you then get no pixels to look at:

```sh
QT_QPA_PLATFORM=offscreen build/qt/hitux    # smoke test only
```

For an actual look, run it normally and capture just its own window. Match on
`WM_CLASS` rather than on the title -- an editor with the project open has
"HiTuxShare" in its title too, and will be grabbed instead:

```sh
for id in $(wmctrl -l | awk '{print $1}'); do
  xprop -id "$id" WM_CLASS 2>/dev/null | grep -qi hitux && WINID=$id && break
done
wmctrl -i -a "$WINID" && gnome-screenshot -w -f /tmp/hitux.png
```

## 5. Things worth testing that are easy to get wrong

- **Leaving and rejoining under the same name.** Session IDs change; nothing may be
  keyed off the name.
- **Two users with the same name.** `/msg` on an ambiguous name must reach all of
  them rather than picking one silently.
- **A very long chat line, and one containing `<b>` or `&`.** The chat log is rich
  text, so unescaped markup from a peer would render.
- **Disconnect while the server is mid-send**, e.g. kill `muscled` under a connected
  client.
