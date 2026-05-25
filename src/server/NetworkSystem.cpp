#include "NetworkSystem.h"
#include "shared/util/Logger.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/BuildingComponent.h"
#include "shared/network/Packet.h"
#include "server/ZombieAI.h"
#include <cstring>
#include <algorithm>

namespace dz {

bool NetworkSystem::init(uint16_t port) {
    if (enet_initialize() != 0) {
        DZ_LOG_FATAL("enet_initialize() failed");
        return false;
    }
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    m_host = enet_host_create(&addr, MAX_CLIENTS, CHAN_COUNT, 0, 0);
    if (!m_host) {
        DZ_LOG_FATAL("enet_host_create failed on port %u", port);
        enet_deinitialize();
        return false;
    }
    DZ_LOG_INFO("[Net] Server listening on port %u (max %d clients)", port, MAX_CLIENTS);
    return true;
}

void NetworkSystem::shutdown() {
    if (m_host) { enet_host_destroy(m_host); m_host = nullptr; }
    enet_deinitialize();
}

void NetworkSystem::pollEvents() {
    if (!m_host) return;
    ENetEvent ev;
    while (enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            uint32_t slot = assignPeerSlot(ev.peer);
            ev.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(slot));
            DZ_LOG_INFO("[Net] Client connected → slot %u", slot);
            if (m_onConnect) m_onConnect(slot);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            auto slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ev.peer->data));
            handlePacket(slot, ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            auto slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ev.peer->data));
            DZ_LOG_INFO("[Net] Client disconnected ← slot %u", slot);
            m_peers[slot] = {};
            if (m_onDiscon) m_onDiscon(slot);
            break;
        }
        default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// broadcastSnapshot — serialize dirty entities into EntityStateRecord packets
// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::broadcastSnapshot(World& world, uint16_t tick) {
    if (!m_host) return;

    // Serialise all alive entities into EntityStateRecord array
    static uint8_t buf[sizeof(SnapshotHeader) +
                        MAX_SNAPSHOT_ENTITIES * sizeof(EntityStateRecord)];

    auto* header = reinterpret_cast<SnapshotHeader*>(buf);
    header->packetType  = static_cast<uint8_t>(PacketType::S2C_WorldSnapshot);
    header->serverTick  = tick;
    header->entityCount = 0;

    auto* records = reinterpret_cast<EntityStateRecord*>(buf + sizeof(SnapshotHeader));
    int count = 0;

    for (EntityID id : world.alive()) {
        if (count >= static_cast<int>(MAX_SNAPSHOT_ENTITIES)) break;
        Entity e{id};

        auto* net = world.tryGet<NetworkComponent>(e);
        if (!net) continue;

        auto* xf  = world.tryGet<TransformComponent>(e);
        auto* hp  = world.tryGet<HealthComponent>(e);
        auto* cbt = world.tryGet<CombatComponent>(e);
        auto* bld = world.tryGet<BuildingComponent>(e);

        EntityStateRecord& rec = records[count++];
        rec.version     = PROTOCOL_VERSION;
        if (bld) {
            rec.recordType = REC_BUILDING;
        } else if (!hp) {
            rec.recordType = REC_LOOT;
        } else {
            rec.recordType = (hp->team == Team::Neutral) ? REC_ZOMBIE : REC_PLAYER;
        }
        rec.entityID    = static_cast<uint16_t>(net->netID);
        rec.statusFlags = 0;
        rec.seqAck      = 0;  // default: 플레이어는 per-peer 루프에서 덮어씀
        rec.x = xf ? xf->x : 0.0f;
        rec.y = xf ? xf->y : 0.0f;

        // Build status flags
        if (hp && hp->isAlive)       rec.statusFlags |= STATUS_ALIVE;
        if (hp && !hp->isAlive)      rec.statusFlags |= STATUS_DEAD;
        if (cbt && cbt->isBleeding)  rec.statusFlags |= STATUS_BLEEDING;
        if (cbt && cbt->isOnFire)    rec.statusFlags |= STATUS_ON_FIRE;
        if (cbt && cbt->isReloading) rec.statusFlags |= STATUS_RELOADING;

        // seqAck = HP (모든 엔티티). 로컬 플레이어는 per-peer 루프에서 inputSeq로 덮어씀
        if (hp) rec.seqAck = static_cast<uint16_t>(std::max(0.0f, hp->currentHp));

        // 좀비: statusFlags 상위 2비트에 ZombieType 인코딩
        if (rec.recordType == REC_ZOMBIE) {
            auto* ai = world.tryGet<ZombieAIComponent>(e);
            if (ai) {
                rec.statusFlags |= (static_cast<uint8_t>(ai->type) & 0x03u) << 6;
            }
        } else if (rec.recordType == REC_BUILDING) {
            if (bld) {
                rec.statusFlags = static_cast<uint8_t>(bld->type);
            }
        }

        rec.computeChecksum();
    }

    header->entityCount = static_cast<uint8_t>(count);
    size_t totalLen = sizeof(SnapshotHeader) + count * sizeof(EntityStateRecord);

    // Send per-peer to stamp correct seqAck for the player each peer owns
    for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
        if (!m_peers[pi].connected) continue;

        // Stamp seqAck in the player's own record
        for (int ri = 0; ri < count; ++ri) {
            if (records[ri].entityID == static_cast<uint16_t>(m_peers[pi].playerNetID)) {
                records[ri].seqAck = static_cast<uint16_t>(m_peers[pi].lastInputSeq);
                records[ri].computeChecksum();
                break;
            }
        }

        ENetPacket* pkt = enet_packet_create(buf, totalLen, 0); // unreliable
        enet_peer_send(m_peers[pi].peer, CHAN_UNRELIABLE, pkt);
    }

    enet_host_flush(m_host);
}

void NetworkSystem::sendReliable(uint32_t idx, const void* data, size_t len) {
    if (!m_peers[idx].connected) return;
    ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peers[idx].peer, CHAN_RELIABLE, pkt);
}

void NetworkSystem::sendUnreliable(uint32_t idx, const void* data, size_t len) {
    if (!m_peers[idx].connected) return;
    ENetPacket* pkt = enet_packet_create(data, len, 0); // Unreliable
    enet_peer_send(m_peers[idx].peer, CHAN_UNRELIABLE, pkt);
}

void NetworkSystem::broadcastReliable(const void* data, size_t len) {
    ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(m_host, CHAN_RELIABLE, pkt);
}

void NetworkSystem::broadcastReliableExcept(uint32_t exceptIdx,
                                             const void* data, size_t len) {
    for (uint32_t i = 0; i < MAX_CLIENTS; ++i) {
        if (i == exceptIdx || !m_peers[i].connected) continue;
        sendReliable(i, data, len);
    }
}

int NetworkSystem::connectedCount() const {
    int n = 0;
    for (auto& p : m_peers) if (p.connected) ++n;
    return n;
}

uint32_t NetworkSystem::assignPeerSlot(ENetPeer* peer) {
    for (uint32_t i = 0; i < MAX_CLIENTS; ++i) {
        if (!m_peers[i].connected) {
            m_peers[i]           = {};
            m_peers[i].peer      = peer;
            m_peers[i].connected = true;
            return i;
        }
    }
    // No slot — disconnect the peer
    enet_peer_disconnect(peer, 0);
    return 0;
}

void NetworkSystem::handlePacket(uint32_t peerIdx,
                                  const uint8_t* data, size_t len) {
    if (len < 1) return;
    auto type = static_cast<PacketType>(data[0]);

    switch (type) {
    case PacketType::C2S_Input: {
        if (len < sizeof(InputPacket)) return;
        InputPacket pkt{};
        std::memcpy(&pkt, data, sizeof(InputPacket));
        if (pkt.seqNum > m_peers[peerIdx].lastInputSeq) {
            m_peers[peerIdx].lastInputSeq = pkt.seqNum;
            m_peers[peerIdx].lastInput    = pkt;
            if (m_onInput) m_onInput(peerIdx, pkt);
        }
        break;
    }
    case PacketType::C2S_Auth: {
        if (len < sizeof(AuthPacket)) return;
        AuthPacket pkt{};
        std::memcpy(&pkt, data, sizeof(pkt));
        // Ensure null termination
        pkt.username[sizeof(pkt.username)-1] = '\0';
        pkt.password[sizeof(pkt.password)-1] = '\0';
        if (m_onAuth) m_onAuth(peerIdx, pkt.username, pkt.password, pkt.isRegister != 0);
        break;
    }
    case PacketType::C2S_JoinMatch: {
        if (m_onJoin) m_onJoin(peerIdx);
        break;
    }
    case PacketType::C2S_StashTransfer: {
        if (len < sizeof(PktStashTransfer)) return;
        PktStashTransfer pkt{};
        std::memcpy(&pkt, data, sizeof(pkt));
        if (m_onStashTransfer) m_onStashTransfer(peerIdx, pkt.srcType, pkt.srcIdx, pkt.dstType, pkt.dstIdx);
        break;
    }
    case PacketType::C2S_UseItem: {
        if (len < sizeof(UseItemPacket)) return;
        UseItemPacket pkt{};
        std::memcpy(&pkt, data, sizeof(pkt));
        pkt.key[sizeof(pkt.key)-1] = '\0';
        if (m_onUseItem) m_onUseItem(peerIdx, pkt.key);
        break;
    }
    case PacketType::C2S_AlliancePropose: {
        // 2-byte payload: [type, toTeam]
        if (len < 2) return;
        uint8_t toTeam = data[1];
        if (m_onAlliance) m_onAlliance(peerIdx, toTeam);
        break;
    }
    case PacketType::C2S_AllianceBreak: {
        if (len < 2) return;
        uint8_t toTeam = data[1];
        if (m_onAlliance) m_onAlliance(peerIdx, toTeam); // reuse — GameServer distinguishes
        break;
    }
    case PacketType::C2S_BuildPlace: {
        if (len < sizeof(BuildPlacePacket)) return;
        BuildPlacePacket pkt{};
        std::memcpy(&pkt, data, sizeof(pkt));
        if (m_onBuild) m_onBuild(peerIdx, pkt.tileX, pkt.tileY, pkt.buildingType);
        break;
    }
    case PacketType::C2S_CraftRequest: {
        if (len < sizeof(CraftRequestPacket)) return;
        CraftRequestPacket pkt{};
        std::memcpy(&pkt, data, sizeof(pkt));
        if (m_onCraft) m_onCraft(peerIdx, pkt.recipeID);
        break;
    }
    case PacketType::C2S_LootPickup: {
        if (len < sizeof(LootPickupPacket)) return;
        LootPickupPacket pkt{};
        std::memcpy(&pkt, data, sizeof(pkt));
        if (m_onLootPickup) m_onLootPickup(peerIdx, pkt.lootNetID);
        break;
    }
    default:
        DZ_LOG_DEBUG("[Net] Unknown packet 0x%02X from peer %u",
                     static_cast<uint8_t>(type), peerIdx);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendHpSync — HP, Stamina와 상태 플래그를 특정 클라이언트에 신뢰 채널로 전송
// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::sendHpSync(uint32_t peerIdx, uint16_t netID,
                                float hp, float maxHp, float stamina, float maxStamina, uint8_t flags) {
    HpSyncPacket pkt{};
    pkt.packetType  = static_cast<uint8_t>(PacketType::S2C_HpSync);
    pkt.targetNetID = netID;
    pkt.currentHp   = hp;
    pkt.maxHp       = maxHp;
    pkt.currentStamina = stamina;
    pkt.maxStamina  = maxStamina;
    pkt.flags       = flags;
    sendReliable(peerIdx, &pkt, sizeof(pkt));
}

// ─────────────────────────────────────────────────────────────────────────────
// broadcastTeamStatus
// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::broadcastTeamStatus(World& world, uint8_t allianceBits, uint16_t gameTimeSec) {
    TeamStatusPacket pkt{};
    pkt.packetType   = static_cast<uint8_t>(PacketType::S2C_TeamStatus);
    pkt.allianceBits = allianceBits;
    pkt.gameTimeSec  = gameTimeSec;

    for (EntityID id : world.alive()) {
        Entity e{id};
        auto* hp  = world.tryGet<HealthComponent>(e);
        if (!hp || !hp->isAlive || hp->team == Team::Neutral) continue;
        int tidx = static_cast<int>(hp->team) - 1;
        if (tidx >= 0 && tidx < 4) ++pkt.aliveCount[tidx];
    }
    broadcastReliable(&pkt, sizeof(pkt));
}

} // namespace dz
