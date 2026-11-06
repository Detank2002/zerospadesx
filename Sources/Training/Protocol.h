#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct Vec3;

namespace proto {

constexpr uint8_t PROTOCOL_VERSION = 3; // AoS 0.75
constexpr size_t NAME_LEN = 16;

// No player. Ids are a byte and this one is reserved, which is what makes the
// server's get_anon_pid() work - so it is never a real player and never
// something to send. Code that finds it in self_id_ has got ahead of the
// StateData that hands the id out.
constexpr uint8_t kInvalidPlayerId = 255;

enum class PacketId : uint8_t {
    PositionData      = 0,
    OrientationData   = 1,
    WorldUpdate       = 2,
    InputData         = 3,
    WeaponInput       = 4,
    SetHP             = 5, // server -> client
    HitPacket         = 5, // client -> server
    GrenadePacket     = 6,
    SetTool           = 7,
    SetColor          = 8,
    ExistingPlayer    = 9,
    ShortPlayerData   = 10,
    MoveObject        = 11,
    CreatePlayer      = 12,
    BlockAction       = 13,
    BlockLine         = 14,
    StateData         = 15,
    KillAction        = 16,
    ChatMessage       = 17,
    MapStart          = 18,
    MapChunk          = 19,
    PlayerLeft        = 20,
    TerritoryCapture  = 21,
    ProgressBar       = 22,
    IntelCapture      = 23,
    IntelPickup       = 24,
    IntelDrop         = 25,
    Restock           = 26,
    FogColor          = 27,
    WeaponReload      = 28,
    ChangeTeam        = 29,
    ChangeWeapon      = 30,
    MapCached         = 31, // 0.76
    VersionHandshakeInit = 31,
    VersionHandshakeResponse = 32,
    VersionGet        = 33,
    VersionResponse   = 34,
    ExtensionInfo     = 60,
    // An extension's packets are numbered 64 + its extension id, so the
    // Teamplay extension (48, below) owns 112. Only ever sent to a connection
    // that claimed the extension; see read_teamplay_ping().
    Teamplay          = 112,
    // Flashlight (extension 0x32): who has a lit lamp. See the section below.
    Flashlight        = 114,
    // Daytime and Weather (extension 0x33): the time of day, server to client.
    DaytimeWeather    = 115,
};

// The three guns. The protocol calls this field the gun and the packets that
// carry it Weapon*, which is the protocol's own inconsistency, not ours - code
// here says gun for the thing and weapon only where it is naming a packet.
enum class Gun : uint8_t { Rifle = 0, SMG = 1, Shotgun = 2 };

// The tools, as SetTool carries them. The server puts the gun in a player's
// hands on every spawn, so that is what a round starts from.
enum class Tool : uint8_t { Spade = 0, Block = 1, Gun = 2, Grenade = 3 };

// Where a HitPacket says it landed. Torso and head are what a gun reports;
// melee takes a different path through the server - no ammunition test, no aim
// ray, just a distance check.
enum class HitType : uint8_t { Torso = 0, Head = 1, Arms = 2, Legs = 3, Melee = 4 };

// Entity ids in MoveObject and in the CTF state block. In TC every object is a
// territory numbered from zero instead, which is why the two cannot share a
// table.
enum class Entity : uint8_t { BlueFlag = 0, GreenFlag = 1, BlueBase = 2, GreenBase = 3 };

// Teams. 255 is spectator; 2 is TC's neutral, which is not a team anyone is on.
// Block coordinates. Three signed 32-bit integers, which is how BlockAction
// and BlockLine carry a position - not the three floats a Vec3 is. Reading one
// as a Vec3 is the mistake this type exists to make impossible.
struct IVec3 {
    int x, y, z;
};

constexpr uint8_t kTeamBlue = 0;
constexpr uint8_t kTeamGreen = 1;
constexpr uint8_t kTeamNeutral = 2;
constexpr uint8_t kTeamSpectator = 255;

struct ByteWriter {
    std::vector<uint8_t> data;

    // Room for most packets up front, rather than growing a byte at a time.
    ByteWriter() { data.reserve(64); }

    void write_u8(uint8_t v);
    void write_i8(int8_t v);
    void write_u16(uint16_t v);
    void write_u32(uint32_t v);
    void write_i32(int32_t v);
    void write_f32(float v);
    // Three floats, which is how every position, orientation and velocity in
    // this protocol is carried.
    void write_vec(const struct Vec3& v);
    // Three signed i32s - a block position. See IVec3.
    void write_ivec(int x, int y, int z);
    void write_bytes(const uint8_t* buf, size_t len);
    void write_fixed_string(const std::string& s, size_t len);
    void write_cstring(const std::string& s);
};

struct ByteReader {
    const uint8_t* data;
    size_t size;
    size_t offset;

    ByteReader(const uint8_t* d, size_t s);
    bool can_read(size_t n) const;
    uint8_t read_u8();
    int8_t read_i8();
    uint16_t read_u16();
    uint32_t read_u32();
    int32_t read_i32();
    float read_f32();
    struct Vec3 read_vec();
    // Twelve bytes. Bounds-check with can_read() before calling: like every
    // other reader here it returns zeroes rather than reading past the end.
    IVec3 read_ivec();
    std::string read_fixed_string(size_t len);
    std::string read_cstring();
    const uint8_t* read_bytes(size_t len);
};

// The chat types, as pyspades/constants.py numbers them.
enum class ChatType : uint8_t {
    All = 0, Team = 1, System = 2, Big = 3, Info = 4, Warning = 5, Error = 6
};

// The protocol extensions, numbered as pyspades/constants.py numbers them.
// ExtensionInfo (60) is an exchange: the server offers its list once it has
// the client's VersionResponse, and the client answers with what it claims.
enum class Extension : uint8_t {
    // Pings between teammates, relayed by the server.
    Teamplay = 48,
    // Lamps everybody sees.
    Flashlight = 0x32,
    // The time of day: a client that claimed it draws a night at midnight.
    DaytimeWeather = 0x33,
    // Player ids past 31.
    PlayerLimit = 192,
    // Chat types sent as they are, for the centre-screen lines and banners.
    ChatType = 193,
    // The reason for a kick, as a CHAT_SYSTEM message before it.
    KickReason = 194,
};

// ExtensionInfo (60), from pyspades/contained.pyx, ProtocolExtensionInfo:
//
//     count(1) then count x ( id(1) version(1) )
//
// A count that overruns the packet is a short read, not a parse failure:
// whatever entries did arrive are kept.
struct ExtensionInfoMsg {
    struct Entry { uint8_t id = 0; uint8_t version = 0; };
    std::vector<Entry> entries;

    // The version offered for `id`, or 0 if it was not offered at all.
    uint8_t version_of(Extension e) const;
    bool offers(Extension e) const { return version_of(e) != 0; }
};
bool read_extension_info(ByteReader& br, ExtensionInfoMsg& out);

// Teamplay (extension 48, packet 112): a ping's reason is capped here.
constexpr size_t kTeamplayReasonMax = 64;

// --- Flashlight (extension 0x32, packet 114) ------------------------------
//
// `[114][sub][...]`:
//
//   0  Light        C->S->C  player(1) on(1). A client sends its own switch
//                            (the player byte is ignored and filled in by the
//                            server); the server relays every granted switch
//                            to everyone who claimed the extension, the
//                            switcher included - that echo is what lights it.
//   1  Light State  S->C     a bitset to the end of the packet, bit i of byte
//                            i/8 for player i: every lamp lit, for a newcomer.
//   2  Light Config S->C     player(1) reach(1) cone(1) rgb(3); 255 is the
//                            server's default for everyone. Not read here.
enum class FlashlightSub : uint8_t { Light = 0, LightState = 1, LightConfig = 2 };

struct FlashlightMsg {
    FlashlightSub sub = FlashlightSub::Light;
    uint8_t player = kInvalidPlayerId;   // Light
    bool on = false;                     // Light
    std::vector<uint8_t> state;          // LightState's bitset
    bool lit(uint8_t id) const {         // LightState: is player `id` lit?
        return id / 8u < state.size() && ((state[id / 8u] >> (id % 8u)) & 1u) != 0;
    }
};
// After the packet id. False for a short packet or a sub this does not read
// (Light Config, and anything a later version adds).
bool read_flashlight(ByteReader& br, FlashlightMsg& out);
// The whole packet, id included: switch `player`'s lamp.
void write_flashlight_light(ByteWriter& bw, uint8_t player, bool on);
// The whole packet, id included: Light State for the ids lit in `lit`.
void write_flashlight_state(ByteWriter& bw, const std::vector<bool>& lit);

// --- Daytime and Weather (extension 0x33, packet 115) ---------------------
//
// `[115][sub][...]`. Version 1 has one sub packet, Sky (0): time(u16 LE,
// minutes since midnight) reserved(u16) weather(u16). Any time but 0 is the day.
constexpr uint16_t kMidnight = 0;
constexpr uint16_t kNoon = 720;
// The whole packet, id included.
void write_daytime_sky(ByteWriter& bw, uint16_t time);

} // namespace proto
