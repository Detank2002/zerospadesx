#pragma once
#include "Vec3.h"

class SolidMap;

// ---------------------------------------------------------------------------
// The player physics, as the game actually runs it
//
// This is a transcription of pyspades' world_c.cpp - clipbox, try_uncrouch,
// boxclipmove, move_player, reposition_player - which is itself a port of
// voxlap's world.c, which is the client. Server and client run the same
// arithmetic, so there is one model here and not two.
//
// The numbers are not tuned, fitted or estimated. Where one looks arbitrary it
// is because it IS arbitrary: 0.36 for a jump, 0.24 before a landing slows you,
// 4096 to turn a velocity into hit points. A client that rounds any of them off
// moves differently from everybody else on the field.
//
// The training course's server runs it for every player, to know where they
// are between their position reports and what a fall does to them, and to
// walk the range's targets.
//
// Coordinates
// -----------
// Game coordinates: x and y across the map, z DOWNWARD. A falling player's z
// increases and `vel.z > 0` means descending. Every offset below reads
// backwards until you hold that in your head.
//
// `pos` is what goes in PositionData. The body hangs off it: head at
// pos.z - 1.35, feet at pos.z + 2.25, footprint 0.9 x 0.9. The probe ladder in
// boxclipmove steps 1.35, 0.45, -0.45, -1.35 off the hip, which is why every
// offset in this file is a multiple of 0.45, and why a body probe that is not
// one is measuring something the game does not.
//
// Water
// -----
// There is no water here, because there is no water in the game. A sea is a
// column with nothing in it, and clipbox has no case for one: a player who
// walks into it sinks until his feet stop, and then wades. Two lines do all of
// it:
//
//     if (sz == 63) sz = 62;      /* the bedrock layer answers for the one
//                                    below it */
//     else if (sz >= 64) return 1;
//
// which is why a body in open sea rests at pos.z = 61.7496 (feet just short of
// 64) and one standing on a sea floor rests at 60.6458 (feet in voxel 62).
// ---------------------------------------------------------------------------

namespace physics {

// Landing softer than this costs nothing. Above it you lose half your
// horizontal speed; above kFallDamageVelocity you lose health with it.
constexpr float kFallSlowDown       = 0.24f;
constexpr float kFallDamageVelocity = 0.58f;
constexpr float kFallDamageScalar   = 4096.0f;

// The line where a body is in the water rather than on the ground, and - on
// any server running piqueserver's map_extensions script - the line where the
// water starts killing. That script is not subtle about it:
//
//     if ('water_damage' in extensions and
//             self.world_object.position.z >= 61):
//         self.environment_hit(extensions['water_damage'])
//
// and push sets water_damage to 100, i.e. instant death on the first position
// update at or below it. It is a HEIGHT and not a test for water, which is
// why this is a physics constant and not a map query - but the two agree: a
// body resting in open sea sits at 61.7496 and one standing on a sea floor
// with a block under it at 62 sits at 60.6458 (see the note above), so the
// line falls exactly between "swimming" and "paddling".
constexpr float kWadeZ = 61.0f;

// What a drop of this many blocks would cost a player who lands on ground.
//
// Measured with this file's own simulation rather than guessed, because the
// number is not intuitive: nothing at all up to 9 blocks, 13 at twelve, and
// 100 - death from full health - only at NINETEEN. It is the honest way to
// ask "could that fall have killed me", which is the question that decides
// whether a death at the bottom of a cliff was the fall or the water.
int fall_damage_for_drop(float blocks);

// Straight up, once. Negative because z grows downward.
constexpr float kJumpVelocity = -0.36f;

// Velocity is in blocks per 1/32 second, which is the unit voxlap keeps it in,
// so everything that turns it into a distance multiplies by 32 * dt.
constexpr float kVelocityToBlocks = 32.0f;

// Half the footprint, and the spacing of the body probes - two half-widths.
constexpr float kHalfWidth = 0.45f;
constexpr float kProbeStep = 0.9f;

// What the player is holding this tick. The first eight are the InputData
// bits. `secondary_fire` is not one of them - it arrives in WeaponInput - but
// the physics reads it, because aiming down the sights slows you exactly like
// sneaking, so it belongs here.
struct Input {
    bool mf = false, mb = false, ml = false, mr = false;
    bool jump = false, crouch = false, sneak = false, sprint = false;
    bool secondary_fire = false;
    // Only the rifle's sights slow you down: the test is
    // `secondary_fire && weapon == WEAPON_PRIMARY`, and WEAPON_PRIMARY is 1,
    // which in the wire enum is the SMG... because pyspades' own constant is
    // misnamed. It means "is the tool the gun", and this flag says so plainly.
    bool holding_gun = false;
};

// A player, as the physics sees one.
struct Body {
    Vec3 pos{};           // p.p - what PositionData carries
    Vec3 vel{};           // p.v - blocks per 1/32s, z downward
    Vec3 ori{1, 0, 0};    // p.f - unit facing. ori.z >= 0.5 disables climbing.

    bool crouching = false;   // the body's state, which is not the key
    bool airborne  = false;
    bool wade      = false;

    // Seconds since the last ledge climb. reposition_player() uses it to lift
    // the eye smoothly over a quarter second.
    float since_climb = 1e9f;

    // The eye, which is NOT the position. It trails pos upward for a quarter
    // second after a climb and is what the real client's reach tests measure
    // from. Kept here so nothing has to guess at it again.
    Vec3 eye{};
};

// What a step did to the body beyond moving it.
struct StepResult {
    // Hit points. move_player returns this and the CLIENT tells the server; a
    // server does not volunteer fall damage.
    int fall_damage = 0;
    // Landed hard enough for the client to halve our horizontal speed - the
    // -1 return, which is a real event and not an error.
    bool landed = false;
    // Went up a ledge rather than walking onto it.
    bool climbed = false;
};

// Is the voxel containing this point solid, as far as a player is concerned?
//
//     off the sides of the map -> yes, the map has walls
//     above the sky (z < 0)    -> no, you may be thrown out of the world
//     z in [63, 64)            -> whatever voxel 62 holds
//     z >= 64                  -> yes
//
// Takes floats and truncates, exactly as the original does. The two z cases at
// the bottom are the whole of the game's water behaviour; see the note above.
bool clip_box(const SolidMap& map, float x, float y, float z);

// Stand up, if there is room. Two ways, in the original's order: an airborne
// body simply lowers its feet, and a grounded one raises its head - which is
// the branch that moves it, by 0.9.
bool try_uncrouch(const SolidMap& map, Body& b);

// Press or release the crouch key. Crouching always takes (the body shrinks
// downward, so pos.z goes UP by 0.9); standing up may be refused by the
// ceiling, and returns false when it is, leaving the body crouched.
bool set_crouch(const SolidMap& map, Body& b, bool want);

// One step of `dt` seconds. Prefer advance().
StepResult step(const SolidMap& map, Body& b, const Input& in, float dt);

// Step the body forward by `seconds` in fixed slices.
//
// The friction here is per-step and does not compose: one 100ms step does not
// land where ten 10ms steps land, because v /= (dt*4 + 1) is not that same
// expression ten times over at a tenth of dt. So the slice is a constant that
// everyone uses rather than whatever the caller's tick happened to be - two
// simulations that disagree about the slice disagree about where the player
// is, and the one that is wrong is the one that used its frame time.
//
// Leftovers shorter than a slice are carried in `carry` to the next call
// instead of being run as a short step.
constexpr float kFixedStep = 1.0f / 60.0f;
StepResult advance(const SolidMap& map, Body& b, const Input& in,
                   float seconds, float* carry);

}  // namespace physics
