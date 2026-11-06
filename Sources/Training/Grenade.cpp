#include "Grenade.h"
#include "SolidMap.h"
#include <cmath>
#include <cstdlib>

namespace nade {

namespace {

// world_c.cpp clipworld(), which is what move_grenade tests - and it is NOT
// the same function the player's box uses:
//
//     long clipworld(long x, long y, long z) {
//         if (x < 0 || x >= 512 || y < 0 || y >= 512) return 0;
//         if (z < 0) return 0;
//         int sz = (int)z;
//         if (sz == 63) sz = 62;
//         else if (sz >= 63) return 1;
//         return get_solid(x, y, sz, global_map);
//     }
//
// Off the side of the map is EMPTY, not a wall: a grenade thrown at the edge
// sails out over nothing and explodes in mid-air rather than bouncing back.
// Above the map (z < 0) is empty too. Only below the floor is solid.
bool clipworld(const SolidMap* map, int x, int y, int z) {
    if (x < 0 || x >= SolidMap::SIZE_X || y < 0 || y >= SolidMap::SIZE_Y) return false;
    if (z < 0) return false;
    if (z == 63) z = 62;
    else if (z >= 63) return true;
    if (!map) return false;
    return map->solid(x, y, z);
}

inline int ifloor(float v) { return static_cast<int>(std::floor(v)); }

// world_c.cpp can_see(), which is what grenade.hit_test() calls, and NOT the
// same question AI.cpp's has_line_of_sight() answers.
//
// Two differences, and both of them matter here. has_line_of_sight is
// player-shaped: it starts at an eye and samples three heights down the
// target's body, because a man half behind a wall is still a target. A blast
// is a POINT and the server tests it against a single point - the player's
// reported position - so borrowing the player version would report damage
// through a wall that only the target's ankles could see round.
//
// The other is the coordinate convention, and it is the one that actually bit.
// can_see takes every endpoint through `ftol(x - .5f)` before it floors, so a
// point at exactly z = 40.0 is in voxel 39, not 40. A grenade that has finished
// bouncing comes to rest precisely on a block boundary - the bounce puts it
// back where it started the tick and damps the velocity, so after seventy-odd
// contacts it converges onto the floor to the last decimal - and floored
// without the shift that reads as "inside solid rock", which is a sight line
// from nowhere to nowhere. Damage came out as zero for every grenade that
// landed, which is all of them.
bool can_see(const SolidMap* map, const Vec3& a, const Vec3& b) {
    if (!map) return true;
    const float ax = a.x - 0.5f, ay = a.y - 0.5f, az = a.z - 0.5f;
    const float bx = b.x - 0.5f, by = b.y - 0.5f, bz = b.z - 0.5f;
    int x = ifloor(ax), y = ifloor(ay), z = ifloor(az);
    const int ex = ifloor(bx), ey = ifloor(by), ez = ifloor(bz);
    if (x == ex && y == ey && z == ez) return true;

    const float dx = bx - ax, dy = by - ay, dz = bz - az;
    const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    const float inf = 1e30f;
    // Parametric distance along the segment (t in 0..1) to the next boundary
    // on each axis, and the spacing between boundaries after that.
    const float tdx = (dx != 0.0f) ? std::fabs(1.0f / dx) : inf;
    const float tdy = (dy != 0.0f) ? std::fabs(1.0f / dy) : inf;
    const float tdz = (dz != 0.0f) ? std::fabs(1.0f / dz) : inf;
    float tx = (dx != 0.0f)
                   ? ((dx > 0 ? (static_cast<float>(x + 1) - ax) : (ax - static_cast<float>(x))) / std::fabs(dx))
                   : inf;
    float ty = (dy != 0.0f)
                   ? ((dy > 0 ? (static_cast<float>(y + 1) - ay) : (ay - static_cast<float>(y))) / std::fabs(dy))
                   : inf;
    float tz = (dz != 0.0f)
                   ? ((dz > 0 ? (static_cast<float>(z + 1) - az) : (az - static_cast<float>(z))) / std::fabs(dz))
                   : inf;

    // One step per voxel boundary crossed, which is exactly the server's `cnt`.
    const int budget = std::abs(ex - x) + std::abs(ey - y) + std::abs(ez - z);
    for (int i = 0; i < budget; ++i) {
        if (tx <= ty && tx <= tz)      { x += sx; tx += tdx; }
        else if (ty <= tz)             { y += sy; ty += tdy; }
        else                           { z += sz; tz += tdz; }
        // Off the edge of the world is not a wall - there is nothing there to
        // stop anything.
        if (x < 0 || x >= SolidMap::SIZE_X || y < 0 || y >= SolidMap::SIZE_Y) continue;
        if (z < 0) continue;
        if (z >= SolidMap::SIZE_Z) return false;
        if (map->solid(x, y, z)) return false;
        if (x == ex && y == ey && z == ez) break;
    }
    return true;
}

}  // namespace

float blast_damage(const SolidMap* map, const Vec3& blast, const Vec3& who) {
    const float dx = who.x - blast.x;
    const float dy = who.y - blast.y;
    const float dz = who.z - blast.z;
    // The per-axis box, tested before anything else and with a strict <. This
    // is a box and not a sphere, so a man 15 blocks along each of the three
    // axes - 26 away in a straight line - still takes damage, and one 16
    // blocks along a single axis takes none.
    if (std::fabs(dx) >= kBlastCutoff) return 0.0f;
    if (std::fabs(dy) >= kBlastCutoff) return 0.0f;
    if (std::fabs(dz) >= kBlastCutoff) return 0.0f;
    // hit_test() - a wall between the blast and the man stops all of it. This
    // is the rule that makes a grenade round a corner useless and one through
    // a doorway lethal, and why a thrower is safe behind a wall: no sight
    // line to itself, no damage to itself.
    if (!can_see(map, blast, who)) return 0.0f;
    const float v = dx * dx + dy * dy + dz * dz;
    if (v == 0.0f) return 100.0f;
    return kBlastK / v;
}

Impact simulate(const SolidMap* map, const Vec3& from, const Vec3& vel, float fuse) {
    Impact out;
    out.pos = from;
    Vec3 p = from;
    Vec3 v = vel;
    float left = fuse;
    const float f = kTickDt * kBlocksPerUnit;   // 0.5333 blocks per unit of v

    // world.pyx Grenade.update(): the fuse is decremented FIRST and an expired
    // grenade explodes where it already is without moving that tick.
    for (int guard = 0; guard < 4096; ++guard) {
        left -= kTickDt;
        if (left <= 0.0f) break;

        const Vec3 fpos = p;
        v.z += kTickDt;                 // gravity, +z is down
        p.x += v.x * f;
        p.y += v.y * f;
        p.z += v.z * f;

        const int lx = ifloor(p.x), ly = ifloor(p.y), lz = ifloor(p.z);
        if (!clipworld(map, lx, ly, lz)) continue;

        // A bounce. Which component turns around is decided by which cell
        // boundary was crossed, with a sideways test to disambiguate a corner:
        // if the intermediate cell is empty then that is the face we came
        // through.
        const int px = ifloor(fpos.x), py = ifloor(fpos.y), pz = ifloor(fpos.z);
        if (lz != pz && ((lx == px && ly == py) || !clipworld(map, lx, ly, pz))) {
            v.z = -v.z;
        } else if (lx != px && ((ly == py && lz == pz) || !clipworld(map, px, ly, lz))) {
            v.x = -v.x;
        } else if (ly != py && ((lx == px && lz == pz) || !clipworld(map, lx, py, lz))) {
            v.y = -v.y;
        }
        // The move is UNDONE - the grenade sits where it started the tick -
        // and then every component is damped, not just the one that turned.
        // Two thirds of the speed gone per contact is why grenades stay put.
        p = fpos;
        v.x *= kBounce;
        v.y *= kBounce;
        v.z *= kBounce;
        ++out.bounces;
    }

    out.pos = p;
    out.time = fuse;
    return out;
}

}  // namespace nade
