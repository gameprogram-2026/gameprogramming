#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <enet/enet.h>
#include "shared/network/Packet.h"
#include "shared/network/Protocol.h"
#include "shared/ecs/World.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// PeerInfo — one connected client
// ─────────────────────────────────────────────────────────────────────────────
struct PeerInfo {
    ENetPeer* peer        = nullptr;
    uint32_t  playerNetID = 0;   ///< Entity this peer controls
    uint8_t   teamID      = 0;
    uint32_t  lastInputSeq= 0;
    InputPacket lastInput{};
    bool      connected   = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// NetworkSystem — manages all ENet I/O on the server
//
// Responsibilities:
//   • Accept/drop connections (max MAX_CLIENTS)
//   • Dispatch incoming packets to registered handlers
//   • Build and broadcast EntityStateRecord snapshots at 20 Hz
//   • Send reliable events to specific peers or all peers
// ─────────────────────────────────────────────────────────────────────────────
class NetworkSystem {
public:
    using InputHandler   = std::function<void(uint32_t peerIdx, const InputPacket&)>;
    using ConnectHandler = std::function<void(uint32_t peerIdx)>;
    using DisconHandler  = std::function<void(uint32_t peerIdx)>;
    using UseItemCB      = std::function<void(uint32_t peerIdx, const char* key)>;
    using AllianceCB     = std::function<void(uint32_t peerIdx, uint8_t toTeam)>;
    using BuildCB        = std::function<void(uint32_t peerIdx, int16_t tileX, int16_t tileY, uint8_t buildingType, uint8_t direction)>;
    using CraftCB        = std::function<void(uint32_t peerIdx, uint8_t recipeID)>;
    using LootPickupCB   = std::function<void(uint32_t peerIdx, uint32_t lootNetID)>;
    using ItemDropCB     = std::function<void(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx, uint16_t quantity)>;
    using DismantleCB    = std::function<void(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx)>;
    using AuthHandler    = std::function<void(uint32_t peerIdx, const char* user, const char* pass, bool isRegister)>;

    using JoinHandler    = std::function<void(uint32_t peerIdx)>;
    using StashTransferCB = std::function<void(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx, uint8_t dstType, uint8_t dstIdx)>;
    using SelectWeaponCB = std::function<void(uint32_t peerIdx, uint8_t slot)>;
    using DoorToggleCB = std::function<void(uint32_t peerIdx, uint16_t doorID)>;

    bool init(uint16_t port);
    void shutdown();

    // ── Poll ENet events, call registered handlers ─────────────────────────────
    void pollEvents();

    // ── Snapshot broadcast (call once per server tick) ─────────────────────────
    void broadcastSnapshot(World& world, uint16_t tick);

    // ── Reliable & Unreliable event helpers ───────────────────────────────────
    void sendReliable(uint32_t peerIdx, const void* data, size_t len);
    void sendUnreliable(uint32_t peerIdx, const void* data, size_t len);
    void broadcastReliable(const void* data, size_t len);
    void broadcastReliableExcept(uint32_t exceptIdx, const void* data, size_t len);

    // ── HP & Stamina sync ─────────────────────────────────────────────────────
    void sendHpSync(uint32_t peerIdx, uint16_t netID, float hp, float maxHp, float stamina, float maxStamina, uint8_t flags);
    void broadcastTeamStatus(World& world, uint8_t allianceBits, uint16_t gameTimeSec);

    // ── Utils ─────────────────────────────────────────────────────────────────
    void disconnectPeer(uint32_t peerIdx) {
        if (peerIdx < MAX_CLIENTS && m_peers[peerIdx].connected && m_peers[peerIdx].peer) {
            enet_peer_disconnect(m_peers[peerIdx].peer, 0);
            m_peers[peerIdx].connected = false;
        }
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onInput(InputHandler h)      { m_onInput   = std::move(h); }
    void onConnect(ConnectHandler h)  { m_onConnect = std::move(h); }
    void onDisconnect(DisconHandler h){ m_onDiscon  = std::move(h); }
    void onAuth(AuthHandler h)        { m_onAuth    = std::move(h); }
    void onJoinMatch(JoinHandler h)   { m_onJoin    = std::move(h); }
    void onStashTransfer(StashTransferCB h) { m_onStashTransfer = std::move(h); }
    void onSelectWeapon(SelectWeaponCB h)   { m_onSelectWeapon = std::move(h); }
    void onDoorToggle(DoorToggleCB h)       { m_onDoorToggle = std::move(h); }
    void onUseItem(UseItemCB h)       { m_onUseItem = std::move(h); }
    void onAlliancePropose(AllianceCB h){ m_onAlliance = std::move(h); }
    void onBuildPlace(BuildCB cb)        { m_onBuild = std::move(cb); }
    void onCraft(CraftCB cb)             { m_onCraft = std::move(cb); }
    void onLootPickup(LootPickupCB cb)   { m_onLootPickup = std::move(cb); }
    void onItemDrop(ItemDropCB cb)        { m_onItemDrop = std::move(cb); }
    void onDismantle(DismantleCB cb)      { m_onDismantle = std::move(cb); }

    const PeerInfo& peer(uint32_t idx) const { return m_peers[idx]; }
    int  connectedCount() const;
    bool isConnected(uint32_t idx) const { return m_peers[idx].connected; }
    void setPeerNetID(uint32_t peerIdx, uint32_t netID) {
        if (peerIdx < MAX_CLIENTS) m_peers[peerIdx].playerNetID = netID;
    }

private:
    ENetHost*                          m_host = nullptr;
    std::array<PeerInfo, MAX_CLIENTS>  m_peers{};

    InputHandler    m_onInput;
    ConnectHandler  m_onConnect;
    DisconHandler   m_onDiscon;
    AuthHandler     m_onAuth;
    JoinHandler     m_onJoin;
    StashTransferCB m_onStashTransfer;
    SelectWeaponCB  m_onSelectWeapon;
    DoorToggleCB    m_onDoorToggle;
    UseItemCB       m_onUseItem;
    AllianceCB      m_onAlliance;
    BuildCB         m_onBuild;
    CraftCB         m_onCraft;
    LootPickupCB    m_onLootPickup;
    ItemDropCB      m_onItemDrop;
    DismantleCB     m_onDismantle;

    uint32_t assignPeerSlot(ENetPeer* peer);
    void     handlePacket(uint32_t peerIdx, const uint8_t* data, size_t len);
};

} // namespace dz
