# [최종 보고서] DeadZone 프로젝트

**팀원 A**: 서버 및 게임 로직 구현  
**팀원 B**: 클라이언트, 데이터 및 보고서 작성  

---

## 프로젝트 요약
DeadZone은 C++17과 SDL2, ENet, MySQL을 사용해 구현한 탑다운 좀비 서바이벌 멀티플레이어 게임입니다. 단순한 싱글 플레이 데모가 아니라, 서버가 권한을 가지고 월드 상태를 판정하는 구조로 설계했습니다. 플레이어는 4개 팀으로 나뉘어 폐허 도시를 탐색하고, 루트박스를 파밍하며, 소음에 반응하는 좀비를 피해 방어선을 구축하고 탈출존으로 탈출해야 합니다.

구현의 핵심은 **게임 프로그래밍에서 필요한 실시간 시뮬레이션, 네트워크 동기화, 충돌 판정, AI 상태 전이, 타일 기반 월드 상호작용**을 직접 설계했다는 점입니다. ECS 기반 월드 구조, 서버 권한 전투 판정, 클라이언트 예측과 보정, Raycast/OBB/AABB 충돌, 소음 기반 좀비 AI, BFS 화염 전파, MySQL 영속화를 주요 기술 요소로 삼았습니다.

---

## 1장. 게임 소개

### 1.1 개요
- **게임명**: DeadZone
- **장르**: 탑다운 좀비 서바이벌 멀티플레이어
- **핵심 컨셉**: 4팀(각 2인)으로 구성된 플레이어들이 폐허가 된 도시에서 좀비의 위협을 피해 생존하며, 궁극적으로 탈출존을 통해 무사히 탈출하는 서바이벌 게임입니다.

### 1.2 팀 구성 및 역할
- **팀원 A (서버·게임로직)**: ECS 기반 서버 구조 설계, 좀비 AI, Combat 시스템, Build 시스템, 탈출 및 네트워크 권한(Server Authority) 모델 구현 등 백엔드/로직 전담.
- **팀원 B (클라이언트·데이터·보고서)**: 클라이언트 렌더링 파이프라인(SDL2), UI(인벤토리 드래그 앤 드롭), 파티클 이펙트, 데이터(JSON) 정리 및 보고서 작성 등 프론트엔드 및 데이터/문서 전담.

---

## 2장. 게임 기획 개요

### 2.1 맵 구성
- **City Ruins (200×200 타일)**: 거대한 폐허 도시를 배경으로 하며, 주거(NW), 상업(NE), 군사(SW), 공업(SE) 4개 구역으로 나뉘어 각기 다른 파밍 경험과 위험도를 제공합니다.
- **건물 및 루트 배치**: 현재 생성 맵 기준 39개 건물이 배치되며, 서버는 라운드 시작 시 실내 55개, 실외 40개로 총 95개의 루트박스를 분산 스폰합니다.
- **스폰 시스템**: 4개의 팀이 맵의 4코너에 분산되어 스폰하며, 초반 생존과 파밍을 위해 각자의 거점을 확보해야 합니다.

> **[그림 1]** 맵 전체 구역 다이어그램
> *(최종 PDF 편집 단계에서 맵 다이어그램 스크린샷 삽입)*

### 2.2 핵심 메카닉
1. **노이즈 시스템**: 총격, 발소리, 건설 등 플레이어의 행동마다 고유의 소음 반경(noiseRadius)이 발생하여, 주변 좀비들의 어그로를 끌게 됩니다.
2. **팀 기반 PvP**: 팀 단위로 스폰되지만 플레이어 직접 공격은 팀킬과 타 팀 교전이 가능하며, 서버가 데미지 판정을 권한 있게 처리합니다.
3. **건설 시스템**: 수집한 재료(고철, 판자, 전자 부품 등)를 활용하여 바리케이드와 포탑을 설치해 방어선을 구축할 수 있습니다.
4. **탈출 시스템**: 게임 시작 5분(300초) 후 `map.json`에 지정된 4개의 탈출존이 활성화되며, 해당 구역에서 5초간 채널링(F키)을 유지하면 탈출에 성공합니다.
5. **자동 포탑 시스템**: 건설한 포탑이 설정된 사격 호(Arc) 범위 내에서 가장 가까운 적을 자동으로 조준하여 사격합니다.

### 2.3 아이템 및 무기 체계
- **등급 시스템**: Normal → Enhanced → Rare → Unique 순으로 아이템 가치가 나뉩니다.
- **루트박스**: 실내외에 배치된 파밍 상자에서 탄약, 회복약(Medkit, Bandage), 재료, 총기류를 획득할 수 있습니다.

> **[그림 2]** 아이템 및 무기 등급 체계 표
> *(최종 PDF 편집 단계에서 아이템 기획안 등급 표 삽입)*

### 2.4 주요 구현 기능 요약
| 구분 | 구현 내용 | 게임 프로그래밍 관점 |
|---|---|---|
| 서버 구조 | C++17 기반 자체 ECS, 서버 권한 월드 시뮬레이션 | 엔티티와 데이터를 분리하여 많은 오브젝트를 매 틱 일관되게 처리 |
| 멀티플레이 | ENet UDP, 입력 패킷, 월드 스냅샷, 신뢰/비신뢰 채널 분리 | 좌표처럼 최신성이 중요한 데이터와 인벤토리/데미지처럼 손실되면 안 되는 이벤트를 분리 |
| 이동/충돌 | 타일맵 AABB 충돌, 축 분리 슬라이딩, 넉백 감쇠 | 상용 물리 엔진 없이 탑다운 게임에 필요한 충돌만 직접 구현 |
| 좀비 AI | FSM, 시야 Raycast, 소음 이벤트, 밤 웨이브 | 거리, 시야, 소리, 시간대를 조합하여 플레이어 행동에 반응하는 AI 구성 |
| 전투 | 총기 Hitscan Raycast, 근접 OBB 판정, 출혈/화염 지속 피해 | 투사체/피격/상태이상을 서버에서 판정하여 클라이언트 단독 조작을 억제 |
| 월드 상호작용 | 화염 타일 전파, 문 파괴/수리, 건설, 포탑 | 맵 타일과 엔티티 시스템을 연결하여 플레이어 행동이 지형과 방어선에 영향 |
| UI/UX | 인벤토리 드래그 앤 드롭, 미니맵 구역명, 건설 단축키 안내 | 테스트 플레이 중 필요한 정보가 화면에서 바로 식별되도록 구성 |
| 영속화 | MySQL 계정, 인벤토리, 스태시 저장 | 탈출 성공 시 보유 인벤토리를 보존하고, 사망 시 인벤토리/장착 아이템은 잃도록 처리 |

---

## 3장. 기술 아키텍처

### 3.1 전체 구조
클라이언트와 서버는 ENet(UDP 기반)을 통해 통신하며, 서버가 게임 상태를 권한 있게 관리합니다. MySQL 데이터베이스가 설정된 환경에서는 계정, 인벤토리, 스태시를 영속 저장하고, DB 연결이 없으면 로그인과 회원가입을 차단합니다. 로컬 실행은 `DeadZoneClient` 또는 `start_deadzone.command`가 `.env.server`를 확인하고, 없을 경우 `scripts/setup_database.sh`를 Terminal에서 실행해 DB, 앱 계정, 테스트 로그인(`test` / `Test1234!`)을 먼저 생성한 뒤 진행합니다.

> **[그림 3]** Client ↔ ENet UDP ↔ Server ↔ MySQL 통신 흐름도
> *(최종 PDF 편집 단계에서 통신 흐름도 삽입)*

### 3.2 구현 방향과 게임 프로그래밍적 사고
본 프로젝트는 “보이는 화면”보다 서버 시뮬레이션을 우선으로 설계했습니다. 플레이어 이동, 충돌, 공격, 좀비 AI, 루트 획득, 건설, 탈출 성공 여부는 서버가 최종 판정합니다. 클라이언트는 입력을 빠르게 보내고, 받은 월드 스냅샷을 보간·보정하여 보여주는 역할을 맡습니다. 이 구조는 멀티플레이 게임에서 중요한 클라이언트 단독 상태 변조를 줄이고, 상태 일관성과 디버깅 가능성을 높입니다.

게임 프로그래밍 측면에서는 다음 세 가지를 중점으로 구현했습니다.

1. **실시간성**: 서버는 20Hz 고정 틱으로 월드를 갱신하고, 클라이언트는 입력 예측으로 조작 지연을 줄였습니다.
2. **판정 안정성**: 이동 충돌, 총기 Raycast, 근접 OBB, 시야 Raycast처럼 게임 규칙에 필요한 물리 판정만 직접 구현해 계산량을 통제했습니다.
3. **플레이 감각**: 좀비는 단순 추적이 아니라 소리, 시야, 출혈, 밤 시간대, 문/바리케이드 상태에 반응하며, 화염병과 건설은 맵 타일과 연결되어 전장이 변화하도록 만들었습니다.

### 3.3 자체 구현 ECS (Entity Component System)
- `World` 클래스를 중심으로 동작하며, `ComponentPool<T>`를 통해 메모리 연속성을 보장해 캐시 히트율을 높였습니다.
- Transform, Inventory, Health, Combat, Network, Building, ZombieAI 등 역할별 컴포넌트를 분리하여 유연한 객체 관리가 가능합니다.
- 지연 파괴(Deferred Destruction)를 도입해 시스템 순회 중 엔티티 삭제 안정성을 확보하고, Dirty Flag는 HP/인벤토리처럼 신뢰 채널로 별도 동기화해야 하는 상태를 선별 전송하는 데 활용했습니다.

**[코드 스니펫: ComponentPool 메모리 구조]**
```cpp
template<typename T>
class ComponentPool {
public:
    template<typename... Args>
    T& emplace(EntityID id, Args&&... args) {
        m_index[id] = static_cast<uint32_t>(m_data.size());
        m_owners.push_back(id);
        return m_data.emplace_back(std::forward<Args>(args)...);
    }
private:
    std::vector<T>                          m_data;
    std::vector<EntityID>                   m_owners;  // parallel to m_data
    std::unordered_map<EntityID, uint32_t>  m_index;
};
```
> **구현 설명**: `ComponentPool`은 `std::vector`를 기반으로 한 연속된 메모리 공간에 컴포넌트를 할당합니다. `m_index`를 통해 Entity ID로 빠른 접근이 가능하며, 컴포넌트 추가/삭제 시 벡터의 끝 요소를 빈 공간으로 스왑(Swap-and-Pop)하여 O(1)에 가까운 삭제와 캐시 친화적인 순회를 의도했습니다.

> **[그림 4]** ECS World/ComponentPool 구조도
> *(최종 PDF 편집 단계에서 ECS 구조도 다이어그램 삽입)*

### 3.4 네트워크 권한 모델
- **서버 권한 (Server Authority)**: 주요 게임 로직과 상태 판정은 서버에서 수행하여 클라이언트 단독 변조 가능성을 줄입니다.
- **클라이언트 사이드 예측 및 서버 롤백**: 클라이언트의 조작에 즉각적으로 반응하여 지연 시간(Lag)을 숨기고, 서버의 결과가 다를 경우 롤백(Rollback)하여 보정합니다.
- **채널 분리**: ENet의 `CHAN_RELIABLE`과 `CHAN_UNRELIABLE`을 분리해 중요한 이벤트와 잦은 상태 업데이트(좌표 등)의 트래픽을 효율적으로 관리했습니다.

**[코드 스니펫: 서버-클라이언트 패킷 프로토콜 구조 (Packet.h / Protocol.h)]**
```cpp
// 클라이언트 -> 서버 (CHAN_UNRELIABLE, 약 60Hz 입력 전송)
#pragma pack(push, 1)
struct InputPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_Input);
    uint32_t seqNum     = 0;     // 클라이언트 예측 보정용 시퀀스 번호
    float    moveX      = 0.0f;  // 정규화된 이동 벡터 [-1, 1]
    float    moveY      = 0.0f;
    float    aimAngle   = 0.0f;  // 마우스 조준 각도
    uint16_t actions    = 0;     // ACT_SHOOT, ACT_SPRINT 등 비트마스크
    uint16_t clientTick = 0;
    float    clientDt   = 0.0f;
};
static_assert(sizeof(InputPacket) == 25, "InputPacket size mismatch");
#pragma pack(pop)

// 서버 -> 클라이언트 (CHAN_UNRELIABLE, 20Hz 월드 스냅샷)
#pragma pack(push, 1)
struct EntityStateRecord {
    uint8_t  version     = PROTOCOL_VERSION;
    uint8_t  recordType  = REC_PLAYER;  // player, zombie, building, loot 등
    uint16_t seqAck      = 0;           // 서버가 처리한 마지막 입력 seq
    uint16_t entityID    = 0;
    uint8_t  statusFlags = 0;
    float    x           = 0.0f;
    float    y           = 0.0f;
    uint16_t checksum    = 0;           // Fletcher-16
};
static_assert(sizeof(EntityStateRecord) == 17, "EntityStateRecord must be exactly 17 bytes");
#pragma pack(pop)
```
> **구현 설명**: 대역폭을 줄이기 위해 `#pragma pack(push, 1)`로 구조체 패딩을 제거했습니다. 클라이언트는 이동, 조준, 사격, 재장전, 상호작용 같은 입력을 `InputPacket` 하나로 압축해 전송하고, 서버는 이를 검증한 뒤 `EntityStateRecord` 배열 기반의 월드 스냅샷으로 좌표와 상태 플래그를 브로드캐스트합니다. 스냅샷 레코드는 17바이트로 고정되어 있으며, Fletcher-16 체크섬으로 손상된 레코드를 걸러냅니다. 이 외에도 데미지, 인벤토리, 건설, 문, 탈출, 드랍 관련 패킷 타입을 분리하여 게임 내 상호작용을 처리합니다. 화염 타일은 엔티티 ID를 가진 월드 오브젝트가 아니라 타일 좌표 집합이므로, 스냅샷 레코드에 억지로 섞지 않고 `S2C_FireUpdate` 전용 패킷으로 별도 동기화하여 서버의 화염 상태와 클라이언트 바닥 그래픽을 일치시켰습니다.

**[코드 스니펫: 월드 스냅샷 최적화 브로드캐스팅 (NetworkSystem.cpp)]**
```cpp
void NetworkSystem::broadcastSnapshot(World& world, uint16_t tick) {
    // 1. SnapshotHeader 구조체와 EntityStateRecord 배열 버퍼 할당
    static uint8_t buf[sizeof(SnapshotHeader) +
                       MAX_SNAPSHOT_ENTITIES * sizeof(EntityStateRecord)];
    auto* header = reinterpret_cast<SnapshotHeader*>(buf);
    auto* records = reinterpret_cast<EntityStateRecord*>(buf + sizeof(SnapshotHeader));

    int count = 0;
    std::vector<EntityID> ordered = world.alive();
    std::stable_sort(ordered.begin(), ordered.end(), priorityByType);

    // 2. NetworkComponent를 가진 엔티티를 우선순위 순서로 직렬화
    for (EntityID id : ordered) {
        Entity e{id};
        auto* net = world.tryGet<NetworkComponent>(e);
        if (!net) continue;

        auto* xf  = world.tryGet<TransformComponent>(e);
        auto* hp  = world.tryGet<HealthComponent>(e);
        auto* bld = world.tryGet<BuildingComponent>(e);

        EntityStateRecord& rec = records[count++];
        rec.recordType = bld ? REC_BUILDING : (!hp ? REC_LOOT :
                         (hp->team == Team::Neutral ? REC_ZOMBIE : REC_PLAYER));
        rec.entityID   = static_cast<uint16_t>(net->netID);
        rec.x          = xf ? xf->x : 0.0f;
        rec.y          = xf ? xf->y : 0.0f;
        rec.computeChecksum();

        if (count >= MAX_SNAPSHOT_ENTITIES) {
            flushPacket();
        }
    }
    
    // 3. CHAN_UNRELIABLE(빠른 상태 전송)을 통해 접속 중인 모든 클라이언트에 전송
    size_t totalLen = sizeof(SnapshotHeader) + count * sizeof(EntityStateRecord);
    ENetPacket* peerPkt = enet_packet_create(buf, totalLen, 2); // unsequenced snapshot
    for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
        if (!m_peers[pi].connected) continue;
        enet_peer_send(m_peers[pi].peer, CHAN_UNRELIABLE, peerPkt);
    }
}
```
> **구현 설명**: 서버는 `NetworkComponent`를 가진 월드 엔티티를 스냅샷 레코드로 직렬화하고, 레코드 수가 `MAX_SNAPSHOT_ENTITIES`에 도달하면 패킷을 나누어 전송합니다. 이때 건물, 플레이어, 루트, 좀비 순서로 우선순위를 두어 화면 구성에 중요한 오브젝트가 먼저 전송되도록 정렬합니다. 각 클라이언트별로 자신의 엔티티 레코드에는 마지막으로 처리된 입력 시퀀스(`seqAck`)를 넣어 클라이언트 사이드 예측 보정에 사용합니다. `CHAN_UNRELIABLE` 채널을 이용해 좌표 업데이트는 최신성이 우선되도록 구성했습니다.

**[코드 스니펫: 클라이언트 사이드 예측 및 롤백 (NetworkClient.cpp)]**
```cpp
void NetworkClient::reconcile(uint16_t ackedSeq, float serverX, float serverY) {
    for (int i = 0; i < m_predCount; ++i) {
        int idx = (m_predHead + i) % PREDICTION_BUFFER;
        if (m_predBuf[idx].seqNum != ackedSeq) continue;

        float dx = serverX - m_predBuf[idx].x;
        float dy = serverY - m_predBuf[idx].y;
        float err = std::sqrt(dx*dx + dy*dy);

        // 오차가 임계값 이내면 기존 예측 유지 (서버 상태 수용)
        if (err < RECONCILE_THRESHOLD) {
            m_predHead = (m_predHead + i + 1) % PREDICTION_BUFFER;
            m_predCount -= (i + 1);
            return;
        }

        // 오차가 크면 서버 위치로 강제 스냅(Snap) 후 큐에 남은 입력 재적용
        m_localX = serverX;
        m_localY = serverY;
        m_predHead = (m_predHead + i + 1) % PREDICTION_BUFFER;
        m_predCount -= (i + 1);
        replayInputsFrom(ackedSeq + 1);
        return;
    }
}
```
> **구현 설명**: 클라이언트가 입력을 서버로 전송함과 동시에 로컬에서 먼저 시뮬레이션(Prediction)합니다. 서버로부터 스냅샷을 받으면 `reconcile`을 호출하여 과거 예측 위치와 서버의 실제 위치 오차를 계산하고, 틀어졌을 경우(롤백) 현재까지의 입력들을 다시 재적용(Replay)하여 네트워크 지연으로 인한 위치 튐을 줄입니다.

---

## 4장. 핵심 시스템 구현

### 4.1 ZombieAI (FSM 및 감각 시스템)
- **상태 전이**: `Idle` → `Alert` → `Chase` → `Frenzy` 단계로 동작합니다.
- **유형**: 기본 체력/속도를 가진 Shambler, 이동 속도가 빠른 Runner, 맷집이 강한 Brute 3종으로 세분화됩니다.
- 시야각(LOS) 검사와 노이즈 청각 시스템을 복합적으로 활용하여 자연스러운 추적 AI를 구현했습니다.

**[코드 스니펫: 좀비의 시야(LOS) 판정 및 출혈 후각 추적 (ZombieAI.cpp)]**
```cpp
// 플레이어와의 거리 계산 및 밤/낮에 따른 시야 범위(sightMult) 설정
float dx = nxf->x - xf->x, dy = nxf->y - xf->y;
float sightMult = isNight ? 2.4f : 0.9f;

// 대상이 출혈(Bleeding) 상태일 경우 피 냄새를 맡고 감지 반경이 2배로 증가
if (ncbt && ncbt->isBleeding) sightMult *= 2.0f;
float sightR = (ai.type == ZombieType::Runner)
             ? ZOMBIE_SIGHT_RADIUS * 1.5f * sightMult
             : ZOMBIE_SIGHT_RADIUS * sightMult;

if (dx*dx + dy*dy < sightR * sightR) {
    bool hasLOS = true;
    float dist = std::sqrt(dx*dx + dy*dy);
    int steps = static_cast<int>(dist / 16.0f); // 16픽셀 단위 레이캐스트(Raycast)
    
    // 플레이어와 좀비 사이에 벽(Solid)이 있는지 검사하여 벽 투시(Wallhack) 방지
    for (int i = 1; i < steps; ++i) {
        float cx = xf->x + dx * (static_cast<float>(i) / steps);
        float cy = xf->y + dy * (static_cast<float>(i) / steps);
        if (m_map->isSolid(TileMap::worldToTile(cx), TileMap::worldToTile(cy))) {
            hasLOS = false; break;
        }
    }

    if (hasLOS) { // 시야가 확보되었다면 추적(Chase) 상태로 전환
        ai.targetX = nxf->x; ai.targetY = nxf->y;
        if (ai.state == ZombieState::Idle) ai.state = ZombieState::Chase;
    }
}
```
> **구현 설명**: 단순히 거리만 가까워졌다고 플레이어를 인식하면 좀비가 벽 너머를 투시하는 불합리함이 생깁니다. 이를 방지하기 위해 16픽셀 단위로 선을 긋는 **Raycast(시선 확보) 연산**을 수행하여 시야에 장애물이 없는지 판별합니다. 밤에는 좀비의 시야 반경이 2.4배로 늘어나고, Runner는 기본 감지 반경이 더 큽니다. 플레이어가 출혈(Bleeding) 상태일 경우에는 **피 냄새를 맡고 감지 반경이 2배로 증폭**되도록 하여, 전투 후 회복과 도주 판단이 중요해지도록 설계했습니다.

**[코드 스니펫: 좀비 상태 전이 로직 일부]**
```cpp
switch (ai.state) {
case ZombieState::Idle:
    if (maxNoise >= static_cast<uint8_t>(NoiseCategory::Deafening)) {
        ai.state = ZombieState::Frenzy;
        ai.chainTriggered = false;
    } else if (maxNoise >= static_cast<uint8_t>(NoiseCategory::Loud)) {
        ai.state = ZombieState::Chase;
    } else if (maxNoise >= static_cast<uint8_t>(NoiseCategory::Soft)) {
        ai.alertLevel += 0.4f;
        if (ai.alertLevel >= 1.0f) ai.state = ZombieState::Alert;
    }
    break;
case ZombieState::Frenzy:
    if (!ai.chainTriggered) {
        ai.chainTriggered = true;
        triggerChainFrenzy(world, zombie, xf->x, xf->y);
    }
    // ...
```
> **구현 설명**: 매 틱마다 `update` 함수가 호출되며, `switch(ai.state)` 구문을 통해 상태를 전이합니다. 특히 광란(Frenzy) 상태 돌입 시 `triggerChainFrenzy` 함수를 호출하여 반경 내의 다른 좀비들을 연쇄적으로 깨우는(Chain Aggro) 시스템을 구현하여, 한 번의 실수가 대규모 웨이브로 이어지도록 설계했습니다.

**[코드 스니펫: 좀비 무리 이동(Flocking) 및 충돌 보정 (ZombieAI.cpp)]**
```cpp
void ZombieAISystem::doMovement(World& world, Entity zombie, ZombieAIComponent& ai, float dt) {
    // ... [상태에 따른 목표 좌표(destX, destY)와 속도 설정] ...
    float dx = destX - xf->x, dy = destY - xf->y;
    float dist = std::sqrt(dx*dx + dy*dy);
    float nx = dx / dist, ny = dy / dist;

    // 1. 목표를 향해 이동하며 벽체 충돌(슬라이딩) 처리
    xf->x += nx * speed * dt;
    m_map->resolveAxisX(xf->x, xf->y, 10.0f, 10.0f);
    xf->y += ny * speed * dt;
    m_map->resolveAxisY(xf->x, xf->y, 10.0f, 10.0f);

    // 2. 무리 행동(Flocking) - 좀비 간 겹침 방지(Separation)
    float pushX = 0.0f, pushY = 0.0f;
    for (EntityID id : world.alive()) {
        // 주변 다른 좀비와의 거리를 계산하여 밀어내는 벡터 생성
        float odx = xf->x - oxf->x, ody = xf->y - oxf->y;
        float d2 = odx*odx + ody*ody;
        if (d2 > 0.01f && d2 <= SEPARATION_R2) {
            float strength = 1.0f - (std::sqrt(d2) / SEPARATION_RADIUS);
            pushX += odx * (1.0f / std::sqrt(d2)) * strength;
            pushY += ody * (1.0f / std::sqrt(d2)) * strength;
        }
    }
    xf->x += pushX * 30.0f * dt;
    xf->y += pushY * 30.0f * dt;
}
```
> **구현 설명**: 좀비가 플레이어를 향해 이동할 때, 단순히 직선으로 쫓아오는 것을 넘어 **무리 지어 이동(Flocking)** 할 때 서로 겹쳐서 하나의 점처럼 뭉치지 않게 만드는 분리(Separation) 로직을 적용했습니다. 벽체와 충돌할 때는 자연스럽게 미끄러지도록 X/Y축을 분리하여 위치를 보정(`resolveAxis`)합니다.

**[코드 스니펫: 안전 반경 기반 좀비 스폰 및 밤 웨이브 생성 (GameServer.cpp)]**
```cpp
constexpr float ZOMBIE_PLAYER_SAFE_RADIUS = 960.0f;
constexpr float NIGHT_WAVE_MIN_SPAWN_DIST = 1400.0f;
constexpr float NIGHT_WAVE_ATTACK_GRACE = 4.0f;

auto tooCloseToPlayerOrSpawn = [&](float x, float y, float radius) {
    const float radius2 = radius * radius;
    for (EntityID id : m_world.alive()) {
        // 생존 플레이어 주변에는 일반 좀비를 즉시 생성하지 않음
        auto* xf = m_world.tryGet<TransformComponent>(Entity{id});
        if (!xf) continue;
        float dx = xf->x - x;
        float dy = xf->y - y;
        if (dx * dx + dy * dy < radius2) return true;
    }
    for (const auto& ps : m_map.getPlayerSpawns()) {
        // 라운드 초반 팀 스폰 지점 주변도 안전 반경으로 보호
        float sx = TileMap::tileCentre(ps.x);
        float sy = TileMap::tileCentre(ps.y);
        float dx = sx - x;
        float dy = sy - y;
        if (dx * dx + dy * dy < radius2) return true;
    }
    return false;
};

// 밤 웨이브는 모든 생존 플레이어 기준으로 화면 밖 먼 거리 후보만 허용
if (tooCloseToAnyPlayer(sx, sy,
    NIGHT_WAVE_MIN_SPAWN_DIST * NIGHT_WAVE_MIN_SPAWN_DIST)) continue;

// 생성 직후 즉시 공격하지 못하게 유예 시간을 둔 뒤 추격 시작
ai.state = ZombieState::Chase;
ai.targetX = txf->x;
ai.targetY = txf->y;
ai.attackTimer = NIGHT_WAVE_ATTACK_GRACE;
```
> **구현 설명**: 일반 좀비 리스폰은 파밍 건물과 열린 타일을 후보로 삼되, 플레이어와 팀 스폰 지점 반경 960px 안에는 생성하지 않도록 필터링했습니다. 따라서 서버가 처음 생성되거나 라운드가 리셋될 때 플레이어가 바라보는 시작 지역에 좀비가 갑자기 튀어나오는 문제를 줄였습니다. 밤 웨이브는 게임 플레이 압박을 유지하기 위해 모든 생존 플레이어 기준 1400px 이상 떨어진 화면 밖 후보만 허용하고, 생성 직후 4초간 공격 유예를 둔 뒤 `Chase` 상태로 추격하게 했습니다. 이는 멀티플레이 상황에서 한 플레이어 기준으로는 멀지만 다른 플레이어 바로 옆에 스폰되는 문제와, 웨이브 시작 직후 즉사하는 문제를 막기 위한 안전장치입니다.

> **[그림 5]** ZombieAI FSM 상태전이 다이어그램
> *(최종 PDF 편집 단계에서 FSM 상태전이 다이어그램 삽입)*

### 4.2 FireSystem (화염 처리)
- 화염병 투척 시 BFS(너비 우선 탐색) 알고리즘을 사용해 가연성 타일로 화염이 번져나갑니다.
- 화염방사기는 뿌린 위치에만 짧은 시간 남는 비전파 화염 타일을 생성하여, 집 전체로 번지지 않도록 별도 TTL/DPS를 사용합니다.
- 바리케이드나 포탑 같은 설치물과 접촉하면 해당 설치물을 파괴하는 방식으로 전장 변화를 유도합니다.

**[코드 스니펫: BFS 기반 화염 전파 알고리즘]**
```cpp
void FireSystem::spreadBFS(World& world, TileMap& map) {
    if (m_frontier.empty()) return;
    std::vector<std::pair<int16_t, int16_t>> currentFrontier = m_frontier;
    m_frontier.clear();

    for (const auto& ft : currentFrontier) {
        for (auto [dx, dy] : NEIGHBORS) {
            int16_t nx = ft.first + dx, ny = ft.second + dy;
            if (!map.inBounds(nx, ny) || m_tileSet.count(fireTileKey(nx, ny))) continue;

            if (map.isFlammable(nx, ny)) {
                igniteTile(nx, ny);
                map.burnTile(nx, ny);
                checkBuildingContact(world, map, nx, ny);
            }
        }
    }
}
```
> **구현 설명**: 매 `FIRE_SPREAD_INTERVAL` 주기로 호출되며, 화염의 최전선(`m_frontier`)에서 상하좌우 인접 타일을 검사합니다. 가연성 타일(나무, 데브리)일 경우 `igniteTile`을 호출하여 불을 붙이고 `m_frontier`에 편입시킵니다. 반면 화염방사기 화염은 `canSpread=false`, `FLAMETHROWER_FIRE_TTL=3.0f`, `FLAMETHROWER_FIRE_DPS=6.0f`로 생성되어 뿌린 위치에만 잠시 남습니다. `checkBuildingContact`를 통해 바리케이드나 포탑 같은 설치물에 닿은 화염은 해당 설치물을 파괴하도록 처리했습니다.

**[코드 스니펫: 화염 타일 네트워크 동기화 및 바닥 렌더링]**
```cpp
// GameServer.cpp - 서버의 FireSystem 타일 목록을 전용 패킷으로 전송
FireUpdatePacket firePkt{};
const auto& fireTiles = m_fire.tiles();
firePkt.tileCount = static_cast<uint8_t>(
    std::min(fireTiles.size(), static_cast<size_t>(MAX_FIRE_UPDATE_TILES)));
for (uint8_t i = 0; i < firePkt.tileCount; ++i) {
    firePkt.tiles[i].tx = fireTiles[i].tx;
    firePkt.tiles[i].ty = fireTiles[i].ty;
}
const size_t fireLen = 2 + static_cast<size_t>(firePkt.tileCount) * sizeof(FireTileRecord);
for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
    if (m_net.isConnected(pi)) {
        m_net.sendUnreliable(pi, &firePkt, fireLen);
    }
}

// Game.cpp - 클라이언트가 받은 타일 좌표를 바닥 화염 그래픽으로 렌더링
m_renderer.drawFire(m_net.fireTiles(), m_camera);
```
> **구현 설명**: 화염병 착탄 후 서버에는 실제 불 타일이 생성되지만, 이를 클라이언트에 보내지 않으면 데미지는 적용되어도 바닥 불 그래픽은 보이지 않습니다. 이를 해결하기 위해 `Packet.h`에 `FireUpdatePacket`과 `FireTileRecord`를 추가하고, 서버가 현재 `FireSystem::tiles()` 목록을 `CHAN_UNRELIABLE`로 전송하도록 했습니다. 클라이언트는 `NetworkClient`에서 타일 좌표 목록을 갱신한 뒤 `Renderer::drawFire()`를 호출하여 주황색 외곽과 노란색 중심부가 깜빡이는 바닥 화염을 그립니다. 라운드 재진입 시에는 기존 화염 목록을 비워 이전 라운드의 불이 남지 않도록 처리했습니다.

> **[그림 6]** FireSystem BFS 화염 전파 원리
> *(최종 PDF 편집 단계에서 BFS 화염 전파 원리 이미지 삽입)*

### 4.3 기타 주요 시스템

#### Combat System (총기 사격 및 전투 판정)
레이캐스트 기반의 사격 판정, AABB 기반 충돌, 출혈(DoT) 메카닉 등을 관리합니다.

**[코드 스니펫: 총기 사격 레이캐스트 판정 (GameLogic.cpp)]**
```cpp
void GameLogic::handleRangedFire(uint32_t ownerID, float aimAngle) {
    // ... [무기 정보 및 재장전 확인 로직] ...
    
    // Raycast: 조준 방향(aimAngle)으로 단계별(STEP)로 전진하며 충돌 확인
    float rad  = aimAngle * (PI / 180.0f);
    float dirX = std::sin(rad), dirY = -std::cos(rad);

    for (float t = STEP; t <= MAX_R; t += STEP) {
        float bx = xf->x + dirX * t;
        float by = xf->y + dirY * t;

        // 1. 엔티티 충돌 판정
        for (EntityID tid : m_world.alive()) {
            if (dx*dx + dy*dy < HIT_R2) {
                m_combat.applyDamage(m_world, target, e, stats.damage, DamageType::Bullet);
                m_onRangedFire(net->netID, xf->x, xf->y, bx, by, hp->team); // 총탄 궤적 이펙트 전송
                goto done; // 관통 불가 시 종료
            }
        }

        // 2. 타일(벽) 충돌 판정
        if (m_map.isSolid(TileMap::worldToTile(bx), TileMap::worldToTile(by))) {
            m_onRangedFire(net->netID, xf->x, xf->y, bx, by, hp->team);
            break;
        }
    }
done:;
}
```
> **구현 설명**: 총을 쏘면 조준 각도를 기준으로 일정한 간격(`STEP`)씩 나아가면서 `Raycast` 연산을 수행합니다. 투사체를 생성하지 않고 즉시(Hitscan) 판정하며, 탄환이 다른 플레이어나 좀비(엔티티)의 반경 내에 들어가거나, 맵의 벽(Solid 타일)에 부딪히면 궤적 탐색을 멈추고 서버에서 직접 피해를 적용합니다. 통과할 수 없는 벽 뒤의 적은 맞지 않게 됩니다.

**[코드 스니펫: 출혈 피해 로직 (CombatSystem.cpp)]**
```cpp
void CombatSystem::tickBleeding(World& world, float dt) {
    auto& cbtPool = world.pool<CombatComponent>();
    for (size_t i = 0; i < cbtPool.owners().size(); ++i) {
        auto& cbt = cbtPool.data()[i];
        if (!cbt.isBleeding) continue;

        cbt.bleedTimer -= dt;
        cbt.bleedAccumulator += cbt.bleedDps * dt;
        while (cbt.bleedAccumulator >= 1.0f) {
            hp->applyDamage(1.0f, DamageType::Melee);
            cbt.bleedAccumulator -= 1.0f;
        }
    }
}
```
> **구현 설명**: 출혈 상태일 때 매 틱마다 `bleedDps`에 따라 피해량이 누적되며, 1.0 이상이 될 때마다 정수 단위로 체력을 깎아 부동소수점 오차를 방지하고 정확하게 체력 감소를 클라이언트로 동기화합니다.

#### Noise System (소음 → 좀비 어그로 파이프라인)
게임 내 모든 행동(걷기, 달리기, 총격, 근접 공격 등)은 고유의 소음 반경을 가지며, 이 소음 이벤트는 실시간으로 좀비 AI에 전달됩니다.

**[코드 스니펫: 행동별 소음 반경 및 이벤트 수확 파이프라인 (CombatComponent.h + NoiseSystem.cpp)]**
```cpp
// ── 1단계: 행동별 소음 반경 정의 (CombatComponent.h) ──
constexpr float NOISE_WALK_RADIUS     =  80.0f;  // 걷기: 2.5m
constexpr float NOISE_RUN_RADIUS      = 240.0f;  // 달리기: 7.5m
constexpr float NOISE_MELEE_RADIUS    = 288.0f;  // 근접 공격: 9m
constexpr float NOISE_PISTOL_RADIUS   = 480.0f;  // 권총 발사: 15m
constexpr float NOISE_RIFLE_RADIUS    = 680.0f;  // 소총 발사: 21.25m

// ── 2단계: 이동 시 발소리 소음 발생 (MovementSystem.cpp) ──
if (cbt && (len > 0.01f)) {
    float noiseR = sprinting ? NOISE_RUN_RADIUS : NOISE_WALK_RADIUS;
    uint8_t cat  = sprinting ? 2 : 1; // Moderate : Soft
    if (crouching) { noiseR = 0.0f; cat = 0; } // 웅크리기(Crouch) = 완전 무음
    cbt->emitNoise(noiseR, cat);
}

// ── 3단계: NoiseSystem이 모든 엔티티의 소음을 수확하여 전역 이벤트 리스트에 등록 ──
void NoiseSystem::update(World& world, float dt) {
    // 기존 이벤트의 수명(TTL) 감소 후 만료된 이벤트 제거
    for (auto& ev : m_events) ev.ttl -= dt;
    m_events.erase(std::remove_if(m_events.begin(), m_events.end(),
        [](const WorldNoiseEvent& e) { return e.ttl <= 0.0f; }), m_events.end());

    // 이번 틱에 소음을 발생시킨 엔티티를 순회하여 전역 소음 이벤트로 변환
    for (size_t i = 0; i < cbtPool.owners().size(); ++i) {
        auto& cbt = cbtPool.data()[i];
        if (!cbt.hasNoise) { cbt.clearNoise(); continue; }
        auto* xf = xfPool.get(cbtPool.owners()[i]);
        if (xf) m_events.push_back({xf->x, xf->y, cbt.noiseRadius, cbt.noiseCategory, 0.6f});
        cbt.clearNoise(); // 1틱짜리 일회성 소음 플래그 초기화
    }
}
```
> **구현 설명**: 소음 시스템은 3단계 파이프라인으로 동작합니다. (1) 실제 플레이 행동에 `constexpr`로 고정된 소음 반경이 할당되어 있고, (2) 이동/사격/근접 공격 등의 로직에서 `emitNoise()`로 해당 틱의 소음을 등록하면, (3) `NoiseSystem::update()`가 매 틱마다 모든 엔티티의 소음 플래그를 수확하여 전역 `WorldNoiseEvent` 리스트에 좌표, 반경, 소음 카테고리와 함께 등록합니다. 좀비 AI(`ZombieAISystem`)는 이 리스트를 참조하여 상태 전이를 결정합니다. 웅크리기(Crouch) 시에는 소음이 0으로 설정되어 은밀한 플레이가 가능하고, 총격과 근접 공격은 걷기보다 큰 반경으로 좀비를 유인하도록 차등 처리했습니다.

#### Build System (건설 시스템)
인벤토리의 재료를 확인하고 소모하여 월드에 바리케이드와 포탑을 배치합니다.
**[코드 스니펫: 건설 자원 소모 로직 (BuildSystem.cpp)]**
```cpp
// Barricade: scrap_metal 2 + plank 2
if (type == BuildingType::Barricade) {
    hasMaterials = requireItem("scrap_metal", 2) && requireItem("plank", 2);
    if (hasMaterials) {
        consumeItem("scrap_metal", 2);
        consumeItem("plank", 2);
    }
}
// 자원 검증 통과 후 Entity 생성 및 맵에 점유(Occupied) 처리
if (hasMaterials) {
    Entity e = world.createEntity();
    auto& bld = world.addComponent<BuildingComponent>(e);
    bld.type = type;
    map.setOccupied(tileX, tileY, e.id, true);
}
```
> **구현 설명**: 클라이언트에서 건설 요청이 오면 람다 함수 `requireItem`과 `consumeItem`을 통해 서버 인벤토리 컴포넌트의 실제 슬롯을 검증합니다. 재료가 충족되면 새로운 건물 엔티티를 생성하고 타일맵에 점유 처리를 하여 다른 오브젝트와 겹치지 않게 만듭니다.

#### Turret AI (자동 포탑 조준 및 사격 호(Arc) 검사)
건설한 포탑은 독립적인 AI로 동작하며, 설정된 방향과 사격 호(Arc) 범위 내에서 가장 가까운 적을 자동으로 탐색하여 사격합니다.
**[코드 스니펫: 포탑 자동 조준 및 내적(Dot Product) 기반 사격 호 판정 (BuildSystem.cpp)]**
```cpp
void BuildSystem::updateTurrets(World& world, float dt) {
    for (size_t i = 0; i < bldPool.owners().size(); ++i) {
        auto& bld = bldPool.data()[i];
        if (bld.isDestroyed || bld.type != BuildingType::Turret) continue;

        bld.turretCooldown -= dt;
        if (bld.turretCooldown > 0.0f) continue;

        // ── 사거리(turretRange) 내 가장 가까운 적 탐색 ──
        float nearDist = bld.turretRange;
        Entity target{NULL_ENTITY};
        for (EntityID id : world.alive()) {
            // 같은 팀이면 공격하지 않음 (아군 판별)
            if (static_cast<uint8_t>(targetTeam) == bld.ownerTeam) continue;
            float dx = txf->x - xf->x, dy = txf->y - xf->y;
            float d  = std::sqrt(dx*dx + dy*dy);
            if (d > nearDist) continue;

            // ── 사격 호(Arc) 범위 제한: 내적(Dot Product)으로 각도 검사 ──
            if (bld.turretArcDeg < 355.0f) {
                float angleRad = bld.turretAngle * (PI / 180.0f);
                float dirX =  std::sin(angleRad);
                float dirY = -std::cos(angleRad);
                float dot  = (dx * dirX + dy * dirY) / (d > 0.001f ? d : 0.001f);
                float halfArc = bld.turretArcDeg * 0.5f * (PI / 180.0f);
                dot = std::max(-1.0f, std::min(1.0f, dot));
                if (std::acos(dot) > halfArc) continue; // 사격 호 밖이면 무시
            }
            nearDist = d; target = e;
        }
        if (!target.isValid()) continue;
        thp->applyDamage(bld.turretDamage, DamageType::Bullet);
        bld.turretCooldown = 1.0f / bld.turretFireRate;
    }
}
```
> **구현 설명**: 포탑은 매 틱마다 모든 생존 엔티티를 순회하며 사거리(`turretRange`) 안에 들어온 가장 가까운 적을 탐색합니다. 이때 핵심은 **내적(Dot Product)** 연산입니다. 포탑이 바라보는 전방 벡터(`dirX, dirY`)와 적까지의 방향 벡터 사이의 내적을 구한 뒤, `acos()`으로 사잇각을 계산하여 포탑의 사격 호(`turretArcDeg`)의 절반보다 작은지 비교합니다. 이를 통해 포탑을 360도 자유회전이 아닌, 특정 방향으로만 사격하는 현실적인 방어 시설로 만들었습니다. 플레이어 직접 공격은 팀킬이 가능하지만, 포탑은 설치 팀과 같은 팀 엔티티를 자동 표적에서 제외합니다.

#### Extraction System (탈출 시스템)
게임 시간 5분 후 탈출존이 열리며 5초 채널링 시 탈출에 성공합니다.
**[코드 스니펫: 탈출 채널링 처리 (ExtractionSystem.cpp)]**
```cpp
// 탈출 구역 내에 있고 채널링 중일 때
if (st.channeling) {
    st.channelTimer += dt;
    
    // 이동하거나 데미지를 입으면 채널링 취소
    float moveDist = std::sqrt(std::pow(xf->x - st.lastX, 2.0f) + std::pow(xf->y - st.lastY, 2.0f));
    if (moveDist > 4.0f || hp->currentHp < st.lastHp - 0.5f) {
        st.channeling = false;
        st.channelTimer = 0.0f;
    }

    if (st.channelTimer >= EXTRACTION_CHANNEL_TIME) {
        st.channeling = false;
        if (m_onExtracted) m_onExtracted(e, static_cast<uint8_t>(zone)); // 탈출 성공
    }
}
```
> **구현 설명**: 탈출존 내에서 F키를 눌러 채널링을 시작하면 `channelTimer`가 증가합니다. 이때 플레이어가 이동(4px 이상)하거나 대미지를 입으면 타이머가 초기화되어 긴장감을 유도하며, 5초(`EXTRACTION_CHANNEL_TIME`)를 채우면 성공 이벤트를 발생시킵니다.

#### Database (MySQL 영구 저장 및 인증)
플레이어의 계정 정보, 인벤토리, 스태시를 MySQL 서버에 저장합니다. 서버는 DB 연결 실패 시 로그인과 회원가입을 차단하며, 로컬 테스트도 `DeadZoneClient` 실행 중 자동으로 열리는 DB 설정 Terminal 또는 `scripts/setup_database.sh`로 MySQL DB와 테스트 계정을 생성한 뒤 실제 DB 인증 경로를 사용합니다. 기본 테스트 로그인은 `test` / `Test1234!`입니다.
**[코드 스니펫: 트랜잭션 기반 인벤토리 DB 저장 (Database.cpp)]**
```cpp
bool Database::saveAccount(const std::string& username, const InventoryComponent& inv) {
    if (!m_conn) return false;

    // 1. 트랜잭션(Transaction) 시작 - 중간에 실패하면 자동 롤백
    struct Transaction {
        MYSQL* conn; bool committed = false;
        Transaction(MYSQL* c) : conn(c) { mysql_query(conn, "START TRANSACTION"); }
        ~Transaction() { if (!committed) mysql_query(conn, "ROLLBACK"); }
        void commit() { mysql_query(conn, "COMMIT"); committed = true; }
    };
    Transaction txn(m_conn);

    // 2. 계정 소지금 업데이트
    std::snprintf(buf, sizeof(buf), "UPDATE accounts SET money=%d WHERE username='%s'", inv.money, escUser.data());
    query(std::string(buf));

    // 3. 기존 인벤토리 기록 일괄 삭제 (Wipe) 후 새로 Insert
    query("DELETE FROM inventory WHERE username='" + escUser.data() + "'");
    
    // 4. Grid / Equipped / Stash 아이템 하나당 INSERT (SQL Injection 방지를 위해 Escape)
    for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
        const Item& item = inv.slots[i];
        if (!item.isValid()) continue;
        mysql_real_escape_string(m_conn, escKey.data(), item.key.c_str(), item.key.size());
        std::snprintf(buf, sizeof(buf),
            "INSERT INTO inventory (username, slot_index, is_equipped, item_id, item_key) "
            "VALUES ('%s', %d, 0, %d, '%s')", escUser.data(), i, item.itemID, escKey.data());
        query(std::string(buf));
    }
    txn.commit(); // 모든 쿼리가 정상 실행되면 DB에 반영
    return true;
}
```
> **구현 설명**: 로비에서 스태시와 인벤토리를 옮길 때, 탈출에 성공할 때, 사망 후 인벤토리 손실을 반영할 때 DB에 기록합니다. 아이템 복사나 손실을 막기 위해 **트랜잭션(Transaction)** 객체를 활용했습니다. 기존 데이터를 삭제하고 새 아이템들을 Insert 하는 과정 중 쿼리가 실패하면, 소멸자(`~Transaction`)에서 `ROLLBACK`을 호출하여 중간 상태 저장을 방지합니다. 실제 저장 코드는 `is_equipped=0`을 그리드, `1`을 장비 슬롯, `2`를 로비 스태시로 구분해 같은 `inventory` 테이블에 저장합니다. DB 연결이 실패한 경우에는 인증 단계에서 접속을 차단하므로, 발표용 서버에서는 MySQL 접속 설정을 먼저 검증해야 합니다.

#### 사망/탈출 아이템 보존 규칙
아이템 보존은 서버가 명확히 구분합니다. 탈출 성공 시에는 현재 인벤토리, 장착 아이템, 스태시 상태를 DB에 저장하여 다음 로비에서 그대로 이어집니다. 반대로 사망 시에는 플레이어가 들고 있던 그리드 인벤토리와 장착 무기를 월드 루트로 드랍하고, DB에는 빈 그리드/빈 장비 슬롯과 기존 로비 스태시만 저장합니다. 따라서 스태시는 로비 보관함 역할만 하며, 사망한 플레이어의 소지품이 자동으로 스태시에 들어가지 않습니다.

### 4.4 플레이어 이동 처리 및 넉백 물리 (MovementSystem)
클라이언트로부터 받은 입력 패킷을 서버에서 물리적으로 시뮬레이션하는 핵심 시스템입니다.

**[코드 스니펫: 서버 측 이동 시뮬레이션 및 넉백 감쇠 (MovementSystem.cpp)]**
```cpp
void MovementSystem::applyInput(World& world, const TileMap& map,
                                 uint32_t ownerID, const InputPacket& input, float dt) {
    // ── 이동 속도 결정: 달리기, 걷기, 웅크리기에 따라 분기 ──
    float speed = SPEED_WALK;
    bool sprinting = (input.actions & ACT_SPRINT) && !(input.actions & ACT_CROUCH);
    bool crouching = (input.actions & ACT_CROUCH) != 0;
    if (sprinting) speed = SPEED_SPRINT;
    if (crouching) speed = SPEED_CROUCH;

    // ── 이동 벡터 정규화 (대각선 이동 시 속도 1.41배 방지) ──
    float mx = input.moveX, my = input.moveY;
    float len = std::sqrt(mx * mx + my * my);
    if (len > 0.01f) { mx /= len; my /= len; }

    // ── 축 분리 이동: X 이동→X 충돌 해결, Y 이동→Y 충돌 해결 ──
    xf->x += mx * speed * dt;
    map.resolveAxisX(xf->x, xf->y, ENTITY_HW, ENTITY_HH);
    xf->y += my * speed * dt;
    map.resolveAxisY(xf->x, xf->y, ENTITY_HW, ENTITY_HH);
}

// ── 넉백 물리: 근접 공격 피격 시 밀려남 + 지수 감쇠 ──
if (cbt.knockTimer > 0.0f) {
    xf->x += cbt.knockVx * dt;
    xf->y += cbt.knockVy * dt;
    cbt.knockTimer -= dt;
    cbt.knockVx *= (1.0f - 10.0f * dt); // 매 프레임 속도를 지수적으로 감쇠
    cbt.knockVy *= (1.0f - 10.0f * dt);
    map.resolveAABB(xf->x, xf->y, ENTITY_HW, ENTITY_HH); // 넉백 중에도 벽 충돌 해결
}
```
> **구현 설명**: 서버가 클라이언트의 입력 패킷(`InputPacket`)을 수신하면 `MovementSystem::applyInput()`을 호출하여 서버 측에서 이동을 시뮬레이션합니다. 대각선 이동 시 속도가 √2배(약 1.41배)로 뛰는 것을 방지하기 위해 이동 벡터를 정규화(Normalize)합니다. 축 분리 이동은 물리 충돌(4.5절)의 resolveAxis 함수와 연동하여 벽에 자연스럽게 미끄러지도록 처리합니다. 근접 공격에 피격당했을 때는 공격 방향으로 넉백(Knockback) 속도가 부여되며, `(1.0f - 10.0f * dt)` 계수를 곱해 매 프레임 지수적으로 감쇠시켜 밀려나다가 서서히 멈추는 자연스러운 물리 연출을 구현했습니다.

### 4.5 물리 엔진 및 충돌 처리 (Custom Collision)
상용 물리 엔진(Box2D 등)을 사용하지 않고, 타일 기반의 **AABB(Axis-Aligned Bounding Box)** 충돌 처리를 직접 구현하여 서버의 연산 부하를 최소화했습니다.

**[코드 스니펫: AABB 기반 충돌 슬라이딩 및 축 분리 처리 (TileMap.cpp)]**
```cpp
void TileMap::resolveAxisX(float& wx, float wy, float hw, float hh) const {
    constexpr float EPS = 0.001f;
    int x0 = worldToTile(wx - hw);
    int x1 = worldToTile(wx + hw - EPS);
    int y0 = worldToTile(wy - hh + EPS);   // 모서리 겹침 방지용 오차(EPS)
    int y1 = worldToTile(wy + hh - EPS);   
    
    // 플레이어의 AABB가 걸쳐 있는 맵 타일 범위를 순회하며 충돌 검사
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (!isSolid(tx, ty)) continue; // 통과 불가능한 벽(Solid)인지 확인
            
            float tL = tileToWorld(tx), tR = tL + TILE_SIZE;
            float ol  = (wx + hw) - tL; // 오른쪽 방향 겹침량
            float or_ = tR - (wx - hw); // 왼쪽 방향 겹침량
            
            if (ol <= 0.0f || or_ <= 0.0f) continue;
            
            // 더 적게 겹친 방향(최단 거리)으로 좌표를 밀어내어 충돌 해결
            if (ol < or_) wx -= ol; 
            else          wx += or_;
        }
    }
}
```
> **구현 설명**: 플레이어나 엔티티가 벽과 충돌할 때 멈춰버리는(Snagging) 현상을 막고 자연스럽게 벽을 타고 미끄러지듯(Sliding) 이동하게 만들기 위해, X축(`resolveAxisX`)과 Y축(`resolveAxisY`)의 충돌 계산을 완전히 분리하여 순차적으로 수행합니다. 또한 소수점 연산의 미세한 오차로 인해 평평한 벽의 타일 이음새에 걸리는 현상을 해결하고자 `EPS(0.001f)` 상수를 둔 Half-open interval 기법을 적용하여 물리 연산의 안정성을 높였습니다.

**[코드 스니펫: 근접 공격 회전 행렬(OBB) 충돌 검사 (CombatSystem.cpp)]**
```cpp
bool CombatSystem::tryMeleeAttack(World& world, Entity attacker) {
    // 1. 공격자의 방향(Rotation)을 라디안으로 변환하여 전방 벡터(fwdX, fwdY) 계산
    float rad = axf->rotation * (3.14159265f / 180.0f);
    float fwdX =  std::sin(rad), fwdY = -std::cos(rad);

    // 2. 공격자의 전방에 타격 박스(Hitbox)의 중심점(hitCX, hitCY) 생성
    float hitCX = axf->x + fwdX * (acbt->melee.range * 0.5f);
    float hitCY = axf->y + fwdY * (acbt->melee.range * 0.5f);
    float hw = acbt->melee.arcWidth * 0.5f, hh = acbt->melee.range * 0.5f;

    for (EntityID vid : world.alive()) {
        // 3. 대상의 위치를 타격 박스 중심점 기준 상대 좌표(vx, vy)로 변환
        float vx = vxf->x - hitCX;
        float vy = vxf->y - hitCY;
        
        // 4. 2D 회전 변환 행렬(Rotation Matrix)을 역으로 적용하여 Local Space로 변환
        float cosA = std::cos(-rad), sinA = std::sin(-rad);
        float localX = vx * cosA - vy * sinA;
        float localY = vx * sinA + vy * cosA;

        // 5. 회전이 풀린 직교 좌표계(AABB) 상태에서 폭(hw)과 높이(hh) 내부에 들어오는지 판별
        if (std::abs(localX) > hw || std::abs(localY) > hh) continue;

        // 판정을 통과하면 피격 처리 (데미지 적용, 넉백 등)
        applyDamage(world, victim, attacker, acbt->melee.damage, DamageType::Melee);
    }
}
```
> **구현 설명**: 근접 무기를 휘두를 때 직사각형 모양의 타격 범위가 플레이어의 바라보는 각도에 맞춰 비스듬하게 회전합니다. 상용 물리 엔진의 OBB(Oriented Bounding Box) 연산을 자체적으로 구현하기 위해, **2D 회전 변환 행렬(Rotation Matrix)**을 사용했습니다. 적의 좌표를 타격 박스의 중심점을 원점으로 하는 로컬 좌표계로 역회전(-rad)시켜 가져온 뒤, 단순한 AABB(축 정렬 bounding box) 충돌로 치환하여 매우 가볍고 정확하게 피격 판정을 수행합니다. (총기 사격에 사용된 Raycast 물리 연산은 앞선 4.3절에 포함되어 있습니다.)

---

## 5장. 클라이언트 구현

### 5.1 렌더링 및 카메라 처리
- **Y-Sort 렌더링**: 2D 탑다운 시점에서 입체감을 주기 위해 엔티티들의 Y 좌표를 기준으로 렌더링 순서를 정렬합니다.
- **카메라 (Camera)**: 플레이어의 움직임에 따라 카메라가 부드럽게 추적하며, 마우스 커서 위치에 따라 조준점 쪽으로 화면을 약간 이동시킵니다.
- **실내 투명화 및 시야 처리**: 플레이어가 건물 내부에 들어가면 지붕을 반투명하게 렌더링하여 내부 구조와 아이템을 확인할 수 있게 만듭니다.
- **시야각 (FOV) 및 Fog of War**: 마우스 커서 방향을 기준으로 120도 시야만 제공하며, 밤낮 시간에 따라 시야 반경이 축소됩니다.

**[코드 스니펫: 건물 진입 시 지붕 투명화 처리 (Renderer.cpp)]**
```cpp
// 매 프레임마다 플레이어의 좌표(localX, localY)가 건물 영역 내부에 있는지 판별
bool isInside = (localX >= bxWorld && localX <= bxWorld + bwWorld &&
                 localY >= byWorld && localY <= byWorld + bhWorld);

// 내부에 있을 때는 지붕의 알파(투명도) 값을 42로 낮춰 건물 내부를 보이게 함
if (isInside) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, roofCol.r, roofCol.g, roofCol.b, 42); // 반투명
    SDL_Rect roofTint = {sx1, sy1, pw, ph};
    SDL_RenderFillRect(m_renderer, &roofTint);
} else {
    // 외부에 있을 때는 지붕을 불투명(255) 타일 텍스처로 덮어 내부를 가림
    SDL_SetRenderDrawColor(m_renderer, roofCol.r, roofCol.g, roofCol.b, 255);
    SDL_RenderFillRect(m_renderer, &roofRect);
}
```
> **구현 설명**: 플레이어가 맵에 배치된 특정 구역(District)이나 건물 영역에 진입했을 때 `isInside` 플래그가 활성화됩니다. 외부에 있을 때는 지붕 텍스처를 불투명(Alpha=255)하게 렌더링하여 건물 내부의 적이나 전리품을 숨기고, 내부에 진입하는 순간 지붕을 반투명(Alpha=42)하게 전환하여 실내 교전이 가능하도록 시야를 자연스럽게 조절했습니다.

**[코드 스니펫: 120도 FOV 및 전장의 안개 구현 (Renderer.cpp)]**
```cpp
void Renderer::drawFOV(float wx, float wy, float aimAngleDeg, const Camera& cam, float gameTime) {
    if (!m_fowTexture || darkness <= 0.01f) return;

    // 1. 렌더 타깃을 FOW 텍스처로 전환 후 어두운 안개(알파값 적용)로 덮음
    SDL_SetRenderTarget(m_renderer, m_fowTexture);
    SDL_SetRenderDrawColor(m_renderer, r, g, b, fogAlpha);
    SDL_RenderClear(m_renderer);

    // 2. SDL_RenderGeometry를 사용하여 시야 부채꼴 내부를 완전 투명하게 뚫어줌 (alpha=0)
    for (int i = 0; i < CONE_SEGS; ++i) {
        float a0 = coneStart + i * coneStep;
        float a1 = coneStart + (i + 1) * coneStep;
        tri[1].position = {px + std::sin(a0) * R, py - std::cos(a0) * R};
        tri[2].position = {px + std::sin(a1) * R, py - std::cos(a1) * R};
        SDL_RenderGeometry(m_renderer, nullptr, tri, 3, nullptr, 0);
    }

    // 3. 양쪽 경계 그라데이션 적용 후 렌더 타깃 복원 및 화면에 텍스처 합성
    drawGradEdge(coneStart, -1.0f);
    drawGradEdge(coneEnd, 1.0f);
    SDL_SetRenderTarget(m_renderer, nullptr);
    SDL_RenderCopy(m_renderer, m_fowTexture, nullptr, nullptr);
}
```
> **구현 설명**: SDL2의 기본 기능만으로는 복잡한 마스킹이 불가능하여, 렌더 타깃(Render Target) 텍스처를 활용했습니다. 안개 텍스처를 먼저 어둡게 칠한 다음, `SDL_RenderGeometry`로 플레이어의 조준 방향(120도)을 투명한 색(`alpha=0`)으로 뚫고, 외곽선에는 그라데이션을 적용하여 부드러운 시야 경계를 만들었습니다.
- **파티클 및 환경 이펙트**: 총구 화염(Muzzle Flash), 탄피 배출, 피격 시 혈흔, 회복 이펙트에 더해 서버에서 동기화된 화염 타일을 바닥 그래픽으로 렌더링하여 화염병의 착탄 지점과 전파 범위를 시각적으로 확인할 수 있게 했습니다.
- **화면 연출**: 피격 시 히트 플래시(화면 붉어짐) 및 카메라 쉐이크를 적용했습니다.

### 5.2 UI / UX
- **인벤토리**: 마우스 드래그 앤 드롭 방식을 지원하여 직관적인 아이템 장착 및 슬롯 이동, 수량 분할 버리기가 가능합니다.
- **제작 UI**: 건설 모드 진입 시 포탑/바리케이드/제작대 등 조합에 필요한 재료 리스트를 직관적으로 표시합니다.

> **[그림 7]** 게임 플레이 인게임 스크린샷 (HUD 포함)
> *(최종 PDF 편집 단계에서 인게임 스크린샷 삽입)*

> **[그림 8]** 인벤토리 및 UI 스크린샷
> *(최종 PDF 편집 단계에서 인벤토리 창이 열려있는 UI 스크린샷 삽입)*

---

## 6장. 결과 및 소감

### 6.1 실행 환경
- **OS 및 개발 환경**: macOS, C++17
- **주요 라이브러리**: CMake, SDL2 (image, mixer, ttf), ENet, cJSON, MySQL (Connector)
- **빌드 방식**: CMake 빌드 도구를 활용 (`cmake --build build`)
- **가장 쉬운 실행 방식**: macOS Finder에서 `build/bin/DeadZoneClient`를 실행합니다. `.env.server`가 없으면 클라이언트가 자동으로 Terminal을 열어 DB 설정 스크립트를 실행하고, 설정 완료 후 서버를 자동 실행합니다. 기존 방식처럼 `start_deadzone.command`를 더블클릭해도 DB 준비 후 서버와 클라이언트를 함께 실행할 수 있습니다.
- **터미널 실행 방식**: `./run_game.sh`로 서버와 클라이언트를 함께 실행하거나, `build/bin/DeadZoneClient`를 실행합니다. 클라이언트는 실행 파일 위치를 기준으로 `assets/`, `data/`를 읽고, 로컬 서버가 없으면 `DeadZoneServer`를 자동 실행합니다.
- **DB 설정**: `scripts/setup_database.sh`를 실행하면 로컬 MySQL에 `deadzone` DB, `deadzone_user` 계정, 기본 테스트 로그인(`test` / `Test1234!`)을 만들고 `.env.server`를 작성합니다. MySQL 비밀번호 정책을 고려해 기본 앱 DB 비밀번호는 `Deadzone1234!`를 사용합니다. 직접 설정할 경우 환경변수(`DEADZONE_DB_HOST`, `DEADZONE_DB_USER`, `DEADZONE_DB_PASS`, `DEADZONE_DB_NAME`)를 사용합니다.

#### 교수님 테스트용 실행 절차
1. MySQL이 설치되어 있지 않다면 먼저 설치 및 실행합니다. macOS Homebrew 환경에서는 `brew install mysql && brew services start mysql`을 사용할 수 있습니다.
2. 프로젝트 폴더의 `build/bin/DeadZoneClient`를 실행합니다. 실행 파일이 없으면 먼저 `start_deadzone.command`를 더블클릭해 빌드를 수행합니다.
3. 최초 실행에서 `.env.server`가 없으면 클라이언트가 Terminal을 열고 DB 설정 스크립트를 실행합니다.
4. Terminal에서 MySQL 관리자 비밀번호를 요구하면 로컬 MySQL `root` 비밀번호를 입력합니다. 설정이 성공하면 `deadzone` DB, `deadzone_user` 앱 계정, 테스트 계정이 생성되고 `.env.server`가 작성됩니다.
5. 게임 로그인 화면에서 아이디 `test`, 비밀번호 `Test1234!`를 입력합니다.
6. MySQL 관리자 비밀번호를 모르는 경우에는 터미널에서 `MYSQL_ADMIN_USER`, `MYSQL_ADMIN_PASS`를 명시해 실행할 수 있습니다.

```bash
MYSQL_ADMIN_USER=root MYSQL_ADMIN_PASS='root비밀번호' ./scripts/setup_database.sh
./run_game.sh
```

7. `Access denied for user 'root'@'localhost'`가 표시되거나 비밀번호를 알 수 없는 환경에서는 MySQL에서 DB 생성 권한이 있는 계정 정보를 확인한 뒤 아래처럼 실행합니다.

```bash
MYSQL_ADMIN_USER='관리자계정' MYSQL_ADMIN_PASS='관리자비밀번호' ./scripts/setup_database.sh
```

### 6.2 구현 완료 주요 기능
| 기능 | 완료 내용 | 구현 포인트 |
|---|---|---|
| 서버 권한 멀티플레이 | 클라이언트 입력 수신, 서버 틱 처리, 월드 스냅샷 전송 | ENet 채널 분리, 입력 seqAck, 클라이언트 예측/보정 |
| ECS 월드 | 플레이어, 좀비, 건물, 루트, 화염 관련 데이터를 컴포넌트로 분리 | ComponentPool, 지연 파괴, Dirty Flag |
| 좀비 AI | Idle/Alert/Chase/Frenzy FSM, 소음/시야/출혈 감지, 밤 웨이브 | Raycast LOS, NoiseSystem, 안전 반경 스폰, 웨이브 공격 유예 |
| 전투 | 총기 사격, 근접 공격, 출혈, 화염 피해 | Hitscan Raycast, OBB 근접 판정, 서버 권한 `applyDamage` |
| 건설/방어 | 바리케이드, 포탑, 제작대, 문 수리/파괴 | 타일 점유, 재료 검증, 포탑 사격 호 내적 판정 |
| 화염 시스템 | 화염병 전파, 화염방사기 비전파 바닥 화염, 클라이언트 그래픽 동기화 | BFS 전파, FireUpdatePacket, 타일 단위 데미지 |
| 인벤토리/DB | 로비 스태시, 장비 장착, 아이템 이동/드랍, 사망 시 소지품 손실, 탈출 시 보존 | MySQL 트랜잭션 저장, 테스트 계정 자동 생성 |
| 실행 편의성 | 클라이언트 단독 실행, DB 자동 준비 Terminal, 테스트 계정 안내 | `DeadZoneClient`, `start_deadzone.command`, `setup_database.sh`, `.env.server` |

### 6.3 미완성 사항 및 한계점
- 좀비 개체 수가 맵 전역에 다수 스폰될 시, 충돌 처리나 탐색에서 전체 엔티티 순회가 발생하여 O(N²) 성능 병목 우려가 존재합니다. 향후 QuadTree 등 공간 분할 최적화가 요구됩니다.
- DB 미설정 환경에서는 로그인이 차단됩니다. `DeadZoneClient`가 `.env.server`를 찾지 못하면 Terminal을 열어 DB 설정을 유도하지만, MySQL 관리자 비밀번호는 테스트 환경의 로컬 설정에 맞게 입력해야 합니다.
- `data/sounds.json`에는 세분화된 사운드 키가 정의되어 있으나, 현재 실제 런타임에서 사용하는 기본 효과음 위주로 파일이 존재합니다. 발표 빌드에서는 누락 사운드 로그가 발생하지 않도록 키-파일 매칭 정리가 필요합니다.
- `smg_9mm` 전용 아이콘은 별도 PNG 에셋으로 추가했습니다. 남은 에셋 보강 항목은 세분화된 사운드 파일 매칭입니다.

### 6.4 배운 점 및 소감
- **팀원 A**: C++ 코어 레벨에서 ECS를 직접 설계하고 클라이언트 예측-롤백 모델을 구현하면서, 메모리 연속성과 서버-클라이언트 상태 동기화 문제의 높은 복잡도를 깊이 이해하게 되었습니다.
- **팀원 B**: SDL2를 이용한 저수준 렌더링 제어와 파티클/사운드 시스템을 붙이면서 게임 클라이언트 구조의 전반을 파악할 수 있었으며, 특히 문서화와 협업 스케줄 관리의 중요성을 몸소 체감했습니다.
