#include "Protocol.h"
#include "Vec3.h"
#include <cmath>
#include <cstring>

namespace proto {

void ByteWriter::write_u8(uint8_t v) { data.push_back(v); }
void ByteWriter::write_i8(int8_t v) { data.push_back(static_cast<uint8_t>(v)); }

void ByteWriter::write_u16(uint16_t v) {
    data.push_back(static_cast<uint8_t>(v & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void ByteWriter::write_u32(uint32_t v) {
    data.push_back(static_cast<uint8_t>(v & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
// Block coordinates are signed - lsd reads them as ivec3p and the protocol has
// always carried them that way. Reading them unsigned and casting worked only
// because nothing in a valid map is negative.
void ByteWriter::write_i32(int32_t v) {
    write_u32(static_cast<uint32_t>(v));
}


void ByteWriter::write_f32(float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(float));
    write_u32(u);
}
void ByteWriter::write_vec(const Vec3& v) {
    write_f32(v.x);
    write_f32(v.y);
    write_f32(v.z);
}
void ByteWriter::write_ivec(int x, int y, int z) {
    write_i32(x);
    write_i32(y);
    write_i32(z);
}


void ByteWriter::write_bytes(const uint8_t* buf, size_t len) {
    data.insert(data.end(), buf, buf + len);
}

void ByteWriter::write_fixed_string(const std::string& s, size_t len) {
    size_t n = s.size() < len ? s.size() : len;
    for (size_t i = 0; i < n; ++i) {
        data.push_back(static_cast<uint8_t>(s[i]));
    }
    for (size_t i = n; i < len; ++i) {
        data.push_back(0);
    }
}

void ByteWriter::write_cstring(const std::string& s) {
    for (char c : s) {
        data.push_back(static_cast<uint8_t>(c));
    }
    data.push_back(0);
}

ByteReader::ByteReader(const uint8_t* d, size_t s) : data(d), size(s), offset(0) {}

bool ByteReader::can_read(size_t n) const { return offset + n <= size; }

uint8_t ByteReader::read_u8() {
    if (!can_read(1)) return 0;
    return data[offset++];
}

int8_t ByteReader::read_i8() {
    return static_cast<int8_t>(read_u8());
}

uint16_t ByteReader::read_u16() {
    if (!can_read(2)) return 0;
    uint16_t v = data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return v;
}

uint32_t ByteReader::read_u32() {
    if (!can_read(4)) return 0;
    uint32_t v = static_cast<uint32_t>(data[offset]) |
                 (static_cast<uint32_t>(data[offset + 1]) << 8) |
                 (static_cast<uint32_t>(data[offset + 2]) << 16) |
                 (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return v;
}
int32_t ByteReader::read_i32() {
    return static_cast<int32_t>(read_u32());
}


float ByteReader::read_f32() {
    uint32_t u = read_u32();
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}
Vec3 ByteReader::read_vec() {
    // Left to right, which is not guaranteed inside a braced initialiser for
    // three separate calls - hence the temporaries.
    const float x = read_f32();
    const float y = read_f32();
    const float z = read_f32();
    return Vec3{x, y, z};
}


IVec3 ByteReader::read_ivec() {
    // Same reason for the temporaries as read_vec().
    const int32_t x = read_i32();
    const int32_t y = read_i32();
    const int32_t z = read_i32();
    return IVec3{x, y, z};
}


std::string ByteReader::read_fixed_string(size_t len) {
    if (!can_read(len)) return "";
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[offset++]);
        if (c == '\0') {
            // consume remaining but stop string
            for (size_t j = i + 1; j < len; ++j) {
                offset++;
            }
            break;
        }
        s.push_back(c);
    }
    return s;
}

std::string ByteReader::read_cstring() {
    std::string s;
    while (offset < size) {
        char c = static_cast<char>(data[offset++]);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

const uint8_t* ByteReader::read_bytes(size_t len) {
    if (!can_read(len)) return nullptr;
    const uint8_t* p = data + offset;
    offset += len;
    return p;
}

uint8_t ExtensionInfoMsg::version_of(Extension e) const {
    for (const Entry& x : entries) {
        if (x.id == static_cast<uint8_t>(e)) return x.version;
    }
    return 0;
}

bool read_extension_info(ByteReader& br, ExtensionInfoMsg& out) {
    out.entries.clear();
    if (!br.can_read(1)) return false;
    const uint8_t count = br.read_u8();
    for (uint8_t i = 0; i < count; ++i) {
        // A count larger than the packet is not a reason to throw away the
        // entries that did arrive: a truncated list simply claims less.
        if (!br.can_read(2)) break;
        ExtensionInfoMsg::Entry e;
        e.id = br.read_u8();
        e.version = br.read_u8();
        out.entries.push_back(e);
    }
    return true;
}

bool read_flashlight(ByteReader& br, FlashlightMsg& out) {
    if (!br.can_read(1)) return false;
    const uint8_t sub = br.read_u8();
    switch (sub) {
        case static_cast<uint8_t>(FlashlightSub::Light):
            if (!br.can_read(2)) return false;
            out.sub = FlashlightSub::Light;
            out.player = br.read_u8();
            out.on = br.read_u8() != 0;
            return true;
        case static_cast<uint8_t>(FlashlightSub::LightState):
            out.sub = FlashlightSub::LightState;
            out.state.clear();
            while (br.can_read(1)) out.state.push_back(br.read_u8());
            return true;
        default:
            return false;
    }
}

void write_flashlight_light(ByteWriter& bw, uint8_t player, bool on) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Flashlight));
    bw.write_u8(static_cast<uint8_t>(FlashlightSub::Light));
    bw.write_u8(player);
    bw.write_u8(on ? 1 : 0);
}

void write_flashlight_state(ByteWriter& bw, const std::vector<bool>& lit) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Flashlight));
    bw.write_u8(static_cast<uint8_t>(FlashlightSub::LightState));
    // ZeroSpades reads one bit per player slot it has, 256; send them all.
    for (size_t byte = 0; byte < 32; ++byte) {
        uint8_t v = 0;
        for (size_t bit = 0; bit < 8; ++bit) {
            const size_t id = byte * 8 + bit;
            if (id < lit.size() && lit[id]) v = static_cast<uint8_t>(v | (1u << bit));
        }
        bw.write_u8(v);
    }
}

void write_daytime_sky(ByteWriter& bw, uint16_t time) {
    bw.write_u8(static_cast<uint8_t>(PacketId::DaytimeWeather));
    bw.write_u8(0);                        // Sky
    bw.write_u8(static_cast<uint8_t>(time & 0xff));
    bw.write_u8(static_cast<uint8_t>(time >> 8));
    bw.write_u8(0); bw.write_u8(0);        // reserved
    bw.write_u8(0); bw.write_u8(0);        // weather: none in version 1
}

} // namespace proto
