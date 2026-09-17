#include "cache_manager.h"
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace muisc {

CacheManager::CacheManager() {
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::path(".");
    cache_dir_ = base / ".cache" / "mousiki";
    std::error_code ec;
    fs::create_directories(cache_dir_, ec); // ignore failure, we surface it on first write instead
}

// FNV-1a (32-bit) over the raw title bytes, as 8 lowercase hex digits.
// Only used to disambiguate titles that contain non-ASCII bytes; small and
// dependency-free, and trivially reimplementable in the Python helper,
// which is the whole reason for choosing it over anything stronger.
static std::string fnv1a32_hex(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x01000193u;
    }
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return std::string(buf);
}

std::string sanitize_cache_name(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    bool had_non_ascii = false;
    for (unsigned char c : raw) {
        if (c >= 0x80) {
            // Dropped, exactly as before -- but remembered, so the suffix
            // below can keep two different non-ASCII titles apart.
            had_non_ascii = true;
        } else if (c >= 'a' && c <= 'z') {
            out += static_cast<char>(c);
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else if (c >= '0' && c <= '9') {
            out += static_cast<char>(c);
        } else if (c == ' ' || c == '-' || c == '_') {
            out += '_';
        }
        // everything else (slashes, quotes, punctuation) is dropped, as before
    }
    while (out.find("__") != std::string::npos) {
        out.replace(out.find("__"), 2, "_");
    }
    if (out.empty()) out = "untitled";
    if (had_non_ascii) out += "_" + fnv1a32_hex(raw);
    return out;
}

fs::path CacheManager::path_for(const std::string& title, const std::string& ext) const {
    return cache_dir_ / (sanitize_cache_name(title) + "." + ext);
}

bool CacheManager::is_cached(const std::string& title, const std::string& ext) const {
    std::error_code ec;
    auto p = path_for(title, ext);
    return fs::exists(p, ec) && fs::file_size(p, ec) > 0;
}

} // namespace muisc
