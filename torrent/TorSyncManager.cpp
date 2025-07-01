#include "TorSyncManager.h"

namespace torrent {

TorSyncManager::Handle::Handle(std::shared_ptr<TorSync> torSync, const std::string& hash)
: _torSync(torSync)
, _hash(hash)
{
    _torSync->BindOnState(std::bind(&TorSyncManager::Handle::OnState, this, std::placeholders::_1));
    _torSync->BindOnError(std::bind(&TorSyncManager::Handle::OnError, this, std::placeholders::_1));
    _torSync->BindOnStop(std::bind(&TorSyncManager::Handle::OnStop, this));
}

TorSyncManager::Handle::~Handle()
{
    _torSync->UnbindOnState();
    _torSync->UnbindOnError();
    _torSync->UnbindOnStop();
}

std::shared_ptr<TorSync> TorSyncManager::Handle::GetTorSync()
{
    std::lock_guard lock(_mutex);
    return _torSync;
}

std::string TorSyncManager::Handle::GetHash()
{
    std::lock_guard lock(_mutex);
    return _hash;
}

std::optional<lt::error_code> TorSyncManager::Handle::GetErrorCode()
{
    std::lock_guard lock(_mutex);
    return _ec;
}

lt::torrent_status::state_t TorSyncManager::Handle::GetState()
{
    std::lock_guard lock(_mutex);
    return _state;
}

void TorSyncManager::Handle::OnState(lt::torrent_status::state_t state)
{
    std::lock_guard lock(_mutex);
    _state = state;
}

void TorSyncManager::Handle::OnError(lt::error_code ec)
{
    std::lock_guard lock(_mutex);
    _ec = ec;
}

void TorSyncManager::Handle::OnStop()
{
    std::lock_guard lock(_mutex);
    _ec = std::make_error_code(std::errc::operation_canceled);
}

TorSyncManager::ServeHandle::ServeHandle(std::shared_ptr<TorSync> torSync, const std::string& hash)
: Handle(torSync, hash)
{
}

TorSyncManager::FetchHandle::FetchHandle(std::shared_ptr<TorSync> torSync, const std::string& hash)
: Handle(torSync, hash)
{
    _event.Attach(CreateEventW(NULL, TRUE, FALSE, NULL));
}

void TorSyncManager::FetchHandle::OnState(lt::torrent_status::state_t state)
{
    TorSyncManager::Handle::OnState(state);

    if (state == lt::torrent_status::finished || state == lt::torrent_status::seeding)
    {
        SetEvent(_event);
    }
}

void TorSyncManager::FetchHandle::OnError(lt::error_code ec)
{
    TorSyncManager::Handle::OnError(ec);
    SetEvent(_event);
}

void TorSyncManager::FetchHandle::OnStop()
{
    TorSyncManager::Handle::OnStop();
    SetEvent(_event);
}

void TorSyncManager::FetchHandle::WaitUntilFinished()
{
    WaitForSingleObject(_event, INFINITE);
}

void TorSyncManager::FetchHandle::WaitWithTimeout(DWORD metadataTimeoutMs)
{
    if (metadataTimeoutMs == INFINITE) WaitUntilFinished();
    else
    {
        DWORD res = WaitForSingleObject(_event, metadataTimeoutMs);

        if (res == WAIT_TIMEOUT)
        {
            lt::torrent_status::state_t state = GetState();
            if (state == lt::torrent_status::downloading_metadata)
            {
                OnError(std::make_error_code(std::errc::timed_out));
            }
            else
            {
                WaitUntilFinished();
            }
        }
    }
}

TorSyncManager::TorSyncManager()
{
    TorSyncSessionPool::InitData idata;
    _sessionPool = std::make_shared<TorSyncSessionPool>(idata);
}

TorSyncManager::~TorSyncManager()
{
    Stop();
}

void TorSyncManager::Stop()
{
    std::lock_guard guard(_mutex);

    for (auto ts : _torSyncs)
    {
        ts.second->Stop();
    }
}

std::shared_ptr<TorSyncManager::ServeHandle> TorSyncManager::Serve(const std::wstring& path)
{
    std::string hash;
    std::shared_ptr<TorSync> torSync = std::make_shared<TorSync>(_sessionPool->GetSession());
    
    if (!torSync->Serve(path, &hash)) return nullptr;
    if (!AddIfNotExists(hash, torSync)) return nullptr;

    return std::make_shared<TorSyncManager::ServeHandle>(torSync, hash);
}

std::shared_ptr<TorSyncManager::ServeHandle> TorSyncManager::Serve(const std::wstring& path, const std::wstring& torrentPath)
{
    std::string hash;
    std::shared_ptr<TorSync> torSync = std::make_shared<TorSync>(_sessionPool->GetSession());

    if (!torSync->Serve(path, torrentPath, &hash)) return nullptr;
    if (!AddIfNotExists(hash, torSync)) return nullptr;

    return std::make_shared<TorSyncManager::ServeHandle>(torSync, hash);
}

std::shared_ptr<TorSyncManager::ServeHandle> TorSyncManager::ServeTorrent(const std::wstring& torrentPath, const std::wstring& dataPath)
{
    std::string hash;
    std::shared_ptr<TorSync> torSync = std::make_shared<TorSync>(_sessionPool->GetSession());

    if (!torSync->ServeTorrent(torrentPath, dataPath, &hash)) return nullptr;
    if (!AddIfNotExists(hash, torSync)) return nullptr;

    return std::make_shared<TorSyncManager::ServeHandle>(torSync, hash);
}

std::shared_ptr<TorSyncManager::FetchHandle> TorSyncManager::Fetch(const std::string& hash, const std::wstring& dest)
{
    std::shared_ptr<TorSync> torSync = std::make_shared<TorSync>(_sessionPool->GetSession());

    if (!torSync->Fetch(hash, dest)) return nullptr;
    if (!AddIfNotExists(hash, torSync)) return nullptr;

    return std::make_shared<TorSyncManager::FetchHandle>(torSync, hash);
}

std::map<std::string, std::shared_ptr<TorSync>> TorSyncManager::GetTorSyncs()
{
    std::lock_guard guard(_mutex);
    return _torSyncs;
}

std::shared_ptr<TorSync> TorSyncManager::GetTorSync(const std::string& hash)
{
    std::lock_guard guard(_mutex);
    auto it = _torSyncs.find(hash);

    if (it == _torSyncs.end()) return nullptr;

    return it->second;
}

void TorSyncManager::RemoveTorSync(const std::string& hash)
{
    std::lock_guard guard(_mutex);

    auto it = _torSyncs.find(hash);
    if (it != _torSyncs.end())
    {
        _sessionPool->Unregister(it->second);
        _torSyncs.erase(it);
    }
}

bool TorSyncManager::AddIfNotExists(const std::string& hash, std::shared_ptr<TorSync> torSync)
{
    std::lock_guard guard(_mutex);

    auto it = _torSyncs.find(hash);
    if (it != _torSyncs.end()) return false;

    _torSyncs[hash] = torSync;
    _sessionPool->Register(torSync);

    return true;
}

} // namespace torrents
