#!/usr/bin/env python3
"""Gzip the portal into src/web/web_assets.h.

The panel serves the portal out of loop(), so the picture stands still while a
browser is pulling it. The page, its style and its script are 144 KB of flash
sent as plain text; gzipped they are under 33 KB, and the browser unpacks them
for free.

The sources stay where they are - the raw strings in src/web/web_pages.h - so
editing the portal does not change: this only rewrites the generated header.

  python3 tools/web_assets_gen.py           # regenerate
  python3 tools/web_assets_gen.py --check   # exit 1 if web_assets.h is stale

The build runs --check (platformio.ini, extra_scripts), so a header that no
longer matches its source fails the build instead of shipping a page that
disagrees with the repository.

The page is static now: what used to be a %TOKEN% resolved while the page
streamed is fetched from /api/portal instead. One thing is still resolved here,
at build time and never on the panel:

  ?v=%ASSETVER%   after /portal.css and /portal.js: that file's own content
                  hash, so a browser keeps its copy for exactly as long as the
                  bytes stay the same

and two things are refused, because either would ship a page that cannot work:

  * any %TOKEN% left in the page - nothing resolves them any more;
  * a named control in the settings form that handlePortalValues() does not
    fill - it would show its HTML default, and Save would write that back.

The gzip carries no file name and mtime 0. A rerun keeps a blob that still
decompresses to its source, so gzip from another Python on another machine
neither rewrites the header nor makes --check call it stale.
"""

import gzip, hashlib, io, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "src/web"
OUT = WEB / "web_assets.h"

# (C variable in web_pages.h, name in the generated header, what it is served as)
ASSETS = [
    ("PORTAL_CSS", "PORTAL_CSS_GZ", "/portal.css"),
    ("PORTAL_JS", "PORTAL_JS_GZ", "/portal.js"),
    ("FAVICON_SVG", "FAVICON_SVG_GZ", "/favicon.svg"),
    ("PAGE_HTML", "PAGE_HTML_GZ", "/"),
]

# Controls in the settings form that are not settings, with why.
NOT_SETTINGS = {
    "resetSpriteColors": "resets the sprite colors on this one save; unchecked on every load",
    "resetScope": "restores the oscilloscope defaults on this one save; unchecked on every load",
}


def check_page(page, web_cpp):
    """Refuse a page the firmware can no longer fill."""
    left = sorted(set(re.findall(r"%[A-Z0-9_]{2,}%", page)))
    if left:
        raise SystemExit("web_assets_gen: nothing resolves these any more: " + ", ".join(left))
    filled = set(re.findall(r'form\["([A-Za-z0-9_]+)"\]', web_cpp))
    named = set()
    for tag in re.findall(r"<(?:input|select|textarea)\b[^>]*>", page):
        name = re.search(r'name="([^"]+)"', tag)
        if not name or 'type="submit"' in tag:
            continue
        named.add(name[1])
    missing = sorted(n for n in named - filled if n not in NOT_SETTINGS)
    if missing:
        raise SystemExit("web_assets_gen: handlePortalValues() does not fill " + ", ".join(missing) +
                         " - the page would show its HTML default, and Save would write that back.")


def source_text(text, var):
    """The text of `static const char VAR[] PROGMEM = R"d(...)d"`, as the compiler sees it."""
    raw = re.search(r'\b%s\[\]\s*PROGMEM\s*=\s*R"(\w*)\((.*?)\)\1"' % var, text, re.S)
    if not raw:
        raise SystemExit(f"web_assets_gen: no raw string {var} in src/web/web_pages.h")
    return raw[2]


def deflate(data):
    buf = io.BytesIO()
    # mtime 0 and no file name: the same bytes on every machine.
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as gz:
        gz.write(data)
    return buf.getvalue()


def c_array(name, blob):
    lines = [f"static const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(blob), 16):
        row = ", ".join("0x%02x" % b for b in blob[i:i + 16])
        lines.append(f"  {row},")
    lines.append("};")
    return "\n".join(lines)


def previous_blob(text, name):
    """The blob already in the header, so an identical asset is not rewritten."""
    m = re.search(r"static const uint8_t %s\[\] PROGMEM = \{(.*?)\};" % name, text, re.S)
    if not m:
        return None
    return bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", m[1]))


def build():
    old = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
    pages = (WEB / "web_pages.h").read_text(encoding="utf-8")
    web_cpp = (WEB / "web.cpp").read_text(encoding="utf-8")
    sources = {var: source_text(pages, var) for var, _, _ in ASSETS}
    # ?v= carries each file's own hash, so one changing does not expire the other.
    for var, url in (("PORTAL_CSS", "/portal.css"), ("PORTAL_JS", "/portal.js")):
        digest = hashlib.sha256(sources[var].encode("utf-8")).hexdigest()[:12]
        sources["PAGE_HTML"] = sources["PAGE_HTML"].replace(url + "?v=%ASSETVER%", url + "?v=" + digest)
    check_page(sources["PAGE_HTML"], web_cpp)
    parts = [
        "// Generated by tools/web_assets_gen.py - do not edit.",
        "//",
        "// The portal, gzipped. Sources: the raw strings in web_pages.h. Rerun the",
        "// generator after changing them; the build checks that it was run.",
        "",
        "#ifndef WEB_ASSETS_H",
        "#define WEB_ASSETS_H",
        "",
        "#include <Arduino.h>",
        "",
    ]
    report = []
    for var, name, url in ASSETS:
        raw = sources[var].encode("utf-8")
        blob = deflate(raw)
        kept = previous_blob(old, name)
        if kept is not None:
            try:
                if gzip.decompress(kept) == raw:
                    blob = kept  # another zlib, same source: leave the header alone
            except Exception:
                pass
        parts.append(f"// {url}: {len(raw):,} B source -> {len(blob):,} B gzip")
        parts.append(c_array(name, blob))
        parts.append("")
        report.append((url, len(raw), len(blob)))
        if var == "PAGE_HTML":
            parts.append('#define WEB_INDEX_ETAG "\\"%s\\""' % hashlib.sha256(blob).hexdigest()[:12])
            parts.append("")
    parts.append("#endif // WEB_ASSETS_H")
    return "\n".join(parts) + "\n", report


def main():
    text, report = build()
    check = "--check" in sys.argv
    current = OUT.read_text(encoding="utf-8") if OUT.exists() else None
    if check:
        if current == text:
            return 0
        print(f"web_assets_gen: src/web/web_assets.h is stale - run "
              f"`{sys.executable} tools/web_assets_gen.py` and commit the result.", file=sys.stderr)
        return 1
    # newline="\n": the header is committed, and CRLF would rewrite every line of it.
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    for url, raw, blob in report:
        print(f"  {url:<14} {raw:>8,} B -> {blob:>7,} B ({blob * 100 // raw}%)")
    print(f"{OUT.relative_to(ROOT)}: {len(text):,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
