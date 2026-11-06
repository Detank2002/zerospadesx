#include "Server.h"

#include "Grenade.h"
#include "MapGen.h"
#include "Physics.h"
#include "Protocol.h"
#include "SolidMap.h"
#include "TrainingCourse.h"
#include "Vec3.h"
#include "VoxelMap.h"

#include <enet/enet.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace host {

namespace {

using proto::ByteReader;
using proto::ByteWriter;
using proto::PacketId;

// pyspades/constants.py, the ones this file uses and their neighbours (kept
// whole so a row reads like the original).
[[maybe_unused]] constexpr int kWeaponKill = 0, kHeadshotKill = 1, kMeleeKill = 2, kGrenadeKill = 3,
                               kFallKill = 4, kTeamChangeKill = 5, kClassChangeKill = 6;
[[maybe_unused]] constexpr int kBuildBlock = 0, kDestroyBlock = 1, kSpadeDestroy = 2, kGrenadeDestroy = 3;
constexpr int kToolSpade = 0, kToolBlock = 1, kToolWeapon = 2;
[[maybe_unused]] constexpr int kTorso = 0, kHead = 1, kArms = 2, kLegs = 3, kMelee = 4;
constexpr int kErrorWrongVersion = 3, kErrorFull = 4;
constexpr float kMaxBlockDistance = 6.0f;
constexpr float kMaxDigDistance = 6.0f;
constexpr float kMeleeDistance = 3.0f;
constexpr int kBuildTolerance = 5;
constexpr int kMeleeDamage = 100;
// Player ids are a byte and 255 means nobody.
constexpr int kSlots = 255;
constexpr double kTick = 1.0 / 60.0;
constexpr size_t kMapChunk = 8192;
// Teamplay: how long a relayed ping lasts, and how often one player may send.
constexpr float kPingDuration = 10.0f;
constexpr double kPingInterval = 1.0;
// A target back on its lane after it falls.
constexpr float kTargetRespawn = 2.0f;
// On the course a person is always blue; the targets are green.
constexpr int kPeople = 0, kTargets = 1;

double now_sec() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

bool finite3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool near3(const Vec3& a, const Vec3& b, float d) {
    return std::fabs(a.x - b.x) < d && std::fabs(a.y - b.y) < d && std::fabs(a.z - b.z) < d;
}

// --- the guns: pyspades/weapon.py -------------------------------------------
//
// The magazine is modelled because the replies depend on it: a reload that
// finishes is announced to its owner with the new clip and reserve, and that
// packet is how the client learns the reload is over.
struct GunSpec {
    float delay;
    int clip, stock;
    float reload_time;
    bool slow;
    int damage[4];
};
constexpr GunSpec kGuns[3] = {
    {0.5f, 10, 50, 2.5f, false, {49, 100, 33, 33}},    // rifle
    {0.11f, 30, 120, 2.5f, false, {29, 75, 18, 18}},   // SMG
    {1.0f, 6, 48, 0.5f, true, {27, 37, 16, 16}},       // shotgun
};
constexpr int kClipTolerance = 10;

struct Gun {
    int type = 0;
    int ammo = 10, stock = 50;
    bool shoot = false, reloading = false;
    double shoot_time = 0, next_shot = 0, reload_at = 0;

    const GunSpec& spec() const { return kGuns[type]; }
    void reset() {
        shoot = false;
        reloading = false;
        ammo = spec().clip;
        stock = spec().stock;
    }
    void restock() { stock = spec().stock; }
    int get_ammo(double now, bool no_max = false) const {
        int a = ammo;
        if (shoot) a = ammo - std::max(0, static_cast<int>(std::ceil((now - shoot_time) / spec().delay)));
        return no_max ? a : std::max(0, a);
    }
    bool is_empty(double now) const { return get_ammo(now, true) < -kClipTolerance || !shoot; }
    void set_shoot(bool v, double now) {
        if (v == shoot) return;
        if (v) {
            if (ammo <= 0) return;
            if (reloading && !spec().slow) return;
            shoot_time = std::max(now, next_shot);
            reloading = false;
        } else {
            const int before = ammo;
            ammo = get_ammo(now, true);
            next_shot = shoot_time + spec().delay * (before - ammo);
        }
        shoot = v;
    }
    void reload(double now) {
        if (reloading) return;
        const int a = get_ammo(now);
        if (!stock || a >= spec().clip) return;
        if (spec().slow && shoot && a) return;
        set_shoot(false, now);
        reloading = true;
        ammo = a;
        reload_at = now + spec().reload_time;
    }
    // True when a reload step finished just now: the owner is told.
    bool update(double now) {
        if (!reloading || now < reload_at) return false;
        reloading = false;
        if (spec().slow) {
            ammo += 1;
            stock -= 1;
            reload(now);
        } else {
            const int left = std::max(0, stock - (spec().clip - ammo));
            ammo += stock - left;
            stock = left;
        }
        return true;
    }
};

// world_c.cpp cube_line: the block line every client draws, voxel for voxel.
std::vector<Cell> cube_line(int x1, int y1, int z1, int x2, int y2, int z2) {
    std::vector<Cell> out;
    Cell c{x1, y1, z1};
    const int ddx = x2 - x1, ddy = y2 - y1, ddz = z2 - z1;
    const int sx = ddx < 0 ? -1 : 1, sy = ddy < 0 ? -1 : 1, sz = ddz < 0 ? -1 : 1;
    const long kFar = 0x3fffffff / 512;
    long dx, dy, dz, dxi, dyi, dzi;
    auto la = [](long v) { return v < 0 ? -v : v; };
    if (la(ddx) >= la(ddy) && la(ddx) >= la(ddz)) {
        dxi = 1024; dx = 512;
        dyi = !ddy ? kFar : la(ddx * 1024L / ddy); dy = dyi / 2;
        dzi = !ddz ? kFar : la(ddx * 1024L / ddz); dz = dzi / 2;
    } else if (la(ddy) >= la(ddz)) {
        dyi = 1024; dy = 512;
        dxi = !ddx ? kFar : la(ddy * 1024L / ddx); dx = dxi / 2;
        dzi = !ddz ? kFar : la(ddy * 1024L / ddz); dz = dzi / 2;
    } else {
        dzi = 1024; dz = 512;
        dxi = !ddx ? kFar : la(ddz * 1024L / ddx); dx = dxi / 2;
        dyi = !ddy ? kFar : la(ddz * 1024L / ddy); dy = dyi / 2;
    }
    if (sx >= 0) dx = dxi - dx;
    if (sy >= 0) dy = dyi - dy;
    if (sz >= 0) dz = dzi - dz;
    for (;;) {
        out.push_back(c);
        if (out.size() >= 64) return out;
        if (c.x == x2 && c.y == y2 && c.z == z2) return out;
        if (dz <= dx && dz <= dy) {
            c.z += sz;
            if (c.z < 0 || c.z >= 64) return out;
            dz += dzi;
        } else if (dx < dy) {
            c.x += sx;
            if (c.x < 0 || c.x >= 512) return out;
            dx += dxi;
        } else {
            c.y += sy;
            if (c.y < 0 || c.y >= 512) return out;
            dy += dyi;
        }
    }
}

std::string sanitise_name(std::string s) {
    std::string out;
    for (char ch : s) {
        if (ch == '%' || ch == '#' || ch == '\r' || ch == '\n' || ch == 0) continue;
        out.push_back(ch);
    }
    if (out.size() > 15) out.resize(15);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

struct Player {
    ENetPeer* peer = nullptr;
    int id = -1;
    // Map transfer: the compressed map, how much is sent, and what was
    // broadcast meanwhile that this player must see after it.
    std::shared_ptr<const std::vector<uint8_t>> map_data;
    // The map revision this player needs at least: edits after it reach them
    // through `saved`, edits before it must be in the snapshot they are sent.
    uint64_t need_rev = 0;
    size_t map_off = 0;
    bool loading = false;
    std::vector<std::vector<uint8_t>> saved;

    bool named = false;
    std::string name;
    int team = -2;          // -2 not chosen yet, -1 spectator, 0 blue, 1 green
    int weapon = 0;
    int tool = kToolWeapon;
    uint32_t color = 0x707070;   // 0xRRGGBB
    int kills = 0;

    bool alive = false;
    int hp = 0;
    int grenades = 3, blocks = 50;
    Gun gun;
    physics::Body body;
    physics::Input input;
    bool primary = false, secondary = false;
    Vec3 line_start{};
    bool line_started = false;
    double respawn_at = -1;
    double last_ping = -1e9;

    bool client_info = false;
    // A target the server owns and walks along this lane of the range itself
    // (no connection behind it); -1 for everybody else.
    int lane = -1;
    bool to_far = true;      // which end of the lane it is walking to
    bool ext_teamplay = false;
    bool ext_flashlight = false;   // claimed Flashlight (0x32)
    bool lamp = false;             // its flashlight is lit

    bool spectator() const { return team == -1; }
    bool playing() const { return named && (team == 0 || team == 1); }
};

struct Grenade {
    int owner;
    int team;
    Vec3 pos;
    double explode_at;
};

struct Server::Impl {
    HostConfig cfg;
    ENetHost* host = nullptr;
    bool enet_up = false;
    VoxelMap map;
    SolidMap phys_map;
    std::mt19937 rng{std::random_device{}()};
    std::array<std::unique_ptr<Player>, kSlots> players{};
    std::vector<Grenade> grenades;
    double world_time = 0;      // the physics clock: advanced in fixed ticks
    // When the next world update is due: a deadline, not "a period since the
    // last", so a 60Hz update on a 120Hz loop goes out every second pass.
    double next_network = 0;
    // The map as sent to joiners, and the revision it was taken at. map_rev
    // counts edits. Re-encoding the map takes a tenth of a second or more, so
    // it is done on a worker from a snapshot, never on this thread.
    std::shared_ptr<const std::vector<uint8_t>> map_cache;
    uint64_t map_rev = 1, cache_rev = 0;
    std::unique_ptr<VoxelMap> snapshot;
    std::thread map_job;
    std::atomic<bool> job_done{false};
    bool job_running = false;
    uint64_t job_rev = 0;
    std::shared_ptr<const std::vector<uint8_t>> job_result;
    const uint32_t team_color[2] = {0x0000C4, 0x00C400};
    const uint32_t fog = 0x80E8FF;

    void log(const char* fmt, ...) {
        if (!cfg.log) return;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        cfg.log(buf);
    }

    // --- sending ----------------------------------------------------------
    void send(Player& p, const ByteWriter& bw, bool reliable = true) {
        if (!p.peer) return;
        ENetPacket* pkt = make_packet(bw, reliable);
        if (enet_peer_send(p.peer, 0, pkt) < 0) enet_packet_destroy(pkt);
    }
    static ENetPacket* make_packet(const ByteWriter& bw, bool reliable) {
        return enet_packet_create(bw.data.data(), bw.data.size(),
                                  reliable ? ENET_PACKET_FLAG_RELIABLE
                                           : (ENET_PACKET_FLAG_UNSEQUENCED |
                                              ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT));
    }
    // pyspades broadcast_contained: everybody with an id, except `sender`, on
    // `team` if given. A player still loading the map gets it after the map if
    // `save`, and never otherwise. One packet for everybody: ENet counts its
    // references, so a relay is not a copy per recipient.
    void broadcast(const ByteWriter& bw, bool save, int sender = -1, int team = -2,
                   bool reliable = true) {
        ENetPacket* pkt = nullptr;
        for (auto& pp : players) {
            if (!pp || pp->id == sender) continue;
            if (team != -2 && pp->team != team) continue;
            if (pp->loading) {
                if (save) pp->saved.push_back(bw.data);
            } else if (pp->peer) {
                if (!pkt) pkt = make_packet(bw, reliable);
                enet_peer_send(pp->peer, 0, pkt);
            }
        }
        if (pkt && pkt->referenceCount == 0) enet_packet_destroy(pkt);
    }
    void system_chat(const std::string& text, int to = -1) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::ChatMessage));
        bw.write_u8(255);
        bw.write_u8(static_cast<uint8_t>(proto::ChatType::System));
        bw.write_cstring(text);
        if (to >= 0 && players[to]) send(*players[to], bw);
        else broadcast(bw, false);
    }

    static void write_color(ByteWriter& bw, uint32_t rgb) {
        bw.write_u8(static_cast<uint8_t>(rgb));
        bw.write_u8(static_cast<uint8_t>(rgb >> 8));
        bw.write_u8(static_cast<uint8_t>(rgb >> 16));
    }

    // --- the map ----------------------------------------------------------
    void load_map() {
        generate_training_map(map);
        map.to_solid_map(phys_map);
    }
    void serve_maps() {
        if (job_running && job_done.load()) {
            map_job.join();
            job_running = false;
            map_cache = job_result;
            cache_rev = job_rev;
        }
        bool waiting = false;
        for (auto& pp : players) {
            if (!pp || !pp->loading || pp->map_data) continue;
            if (map_cache && cache_rev >= pp->need_rev) start_map(*pp);
            else waiting = true;
        }
        if (!waiting || job_running) return;
        if (!snapshot) snapshot = std::make_unique<VoxelMap>();
        *snapshot = map;
        job_rev = map_rev;
        job_done = false;
        job_running = true;
        map_job = std::thread([this]() {
            job_result = std::make_shared<const std::vector<uint8_t>>(snapshot->save_compressed());
            job_done = true;
        });
    }
    void start_map(Player& p) {
        p.map_data = map_cache;
        p.map_off = 0;
        ByteWriter start;
        start.write_u8(static_cast<uint8_t>(PacketId::MapStart));
        start.write_u32(static_cast<uint32_t>(p.map_data->size()));
        send(p, start);
    }
    void touched(const std::vector<Cell>& cells, bool solid) {
        for (const Cell& c : cells) phys_map.set_block(c.x, c.y, c.z, solid);
        if (!cells.empty()) ++map_rev;
    }

    // pyspades get_random_location: a land column in the box, on its top.
    Vec3 random_location(int x1, int y1, int x2, int y2) {
        int x, y;
        map.random_point(x1, y1, x2, y2, rng, x, y);
        return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(map.get_z(x, y))};
    }

    // --- StateData --------------------------------------------------------
    // Territory control with no territories: the course has nothing to
    // capture, so it is never won.
    ByteWriter state_data(int player_id) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::StateData));
        bw.write_u8(static_cast<uint8_t>(player_id));
        write_color(bw, fog);
        write_color(bw, team_color[0]);
        write_color(bw, team_color[1]);
        bw.write_fixed_string("Blue", 10);
        bw.write_fixed_string("Green", 10);
        bw.write_u8(1);
        bw.write_u8(0);
        for (size_t i = 0; i < 16; ++i)
            for (int k = 0; k < 13; ++k) bw.write_u8(0);
        return bw;
    }

    // --- connecting -------------------------------------------------------
    void on_connect(ENetPeer* peer, uint32_t version) {
        if (version != proto::PROTOCOL_VERSION) {
            enet_peer_disconnect(peer, kErrorWrongVersion);
            return;
        }
        int id = -1;
        int count = 0;
        for (int i = 0; i < kSlots; ++i) {
            if (players[i]) ++count;
            else if (id < 0) id = i;
        }
        if (id < 0 || count >= cfg.max_players + training::kLaneCount) {
            enet_peer_disconnect(peer, kErrorFull);
            return;
        }
        // ENet's packet throttle drops unreliable packets when the round trip
        // grows, which is a guess about a congested internet link. On
        // loopback it only makes things worse: a deceleration of zero holds the
        // throttle open, and ENet passes the setting to the client's end too.
        enet_peer_throttle_configure(peer, ENET_PEER_PACKET_THROTTLE_INTERVAL,
                                     ENET_PEER_PACKET_THROTTLE_SCALE, 0);
        peer->packetThrottle = ENET_PEER_PACKET_THROTTLE_SCALE;
        auto p = std::make_unique<Player>();
        p->peer = peer;
        p->id = id;
        p->loading = true;
        peer->data = p.get();
        // What arrives after the map: everybody already here, then the state.
        for (auto& o : players) {
            if (!o || !o->named) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::ExistingPlayer));
            bw.write_u8(static_cast<uint8_t>(o->id));
            bw.write_u8(static_cast<uint8_t>(static_cast<int8_t>(o->team)));
            bw.write_u8(static_cast<uint8_t>(o->weapon));
            bw.write_u8(static_cast<uint8_t>(o->tool));
            bw.write_u32(static_cast<uint32_t>(o->kills));
            write_color(bw, o->color);
            bw.write_cstring(o->name);
            p->saved.push_back(bw.data);
        }
        p->saved.push_back(state_data(id).data);
        p->need_rev = map_rev;
        players[id] = std::move(p);
        Player& pr = *players[id];
        // MapStart goes first, before anything but the handshake; it waits for
        // a map at least as new as this moment (serve_maps).
        if (map_cache && cache_rev >= pr.need_rev) start_map(pr);
        send_handshake(pr);
        log("player #%d connecting", id);
    }
    void send_handshake(Player& p) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::VersionHandshakeInit));
        bw.write_u32(42);
        send(p, bw);
    }
    // Map chunks this pass may still send, across every joining player.
    int chunk_budget = 0;
    void continue_map(Player& p) {
        if (!p.loading || !p.map_data) return;
        // Loopback has no loss and no queue worth speaking of: keep a few
        // hundred KB in flight and the whole map is over in well under a
        // second, where pyspades' ten chunks a frame take several.
        int sent = 0;
        while (p.map_off < p.map_data->size() && sent < 32 && chunk_budget > 0 &&
               p.peer->reliableDataInTransit < 512 * 1024) {
            --chunk_budget;
            const size_t n = std::min(kMapChunk, p.map_data->size() - p.map_off);
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::MapChunk));
            bw.write_bytes(p.map_data->data() + p.map_off, n);
            send(p, bw);
            p.map_off += n;
            ++sent;
        }
        if (p.map_off < p.map_data->size()) return;
        p.map_data.reset();
        p.loading = false;
        for (auto& d : p.saved) {
            ENetPacket* pkt = enet_packet_create(d.data(), d.size(), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(p.peer, 0, pkt);
        }
        p.saved.clear();
    }

    void on_disconnect(Player& p) {
        if (p.named) {
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::PlayerLeft));
            bw.write_u8(static_cast<uint8_t>(p.id));
            broadcast(bw, true, p.id);
            log("%s left", p.name.c_str());
        }
        players[p.id].reset();
    }

    // --- a player's life --------------------------------------------------
    void set_weapon(Player& p, int weapon) {
        p.weapon = std::min(std::max(weapon, 0), 2);
        p.gun = Gun{};
        p.gun.type = p.weapon;
        p.gun.reset();
    }

    void refill(Player& p) {
        p.hp = 100;
        p.grenades = 3;
        p.blocks = 50;
        p.gun.restock();
    }

    // A target on its lane of the range, a person at the start.
    Vec3 spawn_location(Player& p) {
        if (p.lane >= 0) {
            const int x = training::kLaneX[p.lane];
            return Vec3{static_cast<float>(x), 256.0f, static_cast<float>(map.get_z(x, 256))};
        }
        return random_location(training::kSpawnX - 2, training::kSpawnY - 2, training::kSpawnX + 3,
                               training::kSpawnY + 3);
    }

    void spawn(Player& p) {
        p.respawn_at = -1;
        if (p.team == -2) return;
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::CreatePlayer));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(p.weapon));
        bw.write_u8(static_cast<uint8_t>(static_cast<int8_t>(p.team)));
        if (!p.spectator()) {
            Vec3 at = spawn_location(p);
            at.x += 0.5f;
            at.y += 0.5f;
            at.z -= 2.4f;
            p.body = physics::Body{};
            p.body.pos = at;
            p.body.eye = at;
            p.body.ori = Vec3{p.team == 0 ? 1.0f : -1.0f, 0, 0};
            p.input = physics::Input{};
            p.alive = true;
            p.tool = kToolWeapon;
            p.gun.reset();
            refill(p);
            bw.write_vec(at);
        } else {
            p.alive = false;
            bw.write_vec(Vec3{0, 0, 0});
        }
        bw.write_cstring(p.name);
        broadcast(bw, true);
        if (!p.client_info) send_handshake(p);
    }

    float respawn_seconds(const Player& p) const {
        return p.lane >= 0 ? kTargetRespawn : cfg.respawn_sec;
    }
    void schedule_respawn(Player& p) {
        if (p.respawn_at < 0) p.respawn_at = world_time + respawn_seconds(p);
    }

    void kill(Player& p, Player* by, int kill_type) {
        if (!p.alive) return;
        p.alive = false;
        p.hp = 0;
        p.gun.reset();
        p.lamp = false; // every client puts it out on the Kill Action
        const float delay = respawn_seconds(p);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::KillAction));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(by ? by->id : p.id));
        bw.write_u8(static_cast<uint8_t>(kill_type));
        bw.write_u8(static_cast<uint8_t>(std::min(255, static_cast<int>(delay) + 1)));
        broadcast(bw, true);
        if (by && by != &p) by->kills += 1;
        p.respawn_at = world_time + delay;
    }

    void set_hp(Player& p, int value, Player* by, int kill_type, const Vec3* source) {
        p.hp = std::max(0, std::min(100, value));
        if (p.hp <= 0) {
            kill(p, by, kill_type);
            return;
        }
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::SetHP));
        bw.write_u8(static_cast<uint8_t>(p.hp));
        bw.write_u8(kill_type == kFallKill ? 0 : 1);
        Vec3 src{0, 0, 0};
        if (source) src = *source;
        else if (by && by != &p) src = by->body.pos;
        bw.write_vec(src);
        send(p, bw);
    }

    void hit(Player& victim, int amount, Player* by, int kill_type) {
        if (!victim.alive) return;
        if (by && by != &victim && by->team == victim.team) return;
        set_hp(victim, victim.hp - amount, by, kill_type, nullptr);
    }

    // --- the range's targets ----------------------------------------------
    // The server's own players, with no connection behind them: they walk a
    // line and fall over, which needs no AI.
    void add_targets() {
        for (int lane = 0; lane < training::kLaneCount; ++lane) {
            int id = -1;
            for (int i = 0; i < kSlots && id < 0; ++i)
                if (!players[i]) id = i;
            if (id < 0) return;
            auto p = std::make_unique<Player>();
            p->id = id;
            p->named = true;
            p->name = "Target " + std::to_string(lane + 1);
            p->team = kTargets;
            p->lane = lane;
            p->client_info = true; // nobody to send a handshake to
            players[id] = std::move(p);
            spawn(*players[id]);
        }
    }
    // Across the corridor and back: face the end it is going to and walk.
    void steer_target(Player& p) {
        auto end = [&] { return p.to_far ? training::kLaneY1 : training::kLaneY0; };
        if (std::fabs(p.body.pos.y - end()) < 0.75f) p.to_far = !p.to_far;
        p.body.ori = Vec3{0.0f, end() > p.body.pos.y ? 1.0f : -1.0f, 0.0f};
        p.input = physics::Input{};
        p.input.mf = true;
    }

    // --- the world --------------------------------------------------------
    void world_tick() {
        const float dt = static_cast<float>(kTick);
        for (auto& pp : players) {
            if (!pp) continue;
            Player& p = *pp;
            if (p.gun.update(world_time)) {
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::WeaponReload));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(std::max(0, p.gun.ammo)));
                bw.write_u8(static_cast<uint8_t>(std::max(0, p.gun.stock)));
                send(p, bw);
            }
            if (!p.alive) {
                if (p.respawn_at >= 0 && world_time >= p.respawn_at) spawn(p);
                continue;
            }
            if (!p.playing()) continue;
            if (p.lane >= 0) steer_target(p);
            p.input.holding_gun = p.tool == kToolWeapon;
            p.input.secondary_fire = p.secondary;
            const physics::StepResult r = physics::step(phys_map, p.body, p.input, dt);
            p.input.jump = false;
            if (r.fall_damage > 0) set_hp(p, p.hp - r.fall_damage, nullptr, kFallKill, nullptr);
            if (!p.alive) continue;
            if (p.body.pos.z > 64.0f || !finite3(p.body.pos)) kill(p, nullptr, kFallKill);
        }
        for (size_t i = 0; i < grenades.size();) {
            if (world_time >= grenades[i].explode_at) {
                const Grenade g = grenades[i];
                grenades.erase(grenades.begin() + static_cast<long>(i));
                explode(g);
            } else {
                ++i;
            }
        }
    }

    void explode(const Grenade& g) {
        Player* owner = players[g.owner].get();
        if (!owner || !owner->named || owner->team != g.team || owner->spectator()) return;
        const Vec3& pos = g.pos;
        if (pos.x < 0 || pos.x > 512 || pos.y < 0 || pos.y > 512 || pos.z < 0 || pos.z > 63) return;
        for (auto& pp : players) {
            if (!pp || !pp->alive) continue;
            if (pp.get() != owner && pp->team == owner->team) continue;
            const float dmg = nade::blast_damage(&phys_map, pos, pp->body.pos);
            if (static_cast<int>(dmg) <= 0) continue;
            set_hp(*pp, pp->hp - static_cast<int>(dmg), owner, kGrenadeKill, &pos);
        }
        const int x = static_cast<int>(std::floor(pos.x));
        const int y = static_cast<int>(std::floor(pos.y));
        const int z = static_cast<int>(std::floor(pos.z));
        std::vector<Cell> removed;
        for (int nx = x - 1; nx <= x + 1; ++nx)
            for (int ny = y - 1; ny <= y + 1; ++ny)
                for (int nz = z - 1; nz <= z + 1; ++nz)
                    map.destroy_point(nx, ny, nz, &removed);
        touched(removed, false);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
        bw.write_u8(static_cast<uint8_t>(owner->id));
        bw.write_u8(kGrenadeDestroy);
        bw.write_i32(x);
        bw.write_i32(y);
        bw.write_i32(z);
        broadcast(bw, true);
    }

    void world_update() {
        int highest = -1;
        for (int i = 0; i < kSlots; ++i)
            if (players[i] && players[i]->named) highest = i;
        if (highest < 0) return;
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::WorldUpdate));
        for (int i = 0; i <= highest; ++i) {
            const Player* p = players[i].get();
            if (p && p->named && p->playing()) {
                bw.write_vec(p->body.pos);
                bw.write_vec(p->body.ori);
            } else {
                bw.write_vec(Vec3{0, 0, 0});
                bw.write_vec(Vec3{0, 0, 0});
            }
        }
        ENetPacket* pkt = nullptr;
        for (auto& pp : players) {
            if (!pp || pp->loading || !pp->peer) continue;
            if (!pkt) pkt = make_packet(bw, false);
            enet_peer_send(pp->peer, 0, pkt);
        }
        if (pkt && pkt->referenceCount == 0) enet_packet_destroy(pkt);
    }

    // --- packets from clients ---------------------------------------------
    void on_packet(Player& p, const uint8_t* data, size_t len) {
        if (len < 1) return;
        ByteReader br(data + 1, len - 1);
        const double now = world_time;
        switch (static_cast<PacketId>(data[0])) {
            case PacketId::ExistingPlayer: on_join(p, br); break;
            case PacketId::VersionHandshakeResponse: {
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::VersionGet));
                send(p, bw);
            } break;
            case PacketId::VersionResponse: {
                p.client_info = true;
                // Offered: kick reasons, Teamplay, the lights (flashlights and
                // the time of day) and message types.
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::ExtensionInfo));
                bw.write_u8(5);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::ChatType));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::Teamplay));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::KickReason));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::Flashlight));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::DaytimeWeather));
                bw.write_u8(1);
                send(p, bw);
            } break;
            case PacketId::ExtensionInfo: {
                proto::ExtensionInfoMsg m;
                if (!proto::read_extension_info(br, m)) break;
                const bool tp = m.offers(proto::Extension::Teamplay);
                if (tp && !p.ext_teamplay) {
                    ByteWriter bw;
                    bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
                    bw.write_u8(0);                 // Config
                    bw.write_u8(1 | 2 | 4);         // team ESP, pings, compass
                    bw.write_f32(0.0f);             // north: toward y = 0
                    bw.write_f32(-1.0f);
                    send(p, bw);
                }
                p.ext_teamplay = tp;
                // The time of day first: a client that claimed it does not draw
                // the world until it knows whether it is night.
                if (m.offers(proto::Extension::DaytimeWeather)) {
                    ByteWriter sky;
                    proto::write_daytime_sky(sky, cfg.night ? proto::kMidnight : proto::kNoon);
                    send(p, sky);
                }
                const bool fl = m.offers(proto::Extension::Flashlight);
                if (fl && !p.ext_flashlight) {
                    // Whose lamps are lit, after the state the client is loading.
                    ByteWriter state;
                    proto::write_flashlight_state(state, lit_lamps());
                    if (p.loading) p.saved.push_back(state.data);
                    else send(p, state);
                }
                p.ext_flashlight = fl;
            } break;
            case PacketId::Flashlight: on_flashlight(p, br); break;
            case PacketId::Teamplay: on_teamplay(p, br); break;
            case PacketId::ChatMessage: on_chat(p, br); break;
            case PacketId::ChangeWeapon: {
                if (!p.named || !br.can_read(2)) break;
                br.read_u8();
                const int w = br.read_u8();
                if (w > 2) break;
                set_weapon(p, w);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::ChangeWeapon));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(w));
                broadcast(bw, true);
                kill(p, nullptr, kClassChangeKill);
            } break;
            case PacketId::ChangeTeam: {
                if (!p.named || !br.can_read(2)) break;
                br.read_u8();
                int t = static_cast<int8_t>(br.read_u8());
                t = (t == 0 || t == 1) ? kPeople : -1;
                if (t == p.team) break;
                const bool was_spectator = p.spectator();
                p.team = t;
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::ChangeTeam));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(static_cast<int8_t>(t)));
                broadcast(bw, true);
                if (was_spectator || !p.alive) schedule_respawn(p);
                else kill(p, nullptr, kTeamChangeKill);
            } break;
            default: break;
        }
        if (!p.alive) return;

        switch (static_cast<PacketId>(data[0])) {
            case PacketId::PositionData: {
                if (!br.can_read(12)) break;
                const Vec3 v = br.read_vec();
                if (!finite3(v)) break;
                // Taken as sent, at any rate: there is no internet between the
                // client and here.
                p.body.pos = v;
                p.body.eye = v;
            } break;
            case PacketId::OrientationData: {
                if (!br.can_read(12)) break;
                Vec3 o = br.read_vec();
                const float l = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z);
                if (!finite3(o) || l < 1e-4f) break;
                p.body.ori = Vec3{o.x / l, o.y / l, o.z / l};
            } break;
            case PacketId::InputData: {
                if (!br.can_read(2)) break;
                br.read_u8();
                uint8_t f = br.read_u8();
                p.input.mf = f & 1;
                p.input.mb = (f >> 1) & 1;
                p.input.ml = (f >> 2) & 1;
                p.input.mr = (f >> 3) & 1;
                bool jump = (f >> 4) & 1;
                if (jump && !(p.body.vel.z >= 0 && p.body.vel.z < 0.017f)) jump = false;
                p.input.jump = jump;
                const bool crouch = (f >> 5) & 1;
                if (crouch != p.body.crouching) physics::set_crouch(phys_map, p.body, crouch);
                p.input.crouch = crouch;
                p.input.sneak = (f >> 6) & 1;
                p.input.sprint = (f >> 7) & 1;
                if (!jump) f &= static_cast<uint8_t>(~0x10u);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::InputData));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(f);
                broadcast(bw, false, p.id, -2, false);
            } break;
            case PacketId::WeaponInput: {
                if (!br.can_read(2)) break;
                br.read_u8();
                const uint8_t f = br.read_u8();
                const bool primary = f & 1, secondary = (f >> 1) & 1;
                if (primary != p.primary && p.tool == kToolWeapon) p.gun.set_shoot(primary, now);
                if (secondary != p.secondary && secondary && p.tool == kToolBlock) {
                    p.line_start = p.body.pos;
                    p.line_started = true;
                }
                p.primary = primary;
                p.secondary = secondary;
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::WeaponInput));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(f & 3);
                broadcast(bw, false, p.id);
            } break;
            case PacketId::WeaponReload: {
                p.gun.reload(now);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::WeaponReload));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(0);
                bw.write_u8(0);
                broadcast(bw, false, p.id);
            } break;
            case PacketId::HitPacket: on_hit(p, br); break;
            case PacketId::GrenadePacket: on_grenade(p, br); break;
            case PacketId::SetTool: {
                if (!br.can_read(2) || !p.named) break;
                br.read_u8();
                const int tool = br.read_u8();
                if (tool > 3) break;
                if (p.tool == kToolWeapon) p.gun.set_shoot(false, now);
                p.tool = tool;
                if (tool == kToolWeapon) p.gun.set_shoot(p.primary, now);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::SetTool));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(tool));
                broadcast(bw, true, p.id);
            } break;
            case PacketId::SetColor: {
                if (!br.can_read(4) || !p.named) break;
                br.read_u8();
                const uint32_t b = br.read_u8(), g = br.read_u8(), r = br.read_u8();
                p.color = b | (g << 8) | (r << 16);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::SetColor));
                bw.write_u8(static_cast<uint8_t>(p.id));
                write_color(bw, p.color);
                broadcast(bw, true, p.id);
            } break;
            case PacketId::BlockAction: on_block_action(p, br); break;
            case PacketId::BlockLine: on_block_line(p, br); break;
            default: break;
        }
    }

    void on_join(Player& p, ByteReader& br) {
        if (p.team == 0 || p.team == 1) return;   // only from limbo or spectating
        if (!br.can_read(3)) return;
        br.read_u8();
        const int team = static_cast<int8_t>(br.read_u8());
        const int weapon = br.read_u8();
        std::string name;
        if (br.can_read(1)) {
            br.read_u8();                         // tool
            if (br.can_read(7)) {
                br.read_u32();                    // kills
                br.read_u8(); br.read_u8(); br.read_u8();
                name = br.read_cstring();
            }
        }
        p.team = (team == 0 || team == 1) ? kPeople : -1;
        if (!p.named) {
            name = sanitise_name(name);
            if (name.empty()) name = "Deuce";
            std::string candidate = name;
            for (int i = 1;; ++i) {
                bool taken = false;
                for (auto& o : players) {
                    if (!o || !o->named || o.get() == &p) continue;
                    std::string a = o->name, b = candidate;
                    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                    if (a == b) { taken = true; break; }
                }
                if (!taken) break;
                candidate = name + std::to_string(i);
            }
            p.name = candidate;
            p.named = true;
            log("%s joined", p.name.c_str());
        }
        set_weapon(p, weapon > 2 ? 0 : weapon);
        // The dead are announced to a newcomer as fallen, or it draws them
        // standing about.
        for (auto& o : players) {
            if (!o || o.get() == &p || !o->named || o->alive || o->spectator()) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::KillAction));
            bw.write_u8(static_cast<uint8_t>(o->id));
            bw.write_u8(static_cast<uint8_t>(o->id));
            bw.write_u8(kFallKill);
            bw.write_u8(0);
            send(p, bw);
        }
        spawn(p);
    }

    void on_hit(Player& p, ByteReader& br) {
        if (!br.can_read(2)) return;
        const int target = br.read_u8();
        const int value = br.read_u8();
        if (target >= kSlots || !players[target] || target == p.id) return;
        Player& victim = *players[target];
        if (!victim.alive || value > kMelee) return;
        const bool melee = value == kMelee;
        int amount;
        if (melee) {
            if (p.tool != kToolSpade && p.tool != kToolWeapon) return;
            if (!near3(p.body.pos, victim.body.pos, kMeleeDistance)) return;
            amount = kMeleeDamage;
        } else {
            if (p.tool != kToolWeapon || p.gun.is_empty(world_time)) return;
            amount = p.gun.spec().damage[value];
        }
        const int kill_type = melee ? kMeleeKill : value == kHead ? kHeadshotKill : kWeaponKill;
        hit(victim, amount, &p, kill_type);
    }

    void on_grenade(Player& p, ByteReader& br) {
        if (!br.can_read(29)) return;
        br.read_u8();
        float fuse = br.read_f32();
        Vec3 pos = br.read_vec();
        Vec3 vel = br.read_vec();
        if (!std::isfinite(fuse) || !finite3(pos) || !finite3(vel)) return;
        if (p.grenades <= 0) return;
        p.grenades -= 1;
        if (!near3(pos, p.body.pos, 10.0f)) pos = p.body.pos;
        fuse = std::min(fuse, 3.0f);
        Vec3 rel{vel.x - p.body.vel.x, vel.y - p.body.vel.y, vel.z - p.body.vel.z};
        const float l = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
        if (l > 2.0f) {
            rel = Vec3{rel.x / l * 2.0f, rel.y / l * 2.0f, rel.z / l * 2.0f};
            vel = Vec3{rel.x + p.body.vel.x, rel.y + p.body.vel.y, rel.z + p.body.vel.z};
        }
        // Where it goes off is known now: the same flight every client
        // simulates. The map may change under it in three seconds; pyspades'
        // moving object would see that and this does not, which costs a
        // grenade landing on a block somebody dug out mid-flight.
        const nade::Impact at = nade::simulate(&phys_map, pos, vel, std::max(fuse, 0.0f));
        grenades.push_back(Grenade{p.id, p.team, at.pos, world_time + std::max(fuse, 0.0f)});
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::GrenadePacket));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_f32(fuse);
        bw.write_vec(pos);
        bw.write_vec(vel);
        broadcast(bw, false, p.id);
    }

    void on_block_action(Player& p, ByteReader& br) {
        if (!br.can_read(14)) return;
        br.read_u8();
        const int value = br.read_u8();
        const proto::IVec3 c = br.read_ivec();
        const int x = c.x, y = c.y, z = c.z;
        if (value == kGrenadeDestroy || value > kGrenadeDestroy) return;
        if (z >= 62 || !VoxelMap::valid(x, y, z)) return;
        const Vec3& pos = p.body.pos;
        auto within = [&](float d) {
            return std::fabs(pos.x - x) < d && std::fabs(pos.y - y) < d && std::fabs(pos.z - z) < d;
        };
        if (value == kBuildBlock) {
            p.blocks -= 1;
            if (p.blocks < -kBuildTolerance || !within(kMaxBlockDistance)) return;
            if (!map.build_point(x, y, z, p.color | 0x7F000000u)) return;
            touched({Cell{x, y, z}}, true);
        } else {
            if (p.tool == kToolWeapon && p.gun.is_empty(world_time)) return;
            if (!map.solid(x, y, z)) return;
            if (p.tool == kToolSpade && !within(kMaxDigDistance)) return;
            std::vector<Cell> removed;
            if (value == kDestroyBlock) {
                if (map.destroy_point(x, y, z, &removed)) p.blocks = std::min(50, p.blocks + 1);
            } else {
                map.destroy_point(x, y, z, &removed);
                map.destroy_point(x, y, z + 1, &removed);
                map.destroy_point(x, y, z - 1, &removed);
            }
            touched(removed, false);
        }
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(value));
        bw.write_i32(x);
        bw.write_i32(y);
        bw.write_i32(z);
        broadcast(bw, true);
    }

    void on_block_line(Player& p, ByteReader& br) {
        if (!br.can_read(25) || !p.line_started) return;
        br.read_u8();
        const proto::IVec3 a = br.read_ivec();
        const proto::IVec3 b = br.read_ivec();
        if (!VoxelMap::valid(a.x, a.y, a.z) || !VoxelMap::valid(b.x, b.y, b.z)) return;
        auto within = [](const Vec3& v, const proto::IVec3& c) {
            return std::fabs(v.x - c.x) < kMaxBlockDistance && std::fabs(v.y - c.y) < kMaxBlockDistance &&
                   std::fabs(v.z - c.z) < kMaxBlockDistance;
        };
        if (!within(p.body.pos, b) || !within(p.line_start, a)) return;
        if (!map.has_neighbors(a.x, a.y, a.z) || !map.has_neighbors(b.x, b.y, b.z)) return;
        const std::vector<Cell> pts = cube_line(a.x, a.y, a.z, b.x, b.y, b.z);
        if (pts.empty() || static_cast<int>(pts.size()) > p.blocks + kBuildTolerance) return;
        std::vector<Cell> built;
        for (const Cell& c : pts) {
            if (map.solid(c.x, c.y, c.z)) continue;
            if (!map.build_point(c.x, c.y, c.z, p.color | 0x7F000000u)) break;
            built.push_back(c);
        }
        touched(built, true);
        p.blocks -= static_cast<int>(pts.size());
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockLine));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_ivec(a.x, a.y, a.z);
        bw.write_ivec(b.x, b.y, b.z);
        broadcast(bw, true);
    }

    void on_chat(Player& p, ByteReader& br) {
        if (!p.named || !br.can_read(2)) return;
        br.read_u8();
        const int type = br.read_u8();
        std::string text;
        while (br.can_read(1)) {
            const char ch = static_cast<char>(br.read_u8());
            if (ch == 0) break;
            text.push_back(ch == '\r' || ch == '\n' ? ' ' : ch);
        }
        if (text.size() > 108) text.resize(108);
        if (!text.empty() && text[0] == '/') {
            if (text == "/kill" && p.alive) kill(p, nullptr, kFallKill);
            else system_chat("Commands here: /kill", p.id);
            return;
        }
        const bool all = type == static_cast<int>(proto::ChatType::All);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::ChatMessage));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(all ? 0 : 1);
        bw.write_cstring(text);
        broadcast(bw, false, -1, all ? -2 : p.team);
    }

    // --- lamps (Flashlight, extension 0x32) --------------------------------
    std::vector<bool> lit_lamps() const {
        std::vector<bool> lit(kSlots, false);
        for (const auto& pp : players)
            if (pp && pp->lamp) lit[static_cast<size_t>(pp->id)] = true;
        return lit;
    }
    // To everybody who claimed the extension, the switcher too: that echo is
    // what lights it. Saved for a client still loading, which applies it once
    // its world exists.
    void send_lamp(const Player& owner) {
        ByteWriter bw;
        proto::write_flashlight_light(bw, static_cast<uint8_t>(owner.id), owner.lamp);
        for (auto& pp : players) {
            if (!pp || !pp->ext_flashlight) continue;
            if (pp->loading) pp->saved.push_back(bw.data);
            else send(*pp, bw);
        }
    }
    void on_flashlight(Player& p, ByteReader& br) {
        proto::FlashlightMsg m;
        if (!p.ext_flashlight || !proto::read_flashlight(br, m)) return;
        if (m.sub != proto::FlashlightSub::Light) return;   // the rest are ours to send
        // The dead and the spectators carry no lamp; Kill Action puts one out.
        if (!p.alive || p.spectator()) return;
        if (p.lamp == m.on) return;
        p.lamp = m.on;
        send_lamp(p);
    }

    // Teamplay ping (sub 1), client to server: relayed to every teammate that
    // claimed the extension, the sender filled in by us.
    void on_teamplay(Player& p, ByteReader& br) {
        if (!br.can_read(1) || br.read_u8() != 1) return;
        if (!p.alive || !p.playing()) return;
        if (!br.can_read(21)) return;
        br.read_u8();                 // sender: ours to fill
        const Vec3 at = br.read_vec();
        br.read_f32();
        br.read_u8();
        br.read_u8(); br.read_u8(); br.read_u8();
        br.read_u8();
        std::string reason;
        while (br.can_read(1) && reason.size() < proto::kTeamplayReasonMax)
            reason.push_back(static_cast<char>(br.read_u8()));
        if (!finite3(at) || at.x < 0 || at.x > 512 || at.y < 0 || at.y > 512 || at.z < 0 || at.z > 64) return;
        if (world_time - p.last_ping < kPingInterval) return;
        p.last_ping = world_time;
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
        bw.write_u8(1);
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_vec(at);
        bw.write_f32(kPingDuration);
        bw.write_u8(0);
        write_color(bw, p.team == 0 ? 0x3070FF : 0x30E040);
        bw.write_u8(0);
        for (char ch : reason) bw.write_u8(static_cast<uint8_t>(ch));
        for (auto& o : players) {
            if (!o || o->loading || !o->ext_teamplay || o->team != p.team) continue;
            send(*o, bw);
        }
    }

    // --- the loop ---------------------------------------------------------
    void pump(int wait_ms) {
        ENetEvent ev;
        auto take = [&]() {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT: on_connect(ev.peer, ev.data); break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    if (ev.peer->data) on_disconnect(*static_cast<Player*>(ev.peer->data));
                    ev.peer->data = nullptr;
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (ev.peer->data)
                        on_packet(*static_cast<Player*>(ev.peer->data), ev.packet->data, ev.packet->dataLength);
                    enet_packet_destroy(ev.packet);
                    break;
                default: break;
            }
        };
        // Read until the socket is empty: ENet reads at most 256 datagrams a
        // service, and a full read means there may be more.
        uint32_t read_before = host->totalReceivedPackets;
        int r = enet_host_service(host, &ev, static_cast<enet_uint32>(std::max(wait_ms, 0)));
        for (int round = 1;; ++round) {
            while (r > 0) {
                take();
                r = enet_host_check_events(host, &ev);
            }
            if (r < 0 || host->totalReceivedPackets - read_before < 256 || round >= 16) break;
            read_before = host->totalReceivedPackets;
            r = enet_host_service(host, &ev, 0);
        }
        serve_maps();
        chunk_budget = 24;
        for (auto& pp : players)
            if (pp && pp->loading) continue_map(*pp);
        const double t = now_sec();
        int ticks = 0;
        while (t - world_time > kTick && ticks < 30) {
            world_time += kTick;
            world_tick();
            ++ticks;
        }
        if (t - world_time > 1.0) world_time = t;   // a stall: do not try to catch up
        const double period = 1.0 / std::max(1, cfg.update_rate);
        if (t >= next_network) {
            // Behind by more than a period (a stall): start again from now.
            next_network = t - next_network > period ? t + period : next_network + period;
            world_update();
        }
        enet_host_flush(host);
    }
};

// ---------------------------------------------------------------------------

Server::Server(const HostConfig& cfg) : impl_(std::make_unique<Impl>()) { impl_->cfg = cfg; }

Server::~Server() {
    if (impl_->host) {
        for (auto& p : impl_->players)
            if (p && p->peer) enet_peer_disconnect_now(p->peer, 5);
        enet_host_destroy(impl_->host);
    }
    if (impl_->map_job.joinable()) impl_->map_job.join();
    if (impl_->enet_up) enet_deinitialize();
}

bool Server::start(std::string& err) {
    Impl& s = *impl_;
    // WSAStartup on Windows, nothing elsewhere. It counts, so the server's own
    // call and the game's do not undo each other.
    if (enet_initialize() != 0) { err = "cannot start networking"; return false; }
    s.enet_up = true;
    s.load_map();
    s.add_targets();
    s.map_cache = std::make_shared<const std::vector<uint8_t>>(s.map.save_compressed());
    s.cache_rev = s.map_rev;
    ENetAddress addr;
    enet_address_set_host(&addr, "127.0.0.1");
    addr.port = s.cfg.port;
    s.host = enet_host_create(&addr, s.cfg.max_players + 2, 1, 0, 0);
    if (!s.host) {
        err = "cannot listen on 127.0.0.1:" + std::to_string(s.cfg.port) + " (is something already using it?)";
        return false;
    }
    // The range coder on the way in only. Clients compress what they send and
    // flag it, so it has to be decoded; what the server sends goes plain,
    // which every ENet peer reads, and over loopback the coder saves nothing.
    {
        ENetCompressor inbound{};
        inbound.context = enet_range_coder_create();
        inbound.decompress = enet_range_coder_decompress;
        inbound.destroy = enet_range_coder_destroy;
        if (inbound.context) enet_host_compress(s.host, &inbound);
        else enet_host_compress_with_range_coder(s.host);
    }
    s.world_time = now_sec();
    s.next_network = s.world_time;
    s.log("training course on 127.0.0.1:%u, %d world updates a second",
          static_cast<unsigned>(s.cfg.port), s.cfg.update_rate);
    return true;
}

void Server::pump(int wait_ms) { impl_->pump(wait_ms); }

void Server::run(const std::atomic<bool>& stop) {
    // A fixed cycle: take everything that has arrived, run the world, send
    // once. On Windows a sleep lasts a multiple of the system timer, 15.6ms
    // unless somebody asks for finer, which would halve the world updates:
    // this asks for 1ms for as long as the server runs.
#ifdef _WIN32
    timeBeginPeriod(1);
    struct EndPeriod { ~EndPeriod() { timeEndPeriod(1); } } end_period;
#endif
    const double cycle = 1.0 / std::max(120, impl_->cfg.update_rate);
    auto next = std::chrono::steady_clock::now();
    const auto step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(cycle));
    while (!stop.load(std::memory_order_relaxed)) {
        impl_->pump(0);
        next += step;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;   // behind: do not try to catch up
        else std::this_thread::sleep_until(next);
    }
}

int Server::player_count() const {
    int n = 0;
    for (auto& p : impl_->players)
        if (p && p->named) ++n;
    return n;
}
int Server::player_hp(int id) const {
    if (id < 0 || id >= kSlots || !impl_->players[id] || !impl_->players[id]->alive) return -1;
    return impl_->players[id]->hp;
}
bool Server::player_alive(int id) const { return player_hp(id) > 0; }
int Server::player_team(int id) const {
    if (id < 0 || id >= kSlots || !impl_->players[id]) return -2;
    return impl_->players[id]->team;
}
bool Server::player_position(int id, float& x, float& y, float& z) const {
    if (id < 0 || id >= kSlots || !impl_->players[id] || !impl_->players[id]->alive) return false;
    const Vec3& p = impl_->players[id]->body.pos;
    x = p.x, y = p.y, z = p.z;
    return true;
}
bool Server::map_solid(int x, int y, int z) const {
    return VoxelMap::valid(x, y, z) && impl_->map.solid(x, y, z);
}

}  // namespace host
