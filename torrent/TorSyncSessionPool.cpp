#include <random>
#include <plog/Log.h>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>
#include "TorSyncSessionPool.h"
#include "Constants.h"

namespace torrent {

int GenerateRandomPort()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distrib(30000, 60000);

    return distrib(gen);
}

TorSyncSessionPool::TorSyncSessionPool(const InitData& idata)
{
    _idata = idata;
    _stopEvent.Attach(CreateEventW(NULL, TRUE, FALSE, NULL));

    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::storage | lt::alert_category::status | lt::alert_category::performance_warning);

    // TODO: Вынести в настройки?
    // Для раздающей Олега
    //settings.set_bool(lt::settings_pack::enable_upnp, false);
    //settings.set_bool(lt::settings_pack::enable_natpmp, false);

    // Для АТС
    settings.set_bool(lt::settings_pack::enable_upnp, _idata.global);
    settings.set_bool(lt::settings_pack::enable_natpmp, _idata.global);

    settings.set_bool(lt::settings_pack::enable_lsd, true);
    settings.set_bool(lt::settings_pack::enable_dht, _idata.global);
    settings.set_str(lt::settings_pack::user_agent, constants::TORRENT_USER_AGENT);
    settings.set_int(lt::settings_pack::active_downloads, -1);
    settings.set_int(lt::settings_pack::active_seeds, -1);
    settings.set_int(lt::settings_pack::dht_announce_interval, 10);
    settings.set_str(lt::settings_pack::dht_bootstrap_nodes, "router.bittorrent.com:6881,dht.transmissionbt.com:6881,router.bt.ouinet.work:6881,router.utorrent.com,router.bittorrent.com:6881");

    settings.set_int(lt::settings_pack::local_service_announce_interval, 10);
    settings.set_bool(lt::settings_pack::close_redundant_connections, false);
    settings.set_bool(lt::settings_pack::announce_to_all_tiers, true); // Анонсировать на всех трекерах
    settings.set_bool(lt::settings_pack::announce_to_all_trackers, true); // Анонсировать на всех уровнях трекеров
    //settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);

    int port = GenerateRandomPort();
    std::string listenInterface = std::format("0.0.0.0:{}, [::]:{}", port, port);
    settings.set_str(lt::settings_pack::listen_interfaces, listenInterface);
    PLOGI << "Listen interface = " << listenInterface;

    settings.set_int(lt::settings_pack::unchoke_slots_limit, _idata.slotLimit);
    settings.set_int(lt::settings_pack::seed_choking_algorithm, lt::settings_pack::round_robin);
    settings.set_bool(lt::settings_pack::enable_outgoing_utp, true); // Disable uTP
    settings.set_bool(lt::settings_pack::enable_incoming_utp, true); // Disable uTP
    settings.set_int(lt::settings_pack::request_timeout, 10); // seconds
    settings.set_int(lt::settings_pack::piece_timeout, 5); // seconds
    settings.set_int(lt::settings_pack::peer_timeout, 30); // seconds
    settings.set_int(lt::settings_pack::request_queue_time, 5); // seconds
    settings.set_int(lt::settings_pack::max_out_request_queue, 1500);
    settings.set_int(lt::settings_pack::max_allowed_in_request_queue, 4000);
    settings.set_int(lt::settings_pack::num_optimistic_unchoke_slots, 0); 

    settings.set_int(lt::settings_pack::max_queued_disk_bytes, 16 * 1024 * 1024); // 16 MiB
    settings.set_int(lt::settings_pack::send_buffer_watermark, 2 * 1024 * 1024); // 2 MiB
    settings.set_int(lt::settings_pack::send_buffer_watermark_factor, 150); // 150%
    settings.set_int(lt::settings_pack::recv_socket_buffer_size, 2 * 1024 * 1024); // 2 MiB OS buffer
    settings.set_int(lt::settings_pack::send_socket_buffer_size, 2 * 1024 * 1024); // 2 MiB OS buffer
    settings.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled); // Disable encryption
    settings.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled); // Disable encryption

    lt::session_params params(settings);

    _session = std::make_shared<lt::session>(params);
    _worker = std::thread{ &TorSyncSessionPool::Worker, this };
}

TorSyncSessionPool::~TorSyncSessionPool()
{
    SetEvent(_stopEvent);

    if (_worker.joinable()) _worker.join();
    ResetEvent(_stopEvent);

    _session->abort();
}

void TorSyncSessionPool::Register(std::shared_ptr<torrent::TorSync> torSync)
{
    std::lock_guard lock(_mutex);
    _torSyncs[torSync->GetId()] = torSync;
 }

void TorSyncSessionPool::Unregister(std::shared_ptr<torrent::TorSync> torSync)
{
    std::lock_guard lock(_mutex);
    _torSyncs.erase(torSync->GetId());
}

std::shared_ptr<torrent::TorSync> TorSyncSessionPool::GetTorSync(const lt::torrent_handle& handle)
{
    std::shared_ptr<TorSync> torSync;
    std::lock_guard lock(_mutex);

    auto it = _torSyncs.begin();
    for (; it != _torSyncs.end(); ++it)
    {
        if (it->second->GetHandle() == handle)
        {
            torSync = it->second;
            break;
        }
    }

    return torSync;
}

void TorSyncSessionPool::Worker()
{
    while (true)
    {
        DWORD waitResult = WaitForSingleObject(_stopEvent, 50);
        if (waitResult == WAIT_OBJECT_0) break;

        std::vector<lt::alert*> alerts;
        _session->pop_alerts(&alerts);

        for (lt::alert* a : alerts)
        {
            if (auto* e = lt::alert_cast<lt::torrent_error_alert>(a))
            {
                std::shared_ptr<TorSync> torSync = GetTorSync(e->handle);

                if (torSync)
                {
                    PLOGE << torSync->GetHash() << ": " << e->error.message();
                    torSync->RaiseOnError(e->error);
                    break;
                }
            }
            else if (auto* su = lt::alert_cast<lt::state_update_alert>(a))
            {
                for (const auto& status : su->status)
                {
                    std::shared_ptr<TorSync> torSync = GetTorSync(status.handle);

                    if (torSync)
                    {
                        if (status.errc)
                        {
                            PLOGE << torSync->GetHash() << ": " << status.errc.message();
                            torSync->RaiseOnError(status.errc);
                            break;
                        }
                        else
                        {
                            PLOGI << "Num peers " << torSync->_handle.status().num_peers;

                            if (torSync->GetState() == status.state) continue;

                            PLOGI << torSync->GetHash() << ": " << to_string(status.state);
                            torSync->SetState(status.state);
                            torSync->RaiseOnState(status.state);

                        }
                    }
                }
            }
            else if (auto* tf = lt::alert_cast<lt::torrent_finished_alert>(a))
            {
                std::shared_ptr<TorSync> torSync = GetTorSync(tf->handle);

                if (torSync)
                {
                    _session->post_torrent_updates();
                }
            }
        }

        _session->post_torrent_updates();
    }
}

} // namespace torrent
