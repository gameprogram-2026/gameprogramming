#include "NetworkClient.h"
#include "shared/util/Logger.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <SDL2/SDL.h>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// connect
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkClient::connect(const std::string& host, uint16_t port) {
    if (enet_initialize() != 0) {
        DZ_LOG_ERROR("[Client] enet_initialize() failed");
        return false;
    }
    m_host = enet_host_create(nullptr, 1, CHAN_COUNT, 0, 0);
    if (!m_host) {
        DZ_LOG_ERROR("[Client] enet_host_create() failed");
        return false;
    }

    ENetAddress addr{};
    enet_address_set_host(&addr, host.c_str());
    addr.port = port;

    m_peer = enet_host_connect(m_host, &addr, CHAN_COUNT, 0);
    if (!m_peer) {
        DZ_LOG_ERROR("[Client] enet_host_connect() failed");
        return false;
    }

    // 1단계 + 2단계 통합: ENet 연결 + ConnectAck 대기
    // macOS는 SDL 이벤트를 처리하지 않으면 앱을 "무응답"으로 판단해
    // 키보드 포커스를 빼앗는다. 각 루프에서 SDL_PumpEvents()를 호출해
    // OS에 앱이 살아있음을 알린다.
    ENetEvent ev;
    bool enetConnected = false;
    auto deadline = SDL_GetTicks() + 5000; // 5초 타임아웃

    while (SDL_GetTicks() < deadline) {
        // SDL 이벤트 펌프 — 포커스 유지 핵심
        SDL_PumpEvents();

        int ret = enet_host_service(m_host, &ev, 5); // 5ms 단위 폴링
        if (ret < 0) break; // 오류

        if (ret > 0) {
            if (!enetConnected && ev.type == ENET_EVENT_TYPE_CONNECT) {
                enetConnected = true;
                DZ_LOG_INFO("[Client] ENet connected to %s:%u", host.c_str(), port);
                continue;
            }
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                DZ_LOG_ERROR("[Client] Disconnected during handshake");
                m_peer = nullptr;
                return false;
            }
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                bool isAck = false;
                if (ev.packet->dataLength >= 1) {
                    auto ptype = static_cast<PacketType>(ev.packet->data[0]);
                    if (ptype == PacketType::S2C_ConnectAck &&
                        ev.packet->dataLength >= sizeof(ConnectAckPacket)) {
                        ConnectAckPacket ack{};
                        std::memcpy(&ack, ev.packet->data, sizeof(ack));
                        m_localNetID   = ack.netID;
                        m_localX       = ack.spawnX;
                        m_localY       = ack.spawnY;
                        m_localTeam    = ack.teamID;
                        m_localHp      = 100.0f;
                        m_localMaxHp   = 100.0f;
                        m_localFlags   = STATUS_ALIVE;
                        m_localDead    = false;
                        m_justSpawned  = true;
                        m_remoteCount  = 0;
                        m_zombieDeaths.clear();
                        m_predCount    = 0;
                        m_predHead     = 0;
                        isAck = true;
                    }
                }
                enet_packet_destroy(ev.packet);
                if (isAck) {
                    DZ_LOG_INFO("[Client] ConnectAck: netID=%u spawn=(%.0f,%.0f) team=%u",
                                m_localNetID, m_localX, m_localY, m_localTeam);
                    return true;
                }
            }
        }

        // ENet이 연결됐지만 ConnectAck 아직 미수신 — 잠깐 대기
        if (!enetConnected) SDL_Delay(1);
    }

    DZ_LOG_WARN("[Client] ConnectAck not received within 2s — proceeding anyway");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// startConnect — 비블로킹: ENet 연결 요청만 하고 즉시 반환
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkClient::startConnect(const std::string& host, uint16_t port) {
    if (m_host) { enet_host_destroy(m_host); m_host = nullptr; }
    enet_initialize();

    m_host = enet_host_create(nullptr, 1, CHAN_COUNT, 0, 0);
    if (!m_host) { DZ_LOG_ERROR("[Client] enet_host_create() failed"); return false; }

    ENetAddress addr{};
    enet_address_set_host(&addr, host.c_str());
    addr.port = port;

    m_peer = enet_host_connect(m_host, &addr, CHAN_COUNT, 0);
    if (!m_peer) { DZ_LOG_ERROR("[Client] enet_host_connect() failed"); return false; }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// pollConnect — 매 프레임 호출. ConnectAck 수신 완료 시 true 반환
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkClient::pollConnect() {
    if (!m_host) return false;

    ENetEvent ev;
    // 0ms 타임아웃 → 즉시 반환 (블로킹 없음)
    int ret = enet_host_service(m_host, &ev, 0);
    if (ret <= 0) return false;

    if (ev.type == ENET_EVENT_TYPE_CONNECT) {
        // Connected! Send Auth packet.
        AuthPacket authPkt{};
        authPkt.packetType = static_cast<uint8_t>(PacketType::C2S_Auth);
        std::strncpy(authPkt.username, m_authUsername.c_str(), 15);
        std::strncpy(authPkt.password, m_authPassword.c_str(), 15);
        authPkt.isRegister = m_isRegister ? 1 : 0;
        ENetPacket* pkt = enet_packet_create(&authPkt, sizeof(authPkt), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(m_peer, CHAN_RELIABLE, pkt);
        return false;
    }

    if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
        DZ_LOG_ERROR("[Client] Disconnected during handshake");
        m_peer = nullptr;
        m_authError = "Disconnected during handshake.";
        return false;
    }

    if (ev.type == ENET_EVENT_TYPE_RECEIVE && ev.packet) {
        bool done = false;
        if (ev.packet->dataLength >= 1) {
            auto ptype = static_cast<PacketType>(ev.packet->data[0]);
            if (ptype == PacketType::S2C_AuthAck) {
                if (ev.packet->dataLength >= sizeof(AuthAckPacket)) {
                    AuthAckPacket ack{};
                    std::memcpy(&ack, ev.packet->data, sizeof(ack));
                    if (ack.success == 0) {
                        m_authError = ack.message;
                        m_redirectPort = ack.redirectPort;
                        disconnect();
                        enet_packet_destroy(ev.packet);
                        return false;
                    } else {
                        m_isAuthenticated = true;
                        done = true; // Auth completed
                    }
                }
            }
            else if (ptype == PacketType::S2C_ConnectAck) {
                if (ev.packet->dataLength >= sizeof(ConnectAckPacket)) {
                    ConnectAckPacket ack{};
                    std::memcpy(&ack, ev.packet->data, sizeof(ack));
                    m_localNetID   = ack.netID;
                    m_localX       = ack.spawnX;
                    m_localY       = ack.spawnY;
                    m_localTeam    = ack.teamID;
                    m_localHp      = 100.0f;
                    m_localMaxHp   = 100.0f;
                    m_localFlags   = STATUS_ALIVE;
                    m_localDead    = false;
                    m_justSpawned  = true;
                    m_remoteCount  = 0;
                    m_zombieDeaths.clear();
                    m_predCount    = 0;
                    m_predHead     = 0;
                    done = true;
                    DZ_LOG_INFO("[Client] ConnectAck (async): netID=%u spawn=(%.0f,%.0f) team=%u",
                                m_localNetID, m_localX, m_localY, m_localTeam);
                }
            }
        }
        enet_packet_destroy(ev.packet);
        if (done) return true;
    }
    return false;
}

void NetworkClient::disconnect() {
    if (m_peer) {
        enet_peer_disconnect(m_peer, 0);
        m_peer = nullptr;
    }
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    enet_deinitialize();
    
    m_isAuthenticated = false;
    m_justSpawned = false;
    m_localNetID = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// update — drain ENet event queue
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::update(float dt) {
    if (!m_host) return;
    ENetEvent ev;
    while (enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            if (ev.packet->dataLength > 0) {
                auto ptype = static_cast<PacketType>(ev.packet->data[0]);
                if (ptype == PacketType::S2C_WorldSnapshot) {
                    processSnapshot(ev.packet->data, ev.packet->dataLength);
                } else if (ptype == PacketType::S2C_ConnectAck) {
                    // 서버가 새 매치 진입 등으로 새 netID를 부여할 경우 상태 리셋
                    if (ev.packet->dataLength >= sizeof(ConnectAckPacket)) {
                        ConnectAckPacket ack{};
                        std::memcpy(&ack, ev.packet->data, sizeof(ack));
                        m_localNetID  = ack.netID;
                        m_localX      = ack.spawnX;
                        m_localY      = ack.spawnY;
                        m_localTeam   = ack.teamID;
                        m_localHp     = 100.0f;
                        m_localMaxHp  = 100.0f;
                        m_localFlags  = 0;
                        m_localDead   = false;
                        m_justSpawned = true;
                        m_remoteCount = 0;
                        m_zombieDeaths.clear();
                        m_predCount   = 0;
                        m_predHead    = 0;
                        DZ_LOG_INFO("[Client] ConnectAck (fallback): netID=%u", m_localNetID);
                    }
                } else if (ptype == PacketType::S2C_DamageEvent) {
                    if (ev.packet->dataLength >= sizeof(DamageEventPacket)) {
                        DamageEventPacket dmg{};
                        std::memcpy(&dmg, ev.packet->data, sizeof(dmg));
                        if (dmg.victimID == static_cast<uint16_t>(m_localNetID)) {
                            m_localHp = dmg.remainingHp;
                            if (m_localHp < 0.0f) m_localHp = 0.0f;
                            m_recentHit = true;
                        }
                        if (dmg.attackerID == static_cast<uint16_t>(m_localNetID)) {
                            m_recentHit = true;
                        }
                        m_damageEvents.push_back(dmg);
                    }
                } else if (ptype == PacketType::S2C_HpSync) {
                    // 소모품 사용·피해 후 서버가 정확한 HP를 전송
                    if (ev.packet->dataLength >= sizeof(HpSyncPacket)) {
                        HpSyncPacket sync{};
                        std::memcpy(&sync, ev.packet->data, sizeof(sync));
                        if (sync.targetNetID == static_cast<uint16_t>(m_localNetID)) {
                            m_localHp     = sync.currentHp;
                            m_localMaxHp  = sync.maxHp;
                            m_localStamina = sync.currentStamina;
                            m_localMaxStamina = sync.maxStamina;
                            m_localFlags  = sync.flags;
                            DZ_LOG_DEBUG("[Client] HpSync: HP=%.0f/%.0f SP=%.0f/%.0f flags=0x%02X",
                                         m_localHp, m_localMaxHp, m_localStamina, m_localMaxStamina, m_localFlags);
                        }
                    }
                } else if (ptype == PacketType::S2C_TeamStatus) {
                    if (ev.packet->dataLength >= sizeof(TeamStatusPacket)) {
                        TeamStatusPacket ts{};
                        std::memcpy(&ts, ev.packet->data, sizeof(ts));
                        for (int k = 0; k < 4; ++k) m_teamAlive[k] = ts.aliveCount[k];
                        m_allianceBits = ts.allianceBits;
                        m_gameTimeSec  = ts.gameTimeSec;
                    }
                } else if (ptype == PacketType::S2C_AllianceAck) {
                    if (ev.packet->dataLength >= sizeof(AlliancePacket)) {
                        AlliancePacket ap{};
                        std::memcpy(&ap, ev.packet->data, sizeof(ap));
                        // 연합 상태는 TeamStatus로도 전달됨 — 여기선 로그만
                        DZ_LOG_INFO("[Client] Alliance %u↔%u: %s",
                            ap.teamA, ap.teamB, ap.active ? "FORMED" : "BROKEN");
                    }
                } else if (ptype == PacketType::S2C_DeathEvent) {
                    if (ev.packet->dataLength >= sizeof(DeathEventPacket)) {
                        DeathEventPacket death{};
                        std::memcpy(&death, ev.packet->data, sizeof(death));
                        if (death.victimID == static_cast<uint16_t>(m_localNetID)) {
                            m_localHp    = 0.0f;
                            m_localFlags = STATUS_DEAD;
                            m_localDead  = true;
                            
                            if (death.damageType == 4) { // Zombie
                                m_deathCause = "좀비에게 뜯어먹혔습니다.";
                            } else if (death.damageType == 3) { // Fire
                                m_deathCause = "불에 타죽었습니다.";
                            } else if (death.damageType == 2) { // Explosion
                                m_deathCause = "폭발에 휘말려 산산조각 났습니다.";
                            } else if (death.killerName[0] != '\0') {
                                m_deathCause = std::string(death.killerName) + "에게 살해당했습니다.";
                            } else {
                                m_deathCause = "과다 출혈로 사망했습니다.";
                            }
                            
                            DZ_LOG_INFO("[Client] 사망 처리: netID=%u", m_localNetID);
                        } else {
                            auto* rem = findRemote(death.victimID);
                            if (rem && rem->recType == REC_ZOMBIE) {
                                m_zombieDeaths.push_back(
                                    {death.victimID, rem->snap[1].x, rem->snap[1].y});
                            }
                        }
                    }
                } else if (ptype == PacketType::S2C_ExtractionUpdate) {
                    if (ev.packet->dataLength >= sizeof(PktExtractionUpdate)) {
                        PktExtractionUpdate upd{};
                        std::memcpy(&upd, ev.packet->data, sizeof(upd));
                        m_extractProg = upd.progress;
                    }
                } else if (ptype == PacketType::S2C_InventorySync) {
                    if (ev.packet->dataLength >= sizeof(InventorySyncPacket)) {
                        std::memcpy(&m_invSyncPkt, ev.packet->data, sizeof(m_invSyncPkt));
                        m_hasInvSync = true;
                    }
                } else if (ptype == PacketType::S2C_StashSync) {
                    if (ev.packet->dataLength >= sizeof(StashSyncPacket)) {
                        std::memcpy(&m_stashSyncPkt, ev.packet->data, sizeof(m_stashSyncPkt));
                        m_hasStashSync = true;
                    }
                } else if (ptype == PacketType::S2C_TurretFire) {
                    if (ev.packet->dataLength >= sizeof(TurretFirePacket)) {
                        TurretFirePacket tf{};
                        std::memcpy(&tf, ev.packet->data, sizeof(tf));
                        TurretBeam beam{};
                        beam.fromX = tf.fromX;
                        beam.fromY = tf.fromY;
                        beam.toX   = tf.toX;
                        beam.toY   = tf.toY;
                        beam.ownerTeam = tf.ownerTeam;
                        beam.ttl   = 0.15f;
                        m_turretBeams.push_back(beam);
                    }
                } else if (ptype == PacketType::S2C_SirenEvent) {
                    m_hasSirenEvent = true;
                }
            }
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            DZ_LOG_INFO("[Client] Disconnected from server");
            m_peer = nullptr;
            m_isAuthenticated = false;
            break;
        default: break;
        }
    }
    updateRemoteInterp(dt);
    tickTurretBeams(dt);
}

// ─────────────────────────────────────────────────────────────────────────────
// applyPrediction — move locally and store in ring buffer
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::applyPrediction(const InputState& input, float dt) {
    applyInputToPos(input, m_localX, m_localY, dt);

    // Push to ring buffer
    int idx = (m_predHead + m_predCount) % PREDICTION_BUFFER;
    m_predBuf[idx].seqNum = input.seqNum;
    m_predBuf[idx].input  = input;
    m_predBuf[idx].x      = m_localX;
    m_predBuf[idx].y      = m_localY;
    m_predBuf[idx].dt     = dt;  // store actual frame dt for exact replay

    if (m_predCount < PREDICTION_BUFFER) ++m_predCount;
    else m_predHead = (m_predHead + 1) % PREDICTION_BUFFER; // overwrite oldest
}

// ─────────────────────────────────────────────────────────────────────────────
// sendInput — serialize and send InputPacket (unreliable)
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::sendInput(const InputState& input) {
    if (!m_peer) return;
    InputPacket pkt{};
    pkt.packetType = static_cast<uint8_t>(PacketType::C2S_Input);
    pkt.seqNum     = input.seqNum;
    pkt.moveX      = input.moveX;
    pkt.moveY      = input.moveY;
    pkt.aimAngle   = input.aimAngle;
    pkt.actions    = input.actions;
    pkt.clientTick = static_cast<uint16_t>(input.seqNum / 3); // rough estimate
    pkt.clientDt   = input.dt;  // tell server exactly how long this frame lasted

    ENetPacket* ep = enet_packet_create(&pkt, sizeof(pkt), 0); // unreliable
    enet_peer_send(m_peer, CHAN_UNRELIABLE, ep);
}

// ─────────────────────────────────────────────────────────────────────────────
// processSnapshot — parse S2C_WorldSnapshot, reconcile own entity
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::processSnapshot(const uint8_t* data, size_t len) {
    if (len < sizeof(SnapshotHeader)) return;

    const auto* hdr = reinterpret_cast<const SnapshotHeader*>(data);
    if (hdr->packetType != static_cast<uint8_t>(PacketType::S2C_WorldSnapshot)) return;

    size_t offset = sizeof(SnapshotHeader);
    for (int i = 0; i < hdr->entityCount; ++i) {
        if (offset + sizeof(EntityStateRecord) > len) break;
        EntityStateRecord rec{};
        std::memcpy(&rec, data + offset, sizeof(EntityStateRecord));
        offset += sizeof(EntityStateRecord);

        if (!rec.verifyChecksum()) {
            DZ_LOG_WARN("[Client] Checksum mismatch for entity %u — discarded", rec.entityID);
            continue;
        }

        if (rec.entityID == static_cast<uint16_t>(m_localNetID)) {
            // ── Reconcile our own entity ──────────────────────────────────────
            // HP는 S2C_DamageEvent / S2C_DeathEvent 패킷으로 추적
            m_localFlags = rec.statusFlags;
            reconcile(rec.seqAck, rec.x, rec.y);
        } else {
            // ── Update remote interpolation buffer ────────────────────────────
            // HP: 좀비는 seqAck 재활용, 플레이어는 별도 패킷으로 수신
            float recHp = (rec.recordType == REC_ZOMBIE || rec.recordType == REC_BUILDING)
                          ? static_cast<float>(rec.seqAck) : 100.0f;

            auto* remote = findRemote(rec.entityID);
            if (!remote) {
                if (m_remoteCount < 512) {
                    remote = &m_remotes[m_remoteCount++];
                    remote->entityID = rec.entityID;
                    remote->recType  = rec.recordType;
                    remote->maxHp    = (recHp > 0.0f) ? recHp : 100.0f;
                    remote->snap[0]  = {rec.x, rec.y, rec.statusFlags,
                                        hdr->serverTick, recHp};
                    remote->snap[1]  = remote->snap[0];
                    remote->interpT  = 0.0f;
                    remote->snapDt   = WORLD_TICK_DT;
                }
            } else {
                // 과거 틱의 패킷(지연 도착)은 무시하여 과거로 순간이동(Rubber-banding) 방지
                if (hdr->serverTick < remote->snap[1].tick) {
                    continue;
                }

                // recordType 업데이트 (엔티티 ID 재사용 시 REC_PLAYER -> REC_LOOT 등 변경 가능)
                remote->recType = rec.recordType;

                // maxHp 갱신 (살아있을 때 최대값 추적)
                bool wasAlive = (remote->snap[1].statusFlags & STATUS_ALIVE) != 0;
                if (wasAlive && recHp > remote->maxHp)
                    remote->maxHp = recHp;

                remote->snap[0] = remote->snap[1];
                remote->snap[1] = {rec.x, rec.y, rec.statusFlags,
                                   hdr->serverTick, recHp};
                remote->interpT = 0.0f;
                remote->snapDt  = WORLD_TICK_DT;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// reconcile — server-authoritative correction
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::reconcile(uint16_t ackedSeq, float serverX, float serverY) {
    // Find the buffered prediction for ackedSeq
    for (int i = 0; i < m_predCount; ++i) {
        int idx = (m_predHead + i) % PREDICTION_BUFFER;
        if (m_predBuf[idx].seqNum != ackedSeq) continue;

        float dx   = serverX - m_predBuf[idx].x;
        float dy   = serverY - m_predBuf[idx].y;
        float err  = std::sqrt(dx*dx + dy*dy);

        if (err < RECONCILE_THRESHOLD) {
            // Within threshold — discard old records up to ackedSeq
            m_predHead  = (m_predHead + i + 1) % PREDICTION_BUFFER;
            m_predCount -= (i + 1);
            return;
        }

        // Correction needed — snap to server position and re-simulate
        DZ_LOG_DEBUG("[CSP] Correction %.1f px at seq %u", err, ackedSeq);

        m_localX = serverX;
        m_localY = serverY;

        // Safety: server position should already be wall-free, but clamp anyway
        // to prevent the client from ever being placed inside geometry.
        if (m_map) {
            constexpr float HW = 10.0f, HH = 10.0f;
            m_map->resolveAABB(m_localX, m_localY, HW, HH);
        }

        // Drop acknowledged records
        m_predHead  = (m_predHead + i + 1) % PREDICTION_BUFFER;
        m_predCount -= (i + 1);

        // Re-apply all subsequent (unacked) inputs
        replayInputsFrom(ackedSeq + 1);
        return;
    }
}

void NetworkClient::replayInputsFrom(uint32_t fromSeq) {
    for (int i = 0; i < m_predCount; ++i) {
        int idx = (m_predHead + i) % PREDICTION_BUFFER;
        if (m_predBuf[idx].seqNum < fromSeq) continue;

        // Re-apply input using the EXACT dt that was used originally.
        // Using a hardcoded 1/60 caused replay positions to differ from
        // the original prediction, leading to accumulated wall-drift.
        float replayDt = m_predBuf[idx].dt > 0.0f
                         ? m_predBuf[idx].dt
                         : (1.0f / 60.0f);
        applyInputToPos(m_predBuf[idx].input, m_localX, m_localY, replayDt);
        m_predBuf[idx].x = m_localX;
        m_predBuf[idx].y = m_localY;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// updateRemoteInterp — lerp all remote entities between snapshots
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::updateRemoteInterp(float dt) {
    for (int i = 0; i < m_remoteCount; ) {
        auto& rem = m_remotes[i];
        rem.interpT += dt;
        
        if (rem.interpT > 1.0f) {
            // 1초 이상 스냅샷 업데이트가 없으면 엔티티가 파괴되었거나 멀어진 것으로 간주하여 제거
            m_remotes[i] = m_remotes[--m_remoteCount];
            continue;
        }
        
        float t = rem.snapDt > 0.0f ? (rem.interpT / rem.snapDt) : 1.0f;
        if (t > 1.0f) t = 1.0f;
        (void)t; // position read by Renderer via snap[0/1] + interpT
        ++i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
RemoteEntityState* NetworkClient::findRemote(uint16_t id) {
    for (int i = 0; i < m_remoteCount; ++i)
        if (m_remotes[i].entityID == id) return &m_remotes[i];
    return nullptr;
}

void NetworkClient::applyInputToPos(const InputState& input,
                                     float& x, float& y, float dt) {
    // Mirror server MovementSystem speed selection (must match exactly for CSP)
    float speed = 128.0f; // SPEED_WALK
    bool sprint  = (input.actions & ACT_SPRINT) && !(input.actions & ACT_CROUCH);
    bool crouch  = (input.actions & ACT_CROUCH) != 0;
    if (sprint) speed = 224.0f;
    if (crouch) speed =  64.0f;

    float mx = input.moveX, my = input.moveY;
    float len = std::sqrt(mx*mx + my*my);
    if (len > 0.01f) { mx /= len; my /= len; }

    // 서버와 동일한 축 분리 이동 (CSP 일치)
    constexpr float HW = 10.0f, HH = 10.0f;
    x += mx * speed * dt;
    if (m_map) m_map->resolveAxisX(x, y, HW, HH);
    y += my * speed * dt;
    if (m_map) m_map->resolveAxisY(x, y, HW, HH);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendUseItem — 소모품 사용 요청 (신뢰 채널)
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::sendUseItem(const char* key) {
    if (!m_peer) return;
    UseItemPacket pkt{};
    pkt.packetType = static_cast<uint8_t>(PacketType::C2S_UseItem);
    std::strncpy(pkt.key, key, sizeof(pkt.key) - 1);
    ENetPacket* ep = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendAlliancePropose — 연합 제안 (신뢰 채널)
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::sendAlliancePropose(uint8_t toTeam) {
    if (!m_peer) return;
    uint8_t buf[2] = { static_cast<uint8_t>(PacketType::C2S_AlliancePropose), toTeam };
    ENetPacket* ep = enet_packet_create(buf, 2, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendBuildPlace — 건설 배치 요청 (신뢰 채널)
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::sendBuildPlace(int16_t tileX, int16_t tileY, uint8_t buildingType) {
    if (!m_peer) return;
    BuildPlacePacket pkt{};
    pkt.packetType   = static_cast<uint8_t>(PacketType::C2S_BuildPlace);
    pkt.tileX        = tileX;
    pkt.tileY        = tileY;
    pkt.buildingType = buildingType;
    ENetPacket* ep = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

void NetworkClient::sendCraftRequest(uint8_t recipeID) {
    if (!m_peer) return;
    CraftRequestPacket pkt{};
    pkt.packetType = static_cast<uint8_t>(PacketType::C2S_CraftRequest);
    pkt.recipeID = recipeID;
    ENetPacket* ep = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendLootPickup — 파밍 요청 (신뢰 채널)
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::sendLootPickup(uint32_t lootNetID) {
    if (!m_peer) return;
    LootPickupPacket pkt{};
    pkt.packetType = static_cast<uint8_t>(PacketType::C2S_LootPickup);
    pkt.lootNetID  = lootNetID;
    ENetPacket* ep = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

void NetworkClient::sendJoinMatch() {
    if (!m_peer) return;
    uint8_t type = static_cast<uint8_t>(PacketType::C2S_JoinMatch);
    ENetPacket* ep = enet_packet_create(&type, sizeof(type), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

void NetworkClient::sendStashTransfer(uint8_t srcType, uint8_t srcIdx, uint8_t dstType, uint8_t dstIdx) {
    if (!m_peer) return;
    PktStashTransfer pkt{};
    pkt.header.type = PacketType::C2S_StashTransfer;
    pkt.header.tick = 0;
    pkt.srcType = srcType;
    pkt.srcIdx = srcIdx;
    pkt.dstType = dstType;
    pkt.dstIdx = dstIdx;
    ENetPacket* ep = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHAN_RELIABLE, ep);
}

} // namespace dz
