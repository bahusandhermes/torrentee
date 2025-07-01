#pragma once

#include <libtorrent/session.hpp>
#include "TorSync.h"

namespace torrent
{
    namespace lt = libtorrent;

    class TorSyncSessionPool final
    {
        public:
            struct InitData
            {
                bool global = true;
                int slotLimit = -1;
            };

        public:
            TorSyncSessionPool(const InitData& idata);
            ~TorSyncSessionPool();

            std::shared_ptr<lt::session> GetSession() { return _session; }

            void Register(std::shared_ptr<torrent::TorSync> torSync);
            void Unregister(std::shared_ptr<torrent::TorSync> torSync);

        private:
            std::shared_ptr<torrent::TorSync> GetTorSync(const lt::torrent_handle& handle);
            void Worker();

            std::mutex _mutex;
            InitData _idata;
            std::shared_ptr<lt::session> _session;
            CHandle _stopEvent;
            std::thread _worker;

            std::unordered_map<int, std::shared_ptr<torrent::TorSync>> _torSyncs;


    };
}
