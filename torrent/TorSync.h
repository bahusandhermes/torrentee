#pragma once

#include <string>
#include <memory>
#include <thread>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/session.hpp>
#include <atlsafe.h>
#include <optional>
#include <vector>

namespace torrent
{
    namespace lt = libtorrent;

    std::string to_string(lt::torrent_status::state_t s);

    class TorSyncSessionPool;

    class TorSync final
    {
        public:
            friend class TorSyncSessionPool;

            using OnStop = std::function<void()>;
            using OnState = std::function<void(lt::torrent_status::state_t)>;
            using OnError = std::function<void(lt::error_code ec)>;

            struct Metadata
            {
                struct File
                {
                    std::wstring path;
                    std::wstring name;
                    int64_t size = 0;
                };

                uint64_t GetTotalSize();

                std::vector<File> files;
                std::wstring parentPath;
            };

        public:
            TorSync(std::shared_ptr<lt::session> session);
            ~TorSync();

            bool Serve(const std::wstring& path, std::string* hash);
            bool Serve(const std::wstring& path, const std::wstring& torrentPath, std::string* hash);
            bool ServeTorrent(const std::wstring& torrentPath, const std::wstring& dataPath, std::string* hash);
            bool SaveTorrentFile(const std::wstring& torrentPath);
            bool Fetch(const std::string& hash, const std::wstring& dest);

            void Stop();

            int GetId() { return _id; }
            lt::torrent_handle& GetHandle() { return _handle; };
            std::optional<TorSync::Metadata> GetMetadata();
            std::string GetHash();
            lt::torrent_status::state_t GetState();
            std::wstring GetTorrentPath();
            float GetProgress();

            void BindOnState(const OnState& val);
            void UnbindOnState();

            void BindOnError(const OnError& val);
            void UnbindOnError();

            void BindOnStop(const OnStop& val);
            void UnbindOnStop();

        //private:
            void SetState(lt::torrent_status::state_t s);
            void SetHash(const std::string& hash);
            void SetTorrentPath(const std::wstring& torrentPath);

            void RaiseOnState(lt::torrent_status::state_t s);
            void RaiseOnError(lt::error_code ec);
            void RaiseOnStop();

            std::mutex _mutex;
            lt::torrent_handle _handle;
            std::shared_ptr<lt::session> _session;
            std::string _hash;
            std::wstring _torrentPath;
            int _id = 0;

            OnState _onState;
            OnError _onError;
            OnStop _onStop;

            lt::torrent_status::state_t _state = lt::torrent_status::finished;
    };

    std::optional<TorSync::Metadata> GetMetadata(const std::wstring& torrentPath);
}
