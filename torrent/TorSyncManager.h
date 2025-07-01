#pragma once

#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include "TorSync.h"
#include "TorSyncSessionPool.h"

namespace torrent
{
    class TorSyncManager final
    {
        public:
            class Handle
            {
                public:
                    Handle(std::shared_ptr<TorSync> torSync, const std::string& hash);
                    virtual ~Handle();

                    std::shared_ptr<TorSync> GetTorSync();
                    std::string GetHash();
                    std::optional<lt::error_code> GetErrorCode();
                    lt::torrent_status::state_t GetState();

                protected:
                    virtual void OnState(lt::torrent_status::state_t);
                    virtual void OnError(lt::error_code ec);
                    virtual void OnStop();

                    std::mutex _mutex;
                    std::shared_ptr<TorSync> _torSync;
                    std::string _hash;
                    std::optional<lt::error_code> _ec;
                    lt::torrent_status::state_t _state;
            };

            class ServeHandle : public Handle
            {
                public:
                    ServeHandle(std::shared_ptr<TorSync> torSync, const std::string& hash);
            };

            class FetchHandle : public Handle
            {
                public:
                    FetchHandle(std::shared_ptr<TorSync> torSync, const std::string& hash);
                    void WaitUntilFinished();
                    void WaitWithTimeout(DWORD metadataTimeoutMs = INFINITE);

                protected:
                    virtual void OnState(lt::torrent_status::state_t) override;
                    virtual void OnError(lt::error_code ec) override;
                    virtual void OnStop() override;

                private:
                    CHandle _event;
            };

        public:
            TorSyncManager();
            ~TorSyncManager();

            void Stop();

            std::shared_ptr<ServeHandle> Serve(const std::wstring& path);
            std::shared_ptr<ServeHandle> Serve(const std::wstring& path, const std::wstring& torrentPath);
            std::shared_ptr<ServeHandle> ServeTorrent(const std::wstring& torrentPath, const std::wstring& dataPath);

            std::shared_ptr<FetchHandle> Fetch(const std::string& hash, const std::wstring& dest);

            std::map<std::string, std::shared_ptr<TorSync>> GetTorSyncs();
            std::shared_ptr<TorSync> GetTorSync(const std::string& hash);
            void RemoveTorSync(const std::string& hash);

        private:
            bool AddIfNotExists(const std::string& hash, std::shared_ptr<TorSync> torSync);

            std::mutex _mutex;
            std::map<std::string, std::shared_ptr<TorSync>> _torSyncs;
            std::shared_ptr<TorSyncSessionPool> _sessionPool;
    };
}
