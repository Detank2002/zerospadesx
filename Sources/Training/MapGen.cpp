#include "MapGen.h"

#include "VoxelMap.h"
#include "TrainingCourse.h"
#include "TrainingModes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace host {

namespace {

uint32_t hash3(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                 seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float lattice(int x, int y, uint32_t seed) {
    return static_cast<float>(hash3(x, y, seed) & 0xFFFFFF) / 16777215.0f * 2.0f - 1.0f;
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise, -1..1.
float noise(float x, float y, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = smooth(x - ix), fy = smooth(y - iy);
    const float a = lattice(ix, iy, seed), b = lattice(ix + 1, iy, seed);
    const float c = lattice(ix, iy + 1, seed), d = lattice(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

float fbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += noise(x, y, seed + static_cast<uint32_t>(o) * 1013u) * amp;
        norm += amp;
        amp *= 0.5f;
        x *= 2.0f;
        y *= 2.0f;
    }
    return sum / norm;
}

struct RGB { float r, g, b; };

RGB mix(RGB a, RGB b, float t) {
    t = std::min(std::max(t, 0.0f), 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

uint32_t pack(RGB c, float light, int jitter) {
    auto ch = [&](float v) {
        const int i = static_cast<int>(v * light) + jitter;
        return static_cast<uint32_t>(std::min(std::max(i, 0), 255));
    };
    // B,G,R,A in the file; 0x7F is the alpha stock maps carry.
    return ch(c.b) | (ch(c.g) << 8) | (ch(c.r) << 16) | (0x7Fu << 24);
}

bool in_team_zone(int x, int y) {
    return (x < 128 || x >= 384) && y >= 128 && y < 384;
}

}  // namespace

void generate_map(VoxelMap& map, uint32_t seed) {
    constexpr int N = VoxelMap::SX;
    // Height above the water, in blocks: 0 is open water, 1 is the beach.
    std::vector<int> height(static_cast<size_t>(N) * N, 0);
    std::vector<float> wet(static_cast<size_t>(N) * N, 0.0f);
    auto H = [&](int x, int y) -> int& {
        x = std::min(std::max(x, 0), N - 1);
        y = std::min(std::max(y, 0), N - 1);
        return height[static_cast<size_t>(y) * N + x];
    };

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            // Broad rolling land, and hills that rise out of it in places.
            float e = fbm(fx / 160.0f, fy / 160.0f, seed, 6) * 0.5f + 0.5f;
            const float hills = std::max(0.0f, fbm(fx / 90.0f, fy / 90.0f, seed ^ 0x9E3779B9u, 5));
            e += hills * hills * 1.6f;
            // Lakes: a slow field that pulls the ground under the water.
            const float lake = fbm(fx / 110.0f, fy / 110.0f, seed ^ 0x51ED270Bu, 4);
            if (lake > 0.28f) e -= (lake - 0.28f) * 3.0f;
            // An island: open water round the edge of the world.
            const float edge = static_cast<float>(std::min(std::min(x, N - 1 - x), std::min(y, N - 1 - y)));
            if (edge < 48.0f) {
                const float t = 1.0f - edge / 48.0f;
                e -= t * t * 1.2f;
            }
            // The team zones are land, whatever the noise wanted.
            if (in_team_zone(x, y)) e = std::max(e, 0.32f);
            int h = static_cast<int>(std::floor(e * 26.0f));
            if (h < 0) h = 0;
            if (h > 40) h = 40;
            H(x, y) = h;
            wet[static_cast<size_t>(y) * N + x] = lake;
        }
    }

    const RGB grass_a{70, 112, 40}, grass_b{98, 128, 52}, grass_dry{122, 128, 66};
    const RGB sand{196, 182, 128}, dirt{118, 92, 64}, rock{112, 108, 100}, water{38, 86, 128};

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const int h = H(x, y);
            const uint32_t jr = hash3(x, y, seed ^ 0xA5A5u);
            const int jitter = static_cast<int>(jr % 9) - 4;
            if (h == 0) {
                // Open water: the bottom layer only, which is what water is.
                const float d = fbm(x / 30.0f, y / 30.0f, seed ^ 7u, 2) * 0.08f + 1.0f;
                map.set(x, y, 63, pack(water, d, jitter / 2));
                continue;
            }
            const int top = 62 - (h - 1);   // the surface voxel's z
            const int slope = std::max({std::abs(H(x + 1, y) - h), std::abs(H(x - 1, y) - h),
                                        std::abs(H(x, y + 1) - h), std::abs(H(x, y - 1) - h)});
            // Light from the north west: faces toward it are brighter.
            const float light = 1.0f + 0.06f * static_cast<float>((H(x - 1, y) - H(x + 1, y)) +
                                                                 (H(x, y - 1) - H(x, y + 1)));
            const float lt = std::min(std::max(light, 0.75f), 1.25f);

            RGB surf;
            if (h <= 2) {
                surf = sand;
            } else if (slope >= 4) {
                surf = rock;
            } else if (slope >= 2) {
                surf = mix(dirt, grass_a, 0.35f);
            } else {
                const float g = fbm(x / 40.0f, y / 40.0f, seed ^ 0x77u, 3) * 0.5f + 0.5f;
                surf = mix(grass_a, grass_b, g);
                surf = mix(surf, grass_dry, static_cast<float>(h) / 40.0f);
            }
            map.set(x, y, top, pack(surf, lt, jitter));
            for (int z = top + 1; z < 64; ++z) {
                const float depth = static_cast<float>(z - top);
                const RGB under = depth < 3 && h > 2 ? mix(dirt, surf, 0.25f) : mix(dirt, rock, depth / 30.0f);
                map.set(x, y, z, pack(under, std::max(0.6f, lt - depth * 0.02f),
                                      jitter + static_cast<int>(hash3(x, z, seed) % 7) - 3));
            }
        }
    }

    // Trees, on gentle grass away from the water, and outside the band the
    // objectives and spawns are drawn from: a man spawned on a treetop, or a
    // tent in a canopy, is a bug in the map, not the game.
    const RGB trunk{92, 66, 40}, leaf_a{46, 92, 34}, leaf_b{64, 110, 40};
    for (int y = 4; y < N - 4; ++y) {
        for (int x = 4; x < N - 4; ++x) {
            if (y >= 112 && y < 400) continue;
            const uint32_t r = hash3(x, y, seed ^ 0x7EE5u);
            if (r % 700u != 0) continue;
            const int h = H(x, y);
            if (h <= 3) continue;
            const int slope = std::max({std::abs(H(x + 1, y) - h), std::abs(H(x - 1, y) - h),
                                        std::abs(H(x, y + 1) - h), std::abs(H(x, y - 1) - h)});
            if (slope >= 2) continue;
            const int ground = 62 - (h - 1);
            const int tall = 4 + static_cast<int>((r >> 8) % 3);
            for (int k = 1; k <= tall; ++k)
                map.set(x, y, ground - k, pack(trunk, 1.0f, static_cast<int>((r >> (k + 4)) % 7) - 3));
            const int cz = ground - tall - 1;
            for (int dz = -2; dz <= 2; ++dz)
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (dx * dx + dy * dy + dz * dz > 5) continue;
                        if (dx == 0 && dy == 0 && dz >= 1) continue;   // the trunk
                        const int z = cz + dz;
                        if (z < 1 || map.solid(x + dx, y + dy, z)) continue;
                        const uint32_t lr = hash3(x + dx, y + dy * 7 + z * 13, seed);
                        map.set(x + dx, y + dy, z,
                                pack(mix(leaf_a, leaf_b, static_cast<float>(lr % 100) / 100.0f),
                                     dz < 0 ? 1.1f : 0.9f, static_cast<int>(lr % 9) - 4));
                    }
        }
    }
}

void generate_training_map(VoxelMap& map) {
    using namespace training;
    constexpr int N = VoxelMap::SX;
    const RGB grass{92, 140, 60}, dirt{120, 86, 54}, stone{128, 128, 132}, wall{96, 98, 104};
    const RGB lane{220, 190, 60}, line{235, 235, 235}, plug{150, 60, 40}, gold{230, 180, 40};
    auto jitter = [](int x, int y, int z) {
        return static_cast<int>(hash3(x * 64 + z, y, 7u) % 9) - 4;
    };
    auto in_corridor = [](int y) { return y >= kY0 && y <= kY1; };

    // The ground, everywhere: grass over dirt, higher past the ledge.
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const int top = floor_z(x);
            RGB surf = grass;
            if (in_corridor(y) && x >= kRange.x0 && x <= kRange.x1) {
                for (int lx : kLaneX)
                    if (x == lx) surf = lane;       // where each target walks
                if (x == kRange.x0 + 6) surf = line; // the firing line
            }
            if (in_corridor(y) && x >= kFinish.x0 && x <= kFinish.x1) surf = gold;
            map.set(x, y, top, pack(surf, 1.0f, jitter(x, y, top)));
            for (int z = top + 1; z < 64; ++z)
                map.set(x, y, z, pack(dirt, 0.9f, jitter(x, y, z)));
        }
    }

    // The corridor's walls and end caps, up to the roof.
    for (int x = kStart.x0 - 1; x <= kFinish.x1 + 1; ++x) {
        for (int z = kRoofZ; z < floor_z(x); ++z) {
            map.set(x, kY0 - 1, z, pack(wall, 1.0f, jitter(x, kY0 - 1, z)));
            map.set(x, kY1 + 1, z, pack(wall, 1.0f, jitter(x, kY1 + 1, z)));
        }
    }
    for (int y = kY0; y <= kY1; ++y) {
        for (int z = kRoofZ; z < kFloorZ; ++z)
            map.set(kStart.x0 - 1, y, z, pack(wall, 1.0f, jitter(kStart.x0 - 1, y, z)));
        for (int z = kRoofZ; z < floor_z(kFinish.x1 + 1); ++z)
            map.set(kFinish.x1 + 1, y, z, pack(wall, 1.0f, jitter(kFinish.x1 + 1, y, z)));
    }

    // The dig wall: dirt, wall to wall and floor to roof.
    for (int x = kDigWall.x0; x <= kDigWall.x1; ++x)
        for (int y = kY0; y <= kY1; ++y)
            for (int z = kRoofZ; z < floor_z(x); ++z)
                map.set(x, y, z, pack(dirt, 1.0f, jitter(x, y, z)));

    // The ledge's face is the ground stepping up at kLedgeX, already there.

    // The tunnel: stone but for a passage, which the plug's columns close.
    for (int x = kTunnel.x0; x <= kTunnel.x1; ++x) {
        const int top = floor_z(x);
        for (int y = kY0; y <= kY1; ++y) {
            for (int z = kRoofZ; z < top; ++z) {
                const bool passage = y >= kPassageY0 && y <= kPassageY1 && z >= top - kPassageHeight;
                const bool plugged = x >= kPlug.x0 && x <= kPlug.x1;
                if (passage && !plugged) continue;
                map.set(x, y, z, pack(passage ? plug : stone, 1.0f, jitter(x, y, z)));
            }
        }
    }

    // How a client knows this map.
    map.set(kMarkerX, kMarkerY, kMarkerZ,
            ((kMarkerRGB >> 16) & 0xFFu) << 16 | ((kMarkerRGB >> 8) & 0xFFu) << 8 |
              (kMarkerRGB & 0xFFu) | (0x7Fu << 24));
}

void generate_aim_arena(VoxelMap& map, int scenario) {
    using namespace training;
    const RGB floor_a{150, 152, 156}, floor_b{136, 138, 142}, grid{110, 112, 118};
    const RGB wall{84, 88, 98}, stand{170, 120, 60}, stand_top{205, 150, 70};
    const RGB spot{40, 160, 220}, lane{220, 190, 60};
    auto jitter = [](int x, int y, int z) {
        return static_cast<int>(hash3(x * 64 + z, y, 11u) % 7) - 3;
    };
    // The floor: a pale grid every four blocks, so distance and movement read.
    for (int x = kArenaX0; x <= kArenaX1; ++x) {
        for (int y = kArenaY0; y <= kArenaY1; ++y) {
            RGB c = ((x / 4 + y / 4) % 2) ? floor_a : floor_b;
            if (x % 8 == 0 || y % 8 == 0) c = grid;
            map.set(x, y, kArenaFloorZ, pack(c, 1.0f, jitter(x, y, kArenaFloorZ)));
        }
    }
    // Where the player stands
    for (int x = kAimSpawnX - 1; x <= kAimSpawnX + 1; ++x)
        for (int y = kAimSpawnY - 1; y <= kAimSpawnY + 1; ++y)
            map.set(x, y, kArenaFloorZ, pack(spot, 1.0f, 0));
    // The walls, up to the roof, so nobody wanders off the arena.
    for (int x = kArenaX0 - 1; x <= kArenaX1 + 1; ++x) {
        for (int z = kRoofZ; z < kArenaFloorZ; ++z) {
            map.set(x, kArenaY0 - 1, z, pack(wall, 1.0f, jitter(x, kArenaY0 - 1, z)));
            map.set(x, kArenaY1 + 1, z, pack(wall, 1.0f, jitter(x, kArenaY1 + 1, z)));
        }
    }
    for (int y = kArenaY0 - 1; y <= kArenaY1 + 1; ++y) {
        for (int z = kRoofZ; z < kArenaFloorZ; ++z) {
            map.set(kArenaX0 - 1, y, z, pack(wall, 1.0f, jitter(kArenaX0 - 1, y, z)));
            map.set(kArenaX1 + 1, y, z, pack(wall, 1.0f, jitter(kArenaX1 + 1, y, z)));
        }
    }
    if (scenario == static_cast<int>(Scenario::Gridshot)) {
        // The stands: three steps, each higher than the one before.
        for (int s = 0; s < kStandCount; ++s) {
            const int top = kArenaFloorZ - kStandRise[s];
            for (int x = kStandX[s]; x < kStandX[s] + kStandDepth; ++x)
                for (int y = kStandY0; y <= kStandY1; ++y)
                    for (int z = top; z < kArenaFloorZ; ++z)
                        map.set(x, y, z, pack(z == top ? stand_top : stand, 1.0f, jitter(x, y, z)));
        }
    }
    if (scenario == static_cast<int>(Scenario::LongRange)) {
        for (int lx : kFarLaneX)
            for (int y = static_cast<int>(kFarLaneY0); y <= static_cast<int>(kFarLaneY1); ++y)
                map.set(lx, y, kArenaFloorZ, pack(lane, 1.0f, 0));
    }
}

}  // namespace host
