#pragma once
#include <string>
#include <vector>

namespace muisc {

struct OnlineResult {
    std::string video_id;
    std::string title;
    std::string uploader;
    double duration_sec = -1.0; // -1 = unknown (not every extractor/flat-listing includes it)
};

class OnlineSource {
public:
    // `ytsearch<count>:query` with --flat-playlist so this only lists
    // results (fast, no per-video metadata fetch) — actual download only
    // happens once the user picks one (see YoutubeSource::resolve_by_id).
    std::vector<OnlineResult> search(const std::string& query, int count = 15);

    // Lists every video in a YouTube playlist (or, harmlessly, just the
    // one video if given a plain video URL) via yt-dlp --flat-playlist,
    // same fast listing-only approach as search() -- nothing is
    // downloaded here, just enumerated so the caller can queue all of
    // them. `error_out`, if given, is filled in on failure (empty
    // result, non-zero exit, or an unparseable link).
    std::vector<OnlineResult> list_playlist(const std::string& url, std::string* error_out = nullptr);
};

} // namespace muisc
