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
# Slug: byte-for-byte port of muisc::CacheManager::sanitize()
# --------------------------------------------------------------------------
#
# The C++ is:
#
#     for (unsigned char c : raw) {
#         if (std::isalnum(c))            out += std::tolower(c);
#         else if (c==' '||c=='-'||c=='_') out += '_';
#     }                                   // everything else dropped
#     while (out.find("__") != npos) out.replace(out.find("__"), 2, "_");
#     if (out.empty()) out = "untitled";
#
# isalnum/tolower are LOCALE-DEPENDENT, and main.cpp runs setlocale(LC_ALL,"")
# before any of this.  That matters: on macOS under a UTF-8 locale libc
# classifies 65 of the bytes 0x80-0xFF as alnum and lowercases 0xC0-0xDE to
# 0xE0-0xFE, so "Björk" (62 6A C3 B6 72 6B) slugs to b"bj\xe3rk" - not to
# "bjrk" as an ASCII-only port would produce, and not valid UTF-8 either.
# glibc and bionic classify no high byte as alnum, so there the same title
# slugs to "bjrk".  A hardcoded table would therefore write art files where
# the C++ never looks.  So we ask the very same libc, through the very same
# locale sequence, and fall back to ASCII-only if that is not possible.
#
# KNOWN UPSTREAM BUG (src/cache_manager.cpp, not fixable from here): on macOS
# that slug is invalid UTF-8, and APFS rejects such filenames outright -
# fopen() gives EILSEQ (errno 92) to C and Python alike.  So non-ASCII titles
# report {"ok":false,"error":"WRITE_FAILED",...} on macOS.  We deliberately do
# NOT substitute a writable name: CacheManager::path_for() would compute the
# same unopenable path and never find it, so a fallback would only hide the
# breakage.  The real fix is to make sanitize() ASCII-only (or percent-encode
# non-ASCII) in cache_manager.cpp, which also affects its .opus cache paths.

def _ascii_table():
    table = [b""] * 256
    for b in range(256):
        if 48 <= b <= 57 or 97 <= b <= 122:      # 0-9 a-z
            table[b] = bytes((b,))
        elif 65 <= b <= 90:                      # A-Z -> lowercase
            table[b] = bytes((b + 32,))
        elif b in (0x20, 0x2D, 0x5F):            # space, '-', '_'
            table[b] = b"_"
    return table


def _libc_table():
    """Per-byte table built from libc's own isalnum/tolower, or None."""
    import ctypes
    import ctypes.util
    import locale

    # Mirror main.cpp's locale bootstrap exactly.
    try:
        locale.setlocale(locale.LC_ALL, "")
    except (locale.Error, ValueError):
        try:
            locale.setlocale(locale.LC_ALL, "C.UTF-8")
        except (locale.Error, ValueError):
            pass
    else:
        try:
            if locale.setlocale(locale.LC_CTYPE) == "C":
                locale.setlocale(locale.LC_ALL, "C.UTF-8")
        except (locale.Error, ValueError):
            pass

    name = ctypes.util.find_library("c")
    libc = ctypes.CDLL(name) if name else ctypes.CDLL(None)
    for fn in (libc.isalnum, libc.tolower):
        fn.argtypes = [ctypes.c_int]
        fn.restype = ctypes.c_int

    # Sanity-check before trusting it; a wrong table is worse than ASCII.
    if not libc.isalnum(ord("a")) or not libc.isalnum(ord("7")):
        return None
    if libc.isalnum(ord("!")) or libc.isalnum(ord(" ")):
        return None
    if libc.tolower(ord("A")) != ord("a") or libc.tolower(ord("z")) != ord("z"):
        return None

    table = [b""] * 256
    for b in range(256):
        if libc.isalnum(b):
            table[b] = bytes((libc.tolower(b) & 0xFF,))
        elif b in (0x20, 0x2D, 0x5F):
            # Plain char comparisons in the C++, not locale-dependent.
            table[b] = b"_"
    return table


def _build_table():
    try:
        table = _libc_table()
        if table:
            return table
    except Exception:
        pass
    return _ascii_table()


_TABLE = _build_table()


def sanitize_bytes(raw):
    """raw: bytes -> slug bytes (exactly what CacheManager::sanitize returns)."""
    out = b"".join(_TABLE[b] for b in raw)
    # Mirrors the C++ `while (find("__") != npos) replace(pos, 2, "_")` loop,
    # which collapses any run of underscores down to one.
    while b"__" in out:
        i = out.find(b"__")
        out = out[:i] + b"_" + out[i + 2:]
    return out or b"untitled"


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
