# Application icon

`hituxshare.svg` is converted from `hishare.hvif` in
[HiShare](https://github.com/atomozero/HiShare) — the holly-in-a-pot that BeShare has
carried since 1999. The PNGs are rendered from that same HVIF at each size rather
than downscaled from one another, so small sizes stay crisp.

The artwork was **not** redrawn. It was converted from the original vector source with
[`icon2icon`](https://github.com/KevinAdams05/hvif-tools):

```sh
icon2icon hishare.hvif hituxshare.svg
icon2icon hishare.hvif hituxshare-48.png --width 48 --height 48
```

HiShare's application code and assets under `source/hishare/` were released into the
public domain by Jeremy Friesner (BeShare, 1999–2012) and atomozero (HiShare, 2026),
so reuse here is unencumbered. HiTuxShare is a port of HiShare and deliberately keeps
the family identity rather than inventing a new one.
