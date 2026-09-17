#!/usr/bin/env python3
"""Fetch album cover art for a track via Apple's keyless iTunes Search API.

Usage:  python3 scripts/fetch_art.py "<title>" ["<artist>"]

Saves the cover to ~/.cache/mousiki/art/<slug>.jpg, where <slug> is produced
by the same algorithm as muisc::CacheManager::sanitize() in
src/cache_manager.cpp, applied to the TITLE ONLY (never the artist).

Prints exactly one line of JSON to stdout and always exits 0:

  success   {"ok": true, "path": "...", "cached": false, "exact": true,
             "artist": "...", "track": "..."}
  failure   {"ok": false, "error": "NOT_FOUND", "detail": "..."}

Standard library only - no third-party packages.
"""

import json
import os
import signal
import sys
import urllib.parse
import urllib.request

SEARCH_URL = "https://itunes.apple.com/search"
NET_TIMEOUT = 8       # per-request socket timeout (seconds)
HARD_TIMEOUT = 20     # hard alarm, so a hung socket still yields JSON
USER_AGENT = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)"

_emitted = False


def emit(obj):
    """Write the single JSON result line and exit 0.  Never returns.

    Written to stdout.buffer with ensure_ascii=False, because a slug can
    legitimately contain bytes that are not valid UTF-8 (see sanitize()).
    The C++ reader (json_get_string / json_unescape in lyrics_fetcher.cpp)
    copies raw bytes through verbatim, but turns a lone "\\udcXX" escape -
    which is what json.dumps() would emit by default for such a byte - into
    a different 3-byte sequence.  Raw bytes are what the filesystem has, so
    raw bytes are what we print.
    """
    global _emitted
    # Disarm the alarm first: the handler must never interleave with the
    # one and only line we write.
    try:
        signal.alarm(0)
    except (AttributeError, OSError):
        pass
    if not _emitted:
        _emitted = True
        try:
            line = json.dumps(obj, ensure_ascii=False) + "\n"
            sys.stdout.buffer.write(line.encode("utf-8", "surrogateescape"))
            sys.stdout.buffer.flush()
        except Exception:
            # Last-ditch: a plain ASCII-safe line is better than none.
            try:
                sys.stdout.write(json.dumps(obj) + "\n")
                sys.stdout.flush()
            except Exception:
                pass
    sys.exit(0)


def fail(error, detail):
    emit({"ok": False, "error": error, "detail": str(detail)})


def _on_alarm(signum, frame):
    # SIGALRM's default action would kill us silently and the C++ caller
    # would get no JSON at all, so emit instead.
    fail("TIMEOUT", "timed out after %ds" % HARD_TIMEOUT)


def arm_alarm():
    # Same best-effort pattern as fetch_lyrics.py (absent on Windows and in
    # some restricted environments).
    try:
        signal.signal(signal.SIGALRM, _on_alarm)
        signal.alarm(HARD_TIMEOUT)
    except (AttributeError, OSError, ValueError):
        pass


# --------------------------------------------------------------------------
# Slug: byte-for-byte port of muisc::sanitize_cache_name()
# --------------------------------------------------------------------------
#
# The C++ (src/cache_manager.cpp) is:
#
#     for (unsigned char c : raw) {
#         if      (c >= 0x80)              had_non_ascii = true;   // dropped
#         else if (c >= 'a' && c <= 'z')   out += c;
#         else if (c >= 'A' && c <= 'Z')   out += c - 'A' + 'a';
#         else if (c >= '0' && c <= '9')   out += c;
#         else if (c==' '||c=='-'||c=='_') out += '_';
#     }                                    // everything else dropped
#     while (out.find("__") != npos) out.replace(out.find("__"), 2, "_");
#     if (out.empty()) out = "untitled";
#     if (had_non_ascii) out += "_" + fnv1a32_hex(raw);
#
# Classification is deliberately ASCII-only, so this port needs no ctypes
# and no locale bootstrap -- an earlier version of this file had both,
# because sanitize() used locale-dependent isalnum/tolower and the two
# implementations had to agree byte-for-byte about 65 high bytes that macOS
# libc calls alphanumeric. That produced filenames APFS rejects outright
# (EILSEQ), so non-ASCII titles could never be cached at all; the C++ is
# now ASCII-only with an FNV-1a suffix, and every platform agrees.

_TABLE = [b""] * 256
for _b in range(256):
    if 48 <= _b <= 57 or 97 <= _b <= 122:        # 0-9 a-z
        _TABLE[_b] = bytes((_b,))
    elif 65 <= _b <= 90:                         # A-Z -> lowercase
        _TABLE[_b] = bytes((_b + 32,))
    elif _b in (0x20, 0x2D, 0x5F):               # space, '-', '_'
        _TABLE[_b] = b"_"


def _fnv1a32_hex(raw):
    """FNV-1a (32-bit) over `raw` bytes, as 8 lowercase hex digits."""
    h = 0x811C9DC5
    for b in raw:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return b"%08x" % h


def sanitize_bytes(raw):
    """raw: bytes -> slug bytes (exactly what sanitize_cache_name returns)."""
    had_non_ascii = any(b >= 0x80 for b in raw)
    out = b"".join(_TABLE[b] for b in raw)
    # Mirrors the C++ `while (find("__") != npos) replace(pos, 2, "_")` loop,
    # which collapses any run of underscores down to one.
    while b"__" in out:
        i = out.find(b"__")
        out = out[:i] + b"_" + out[i + 2:]
    out = out or b"untitled"
    if had_non_ascii:
        out += b"_" + _fnv1a32_hex(raw)
    return out


def sanitize(title):
    """title: str -> slug str (surrogateescape-decoded, filesystem-exact)."""
    return os.fsdecode(sanitize_bytes(os.fsencode(title)))


def art_path(title):
    base = os.path.join(os.path.expanduser("~"), ".cache", "mousiki", "art")
    return os.path.join(base, sanitize(title) + ".jpg")


# --------------------------------------------------------------------------
# iTunes Search
# --------------------------------------------------------------------------

def http_get(url):
    req = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "*/*",
    })
    with urllib.request.urlopen(req, timeout=NET_TIMEOUT) as resp:
        return resp.read()


def looks_like_image(data):
    if len(data) < 12:
        return False
    if data[:2] == b"\xff\xd8":                        # JPEG
        return True
    if data[:8] == b"\x89PNG\r\n\x1a\n":               # PNG
        return True
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":  # WebP
        return True
    return False


def pick_result(results, artist):
    """Return (chosen, exact).

    Prefer a result whose artistName case-insensitively matches the passed
    artist (either string containing the other).  Otherwise take the first
    result and flag the match as non-exact.  Only results that actually
    carry artwork are considered.
    """
    usable = [r for r in results
              if isinstance(r, dict) and r.get("artworkUrl100")]
    if not usable:
        return None, False

    want = (artist or "").strip().lower()
    if want:
        for r in usable:
            have = str(r.get("artistName") or "").strip().lower()
            if have and (want in have or have in want):
                return r, True

    return usable[0], False


def save_atomic(path, data):
    """Temp file + rename, so an interrupted write never leaves a non-empty
    file behind for the cache check to trust."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.%d.part" % (path, os.getpid())
    try:
        with open(tmp, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, path)
    except Exception:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


def run():
    if len(sys.argv) < 2 or not sys.argv[1].strip():
        fail("EXCEPTION", "usage: fetch_art.py <title> [artist]")

    title = sys.argv[1]
    artist = sys.argv[2] if len(sys.argv) > 2 else ""

    path = art_path(title)

    # Already cached?  The slug depends on the title alone, so this costs no
    # network round-trip.
    try:
        if os.path.isfile(path) and os.path.getsize(path) > 0:
            emit({
                "ok": True,
                "path": path,
                "cached": True,
                "exact": True,
                "artist": artist,
                "track": title,
            })
    except OSError:
        pass  # unreadable for some reason - fall through and re-fetch

    term = ("%s %s" % (title, artist)).strip()
    url = SEARCH_URL + "?" + urllib.parse.urlencode({
        "term": term,
        "entity": "song",
        "limit": 5,
    })

    try:
        raw = http_get(url)
    except Exception as e:
        fail("NETWORK", "search request failed: %s" % e)

    try:
        payload = json.loads(raw.decode("utf-8", "replace"))
    except Exception as e:
        fail("EXCEPTION", "could not parse search response: %s" % e)

    results = payload.get("results") if isinstance(payload, dict) else None
    if not results:
        fail("NOT_FOUND", "no iTunes results for: %s" % term)

    chosen, exact = pick_result(results, artist)
    if chosen is None:
        fail("NOT_FOUND", "no artwork in iTunes results for: %s" % term)

    # artworkUrl100 ends in .../100x100bb.jpg; ask for the 600px render.
    art_url = str(chosen.get("artworkUrl100") or "")
    art_url = art_url.replace("100x100bb", "600x600bb")
    if not art_url:
        fail("NOT_FOUND", "chosen result has no artworkUrl100 for: %s" % term)

    try:
        data = http_get(art_url)
    except Exception as e:
        fail("NETWORK", "artwork download failed: %s" % e)

    if not data:
        fail("NOT_FOUND", "artwork download was empty: %s" % art_url)
    if not looks_like_image(data):
        fail("BAD_IMAGE", "artwork is not JPEG/PNG/WebP: %s" % art_url)

    try:
        save_atomic(path, data)
    except Exception as e:
        fail("WRITE_FAILED", "could not write %s: %s" % (path, e))

    emit({
        "ok": True,
        "path": path,
        "cached": False,
        "exact": exact,
        "artist": str(chosen.get("artistName") or artist),
        "track": str(chosen.get("trackName") or title),
    })


def main():
    arm_alarm()
    try:
        run()
    except SystemExit:
        raise
    except BaseException as e:
        # Absolute last resort: still one JSON line, still exit 0.
        fail("EXCEPTION", "%s: %s" % (type(e).__name__, e))


if __name__ == "__main__":
    main()
