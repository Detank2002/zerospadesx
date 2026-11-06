#pragma once
#include <cstdint>
#include "Vec3.h"

class SolidMap;

// ---------------------------------------------------------------------------
// Grenades
//
// The one weapon in this game the server simulates on its own. A bullet is a
// claim we make and the server checks; a grenade is an OBJECT we hand over,
// and from the moment the packet lands the server owns it completely - it
// flies, bounces, and explodes on the server's clock whether we are alive to
// see it or not.
//
// That is what makes grenades worth doing carefully. There is no "the server
// refused it" to detect, no echo to wait for. Everything that decides whether
// the throw was any good happens BEFORE the packet goes out, in arithmetic we
// have to get right, because after that we are a spectator.
//
// pyspades/player.py, on_grenade_recieved, in the order it tests:
//
//     if not self.hp: return                       # dead men do not throw
//     if check_nan(value, *position) or check_nan(*velocity):
//         self.on_hack_attempt("Invalid grenade data")   # <- this is a KICK
//         return
//     if not self.grenades: return                 # the count, and nothing else
//     self.grenades -= 1
//     if not self.check_speedhack(*position):
//         position = self.world_object.position.get()    # silently relocated
//     if value > 3.0: value = 3.0                  # fuse ceiling
//     velocity = Vertex3(*velocity) - self.world_object.velocity
//     if velocity.length() > 2.0:                  # "cap at tested maximum"
//         velocity = velocity.normal() * 2.0
//     velocity += self.world_object.velocity
//     grenade = world.create_object(world.Grenade, value, position, None,
//                                   velocity, self.grenade_exploded)
//     grenade.team = self.team
//
// Nothing there limits how often a player throws but `self.grenades`, three,
// and the throw speed is capped at 2.0 relative to the player's own velocity
// (a real client throws at exactly 1.0). The server here keeps both rules.
//
// And the explosion, from grenade_exploded:
//
//     for player_list in (self.team.other.get_players(), (self,)):
//         damage = grenade.get_damage(player.world_object.position)
//
// Read that tuple twice. The blast is applied to the ENEMY TEAM and to the
// THROWER, and to nobody else. Teammates are immune in stock piqueserver -
// you cannot frag your own side, but you can very easily frag yourself. See
// blast_damage().
// ---------------------------------------------------------------------------

namespace nade {

// refill() sets grenades = 3, and that is the only thing that ever raises the
// number. Spawning refills; standing on your own tent refills.
constexpr int kMaxGrenades = 3;

// `if value > 3.0: value = 3.0`. The fuse is a float in seconds and it is what
// we choose - the whole cooking mechanic is nothing more than sending a number
// smaller than three.
constexpr float kMaxFuse = 3.0f;
// Below this a grenade is a suicide note: it goes off before it has travelled
// anywhere. The real client cannot send one this short either, because a
// person cannot release a key that fast after pressing it.
constexpr float kMinFuse = 0.35f;

// What a client actually throws at: the orientation vector, unit length. The
// server would take 2.0 (see the note above); we do not.
constexpr float kThrowSpeed = 1.0f;
constexpr float kServerSpeedCap = 2.0f;

// The world's clock. pyspades calls world.update(dt) at 60Hz and world_c's
// fsynctics is that dt, so a grenade tick is 1/60s, gravity adds fsynctics to
// v.z (+z is DOWN in this game), and position advances v * fsynctics * 32.
//
//     g->v.z += fsynctics;
//     g->p.x += g->v.x * fsynctics * 32;   (and y, z)
//
// So one velocity unit is 32 blocks/second and gravity is 32 blocks/s^2. A
// throw at 45 degrees carries v^2/g = 32 blocks, which is the whole practical
// range of the weapon and the reason nothing here tries to reach further.
constexpr float kTickDt = 1.0f / 60.0f;
constexpr float kBlocksPerUnit = 32.0f;
constexpr float kGravity = 32.0f;          // blocks/s^2, +z, i.e. downward
constexpr float kSpeedBlocks = kThrowSpeed * kBlocksPerUnit;  // 32 blocks/s
constexpr float kMaxRange = kSpeedBlocks * kSpeedBlocks / kGravity;  // 32 blocks

// move_grenade's bounce. The component that crossed a face is negated, the
// position is put BACK where it started the tick, and then ALL THREE
// components are multiplied by 0.36. A grenade loses nearly two thirds of its
// speed every time it touches anything, which is why they die where they land
// and why a lobbed one does not roll far.
constexpr float kBounce = 0.36f;

// get_damage(), verbatim:
//
//     if (fabs(diff_x) < 16 and fabs(diff_y) < 16 and fabs(diff_z) < 16 and
//         self.hit_test(player_position)):
//         value = diff_x**2 + diff_y**2 + diff_z**2
//         if value == 0.0: return 100.0
//         return 4096.0 / value
//
// Isotropic - the vertical axis is NOT weighted, whatever the folklore says -
// uncapped, and gated on a line of sight from the blast to the player. So:
//
//     4096/d^2 >= 100  ->  d <= 6.40   certain death at full health
//     4096/d^2 >=  50  ->  d <= 9.05
//     4096/d^2 >=  25  ->  d <= 12.80
//     the per-axis box                 nothing at all past 16
//
// and a wall between the two of you means nothing at any distance.
constexpr float kBlastCutoff = 16.0f;
constexpr float kBlastK = 4096.0f;
constexpr float kLethalRadius = 6.4f;

// 4096/d^2 with the line-of-sight gate, for any two points. `map` may be null,
// in which case sight is assumed - callers that care pass a map.
float blast_damage(const SolidMap* map, const Vec3& blast, const Vec3& who);

// Where a grenade thrown with `vel` from `from` actually ends up, simulated
// one 1/60s tick at a time exactly as move_grenade does it, bounces and all,
// until the fuse runs out.
//
// The server decides where a thrown grenade goes off when it is thrown:
// the same flight every client simulates.
struct Impact {
    Vec3 pos{};
    float time = 0.0f;   // when the fuse ran out, i.e. the fuse
    int bounces = 0;
};
Impact simulate(const SolidMap* map, const Vec3& from, const Vec3& vel, float fuse);

}  // namespace nade
