#pragma once
#include <filesystem>
#include <string>

namespace muisc {

namespace fs = std::filesystem;

// The one and only cache-filename rule, shared by the audio cache and the
// album-art cache (and mirrored in scripts/fetch_art.py, which must stay
// in lockstep).
//
// This used to use std::isalnum/std::tolower, which are LOCALE-DEPENDENT:
// main.cpp calls setlocale(LC_ALL, ""), and under a macOS UTF-8 locale
// libc classifies 65 of the bytes 0x80-0xFF as alphanumeric and lowercases
// 0xC0-0xDE. A title like "Bjork" spelled with an umlaut therefore slugged
// to raw high bytes that are not valid UTF-8 -- and APFS rejects such
// filenames outright with EILSEQ, so neither the cached .opus nor its
// cover could ever be written. glibc classifies no high byte as alnum, so
// the same title behaved differently on Linux.
//
// Now: ASCII-only classification, so every platform agrees. Non-ASCII
// bytes are dropped as before, but if any were present the slug gets an
// 8-hex-digit FNV-1a suffix of the original title. That keeps distinct
// non-ASCII titles from colliding (without it, every all-CJK title would
// slug to "untitled" and share one cache entry). Pure-ASCII titles are
// byte-identical to the old behaviour, so existing caches stay valid.
std::string sanitize_cache_name(const std::string& raw);

class CacheManager {
public:
    CacheManager();

    // $HOME/.cache/muisc  (created if missing)
    const fs::path& cache_dir() const { return cache_dir_; }

    // Deterministic, filesystem-safe path for a given song title, e.g.
    // "Never Gonna Give You Up" -> ~/.cache/muisc/never_gonna_give_you_up.opus
    fs::path path_for(const std::string& title, const std::string& ext = "opus") const;

    bool is_cached(const std::string& title, const std::string& ext = "opus") const;

private:
    fs::path cache_dir_;
};

} // namespace muisc
