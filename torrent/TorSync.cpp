#include "TorSync.h"
#include <filesystem>
#include <fstream>
#include <optional>
#include <atomic>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include "libtorrent/load_torrent.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hex.hpp>
#include <plog/Log.h>
#include "Constants.h"
#include "utils/Converters.h"
#include "utils/Helpers.h"

namespace torrent {

const size_t HASH_LENGTH = 40;

static std::atomic_int gId = 0;

std::optional<lt::sha1_hash> ParseHex(const std::string& hex) 
{
    if (hex.length() != HASH_LENGTH) return std::nullopt;

    lt::sha1_hash hash;
    if (!lt::aux::from_hex({ hex.data(), static_cast<long int>(hex.length()) }, hash.data())) return std::nullopt;

    return hash;
}

std::string to_string(lt::torrent_status::state_t s) 
{
    switch (s) 
    {
        case lt::torrent_status::checking_files: return "Checking";
        case lt::torrent_status::downloading_metadata: return "Downloading metadata";
        case lt::torrent_status::downloading: return "Downloading";
        case lt::torrent_status::finished: return "Finished";
        case lt::torrent_status::seeding: return "Seeding";
    }
    
    return "Unknown";
}

uint64_t TorSync::Metadata::GetTotalSize()
{
    uint64_t totalSize = 0;

    for (auto& f : files)
    {
        totalSize += f.size;
    }

    return totalSize;
}

TorSync::TorSync(std::shared_ptr<lt::session> session)
{
    _id = ++gId;
    _session = session;
}

TorSync::~TorSync()
{
    Stop();
}

bool TorSync::Serve(const std::wstring& path, std::string* hash)
{
    return Serve(path, L"", hash);
}

bool TorSync::Serve(const std::wstring& path, const std::wstring& torrentPath, std::string* hash)
{
    Stop();

    if (!hash) return false;

    lt::error_code ec;
    lt::add_torrent_params atp;

    std::wstring stripped = path;
    utils::helpers::StripBackslash<std::wstring>(stripped);

    std::filesystem::path sourcePath = std::filesystem::absolute(stripped);
    std::filesystem::path parentPath = sourcePath.parent_path();

    lt::file_storage fsStorage;
    lt::add_files(fsStorage, utils::converters::from_u8string(sourcePath.generic_u8string()), lt::create_flags_t { lt::create_torrent::v1_only });

    if (fsStorage.num_files() == 0) 
    {
        PLOGE << "No files found to serve at: " << stripped;
        return false;
    }

    lt::create_torrent ct(fsStorage, 0, lt::create_torrent::v1_only);
    ct.set_creator(constants::TORRENT_USER_AGENT.c_str());
    lt::set_piece_hashes(ct, utils::converters::from_u8string(parentPath.generic_u8string()), ec);

    if (ec) 
    {
        PLOGE << "Error creating torrent (piece hashes): " << ec.message();
        return false;
    }

    // create the torrent

    std::vector<char> torrent;
    lt::bencode(back_inserter(torrent), ct.generate());

    try 
    {
        atp.ti = std::make_shared<lt::torrent_info>(torrent.data(), (int)torrent.size());
    }
    catch (const lt::system_error& e) 
    {
        PLOGE << "Error creating torrent_info from entry: " << e.what();
        return false;
    }

    atp.save_path = utils::converters::from_u8string(parentPath.generic_u8string());
    atp.flags |= lt::torrent_flags::seed_mode;
    atp.trackers.insert(atp.trackers.end(),
        constants::DEFAULT_TRACKERS.begin(),
        constants::DEFAULT_TRACKERS.end());

    _handle = _session->add_torrent(atp, ec);

    if (ec) 
    {
        PLOGE << "Error adding torrent to session: " << ec.message();
        return false;
    }

    SetHash(lt::aux::to_hex(_handle.info_hashes().v1));
    *hash = GetHash();

    if (!torrentPath.empty())
    {
        std::filesystem::path p(torrentPath);
        p /= *hash + constants::TORRENT_EXT;

        SetTorrentPath(p.generic_wstring());

        std::fstream out;
        out.exceptions(std::ifstream::failbit);
        out.open(GetTorrentPath().c_str(), std::ios_base::out | std::ios_base::binary);
        out.write(torrent.data(), (int)torrent.size());
    }

    PLOGI << "Serving path = " << stripped << " torrent path = " << GetTorrentPath() << " hash = " << *hash << "...";

    return true;
}

bool TorSync::ServeTorrent(const std::wstring& torrentPath, const std::wstring& dataPath, std::string* hash)
{
    if (!hash) return false;

    std::wstring stripped = dataPath;
    utils::helpers::StripBackslash<std::wstring>(stripped);

    std::filesystem::path tp(torrentPath);
    std::filesystem::path dp(stripped);

    std::error_code fsec;
    if (!std::filesystem::exists(tp, fsec)) return false;

    Stop();

    lt::error_code ec;
    lt::load_torrent_limits cfg;
    lt::add_torrent_params atp = lt::load_torrent_file(utils::converters::from_u8string(tp.generic_u8string()), cfg);

    atp.save_path = utils::converters::from_u8string(dp.parent_path().generic_u8string());
    atp.flags |= lt::torrent_flags::seed_mode;
    atp.trackers.insert(atp.trackers.end(),
        constants::DEFAULT_TRACKERS.begin(),
        constants::DEFAULT_TRACKERS.end());

    _handle = _session->add_torrent(atp, ec);

    if (ec)
    {
        PLOGE << "Error adding torrent to session: " << ec.message();
        return false;
    }

    SetTorrentPath(tp.generic_wstring());
    SetHash(lt::aux::to_hex(_handle.info_hashes().v1));
    *hash = GetHash();

    PLOGI << "Serving path = " << stripped << " torrent path = " << torrentPath << " hash = " << *hash << "...";

    return true;
}

bool TorSync::SaveTorrentFile(const std::wstring& torrentPath)
{
    auto ti = _handle.torrent_file();
    if (!ti) return false;

    lt::create_torrent ct(*ti);
    std::vector<char> torrent;
    lt::bencode(back_inserter(torrent), ct.generate());

    if (!torrentPath.empty())
    {
        std::filesystem::path p(torrentPath);

        SetTorrentPath(p.generic_wstring());

        std::fstream out;
        out.exceptions(std::ifstream::failbit);
        out.open(GetTorrentPath().c_str(), std::ios_base::out | std::ios_base::binary);
        out.write(torrent.data(), (int)torrent.size());
    }

    return true;
}

bool TorSync::Fetch(const std::string& hash, const std::wstring& dest)
{
    Stop();

    auto h = ParseHex(hash);
    if (!h.has_value())
    {
        PLOGE << "Invalid info-hash format (must be 40 hex chars)";
        return false;
    }

    lt::add_torrent_params atp;
    atp.info_hashes.v1 = h.value();
    atp.trackers.insert(atp.trackers.end(),
        constants::DEFAULT_TRACKERS.begin(),
        constants::DEFAULT_TRACKERS.end());

    std::filesystem::path fetchPath = std::filesystem::absolute(dest);
    std::error_code ec;

    if (std::filesystem::exists(fetchPath, ec))
    {
        if (!std::filesystem::is_directory(fetchPath, ec))
        {
            PLOGE << "Fetch destination exists but is not a directory: " << dest;
            return false;
        }
    }
    else 
    {
        if (!std::filesystem::create_directories(fetchPath, ec) || ec) 
        {
            PLOGE << "Could not create fetch destination directory: " << dest << (ec ? " (" + ec.message() + ")" : "");
            return false;
        }

        PLOGI << "Created destination directory: " << utils::converters::from_u8string(fetchPath.generic_u8string());
    }

    if (ec) 
    {
        PLOGE << "Error accessing fetch destination path " << dest << ": " << ec.message();
        return false;
    }

    atp.save_path = utils::converters::from_u8string(fetchPath.generic_u8string());

    lt::error_code eclt;

    _handle = _session->add_torrent(atp, eclt);

    if (eclt)
    {
        PLOGE << "Error adding torrent by hash: " << eclt.message();
        return false;
    }

    SetHash(hash);
    _handle.force_dht_announce();
    _handle.force_reannounce();
    PLOGI << "Fetching hash = " << hash << " dest = " << dest << "...";

    return true;
}

void TorSync::Stop()
{
    if (_handle.is_valid())
    {
        auto s = _session;
        if (s) s->remove_torrent(_handle);
    }

    RaiseOnStop();
}

std::optional<TorSync::Metadata> GetMetadataInternal(std::shared_ptr<const lt::torrent_info> ti)
{
    if (!ti) return std::nullopt;

    auto files = ti->files();
    int numFiles = ti->num_files();

    TorSync::Metadata md;

    for (int i = 0; i < numFiles; ++i)
    {
        TorSync::Metadata::File f;
        lt::file_index_t index = (lt::file_index_t)i;

        f.path = utils::converters::ToWideChar(files.file_path(index));
        f.name = utils::converters::ToWideChar(files.file_name(index).to_string());
        f.size = files.file_size(index);

        md.files.push_back(f);
    }

    if (md.files.size() > 0)
    {
        std::filesystem::path p = md.files[0].path;
        md.parentPath = p.parent_path();
    }

    return md;
}

std::optional<TorSync::Metadata> GetMetadata(const std::wstring& torrentPath)
{
    std::filesystem::path tp(torrentPath);
    std::error_code fsec;
    if (!std::filesystem::exists(tp, fsec)) return std::nullopt;

    lt::error_code ec;
    lt::load_torrent_limits cfg;
    lt::add_torrent_params atp = lt::load_torrent_file(utils::converters::from_u8string(tp.generic_u8string()), cfg);

    return GetMetadataInternal(atp.ti);
}

std::optional<TorSync::Metadata> TorSync::GetMetadata()
{
    return GetMetadataInternal(_handle.torrent_file());
}

std::wstring TorSync::GetTorrentPath()
{
    std::lock_guard lock(_mutex);
    return _torrentPath;
}

void TorSync::BindOnState(const OnState& val)
{
    std::lock_guard lock(_mutex);
    _onState = val;
}

void TorSync::UnbindOnState()
{
    std::lock_guard lock(_mutex);
    _onState = {};
}

void TorSync::SetHash(const std::string& hash)
{
    std::lock_guard lock(_mutex);
    _hash = hash;
}

void TorSync::SetState(lt::torrent_status::state_t s)
{
    std::lock_guard lock(_mutex);
    _state = s;
}

void TorSync::SetTorrentPath(const std::wstring& torrentPath)
{
    std::lock_guard lock(_mutex);
    _torrentPath = torrentPath;
}

std::string TorSync::GetHash()
{
    std::lock_guard lock(_mutex);
    return _hash;
}

lt::torrent_status::state_t TorSync::GetState()
{
    std::lock_guard lock(_mutex);
    return _state;
}

float TorSync::GetProgress()
{
    float status = 0;

    try
    {
        if (_handle.is_valid())
        {
            status = _handle.status().progress * 100;
        }
    }
    catch (const std::exception& ex)
    {
        PLOGE << "Id = " << GetId() << " hash = " << _hash << " ex = " << ex.what();
    }

    return status;
}

void TorSync::RaiseOnState(lt::torrent_status::state_t s)
{
    OnState cb;
    {
        std::lock_guard lock(_mutex);
        cb = _onState;
    }

    if (cb) cb(s);
}

void TorSync::BindOnError(const OnError& val)
{
    std::lock_guard lock(_mutex);
    _onError = val;
}

void TorSync::UnbindOnError()
{
    std::lock_guard lock(_mutex);
    _onError = {};
}

void TorSync::RaiseOnError(lt::error_code ec)
{
    OnError cb;
    {
        std::lock_guard lock(_mutex);
        cb = _onError;
    }

    if (cb) cb(ec);
}

void TorSync::BindOnStop(const OnStop& val)
{
    std::lock_guard lock(_mutex);
    _onStop = val;
}

void TorSync::UnbindOnStop()
{
    std::lock_guard lock(_mutex);
    _onStop = {};
}

void TorSync::RaiseOnStop()
{
    OnStop cb;
    {
        std::lock_guard lock(_mutex);
        cb = _onStop;
    }

    if (cb) cb();
}

} // namespace torrent
