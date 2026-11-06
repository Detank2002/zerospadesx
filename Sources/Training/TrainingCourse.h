#pragma once
// The training course: one walled corridor along +x, stations in a row, each
// one a thing a new player has to do to get past it. The layout is the gate,
// not a script: the dig wall is solid to the roof, the ledge is higher than a
// jump, the tunnel is plugged. Shared by the map generator (MapGen.cpp), the
// server's spawns and targets (Server.cpp) and the client, which reads it to
// put the tip for the station the player is at in front of them.
//
// Header only and dependency free, so the client can include it as it stands.
// AoS axes: x and y across the map, z down (0 the sky, 63 the bottom).

namespace training {

// The floor's top voxel before the ledge, and how much higher it is after.
constexpr int kFloorZ = 40;
constexpr int kLedgeRise = 5;
// The corridor's inside, inclusive; its walls are one voxel outside, up to the
// roof so nobody climbs out.
constexpr int kY0 = 248, kY1 = 263;
constexpr int kRoofZ = 2;

struct Span { int x0, x1; }; // inclusive

constexpr Span kStart{40, 63};
constexpr Span kRange{64, 140};
constexpr Span kDigWall{144, 147};  // dirt, wall to wall and floor to roof
constexpr int kLedgeX = 160;         // the floor is kLedgeRise higher from here on
constexpr Span kTunnel{186, 199};    // solid but for a passage...
constexpr Span kPlug{197, 199};      // ...which these columns plug
constexpr int kPassageY0 = 255, kPassageY1 = 257; // the passage's width
constexpr int kPassageHeight = 3;
constexpr Span kFinish{205, 230};

// The player starts here, facing +x.
constexpr int kSpawnX = 48, kSpawnY = 256;

// The range's lanes: a target walks across the corridor at each x, from one
// wall to the other and back, 30, 45 and 60 blocks from the firing line.
constexpr int kLaneCount = 3;
constexpr int kLaneX[kLaneCount] = {96, 111, 126};
constexpr float kLaneY0 = kY0 + 1.5f, kLaneY1 = kY1 - 0.5f;

// The floor's top voxel at x.
constexpr int floor_z(int x) { return x >= kLedgeX ? kFloorZ - kLedgeRise : kFloorZ; }

// A voxel no other map has, so a client can tell it is on this one: the top
// of the corner column, in this colour (0xRRGGBB). On the surface because a
// .vxl keeps colours only for voxels that can be seen; the first try put it at
// the bottom of the column, and its colour never reached a client.
constexpr int kMarkerX = 0, kMarkerY = 0, kMarkerZ = kFloorZ;
constexpr unsigned kMarkerRGB = 0x7A1E55u;

// Where a player is along the course: the station whose tip they need.
enum Station { Start = 0, Range, Dig, Build, Grenade, Finish, StationCount };
constexpr Station station_at(float x) {
    return x < kRange.x0          ? Start
           : x < kDigWall.x0 - 3  ? Range
           : x < kDigWall.x1 + 3  ? Dig
           : x < kLedgeX + 4      ? Build
           : x < kPlug.x1 + 2     ? Grenade
                                  : Finish;
}

}  // namespace training
