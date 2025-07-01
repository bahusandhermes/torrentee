#pragma once

#include <array>
#include <string>

namespace torrent {
    namespace constants {
        // Extension used for torrent files
        inline const wchar_t* TORRENT_EXT = L".torrent";

        // User agent string for libtorrent
        inline const char* TORRENT_USER_AGENT = "TorSync/1.0";

        // Default trackers used when fetching torrents by info hash
        inline const std::array<std::string, 3> DEFAULT_TRACKERS{
            "udp://tracker.opentrackr.org:1337/announce",
            "udp://open.stealth.si:80/announce",
            "udp://tracker.openbittorrent.com:6969/announce"
        };
    }
}
