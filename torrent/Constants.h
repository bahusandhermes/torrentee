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
        inline const std::array<std::string, 10> DEFAULT_TRACKERS{
            "udp://tracker.opentrackr.org:1337/announce",
            "udp://open.stealth.si:80/announce",
            "udp://tracker.openbittorrent.com:6969/announce",
            "udp://p4p.arenabg.com:1337/announce",
            "udp://p4p.arenabg.ch:1337/announce",
            "udp://tracker.torrent.eu.org:451/announce",
            "http://tracker.opentrackr.org:1337/announce",
            "http://open.stealth.si:80/announce",
            "http://tracker.openbittorrent.com:6969/announce",
            "udp://tracker.leechers-paradise.org:6969/announce"
        };

        // Default DHT routers to improve metadata lookup
        inline const std::array<std::pair<std::string, int>, 4> DEFAULT_DHT_ROUTERS{
            std::pair{"router.bittorrent.com", 6881},
            std::pair{"dht.transmissionbt.com", 6881},
            std::pair{"router.utorrent.com", 6881},
            std::pair{"router.bt.ouinet.work", 6881}
        };
    }
}
