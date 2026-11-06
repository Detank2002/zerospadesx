#include "Physics.h"
#include "SolidMap.h"
#include <cmath>

namespace physics {
namespace {

// sqrt(2)/2, the diagonal scale. Written as the original writes it rather than
// as 0.7071 so the two can be compared line by line.
const float kDiagonal = std::sqrt(2.0f) / 2.0f;

// The strafe vector: the facing turned ninety degrees in the xy plane, then
// normalised. voxlap keeps it alongside the facing and recomputes both whenever
// the player turns; here it is derived on the spot, which is the same thing
// done less often.
//
// A facing that points straight up or straight down has no xy part to turn, and
// the original divides by its length regardless - producing infinities that
// then go into the velocity. It cannot happen to a real player, whose facing
// comes from a mouse, but a reported orientation can say anything, so there
// is a floor on the divisor here.
void strafe_vector(const Vec3& ori, float& sx, float& sy) {
    const float f = std::sqrt(ori.x * ori.x + ori.y * ori.y);
    if (f < 1e-6f) { sx = 0.0f; sy = 0.0f; return; }
    sx = -ori.y / f;
    sy =  ori.x / f;
}

void reposition_player(Body& b) {
    b.eye = b.pos;
    // lastclimb - ftotclk, i.e. minus the time since the climb. Within a
    // quarter second of one the eye is lifted, fading to nothing.
    const float f = -b.since_climb;
    if (f > -0.25f) b.eye.z += (f + 0.25f) / 0.25f;
}

}  // namespace

bool clip_box(const SolidMap& map, float x, float y, float z) {
    if (x < 0.0f || x >= SolidMap::SIZE_X || y < 0.0f || y >= SolidMap::SIZE_Y)
        return true;
    if (z < 0.0f) return false;

    int sz = static_cast<int>(z);
    // The bedrock layer answers for the one below it, and everything under
    // that is solid. These two lines are the entire behaviour of water in this
    // game - see the note in the header for the two resting depths they
    // produce.
    if (sz == 63) sz = 62;
    else if (sz >= 64) return true;

    // x and y are inside the map by the test above, so the truncation here is
    // a floor. The original truncates; keeping the same operation keeps the
    // same answers on the boundary.
    return map.solid(static_cast<int>(x), static_cast<int>(y), sz);
}

bool try_uncrouch(const SolidMap& map, Body& b) {
    const float x1 = b.pos.x + 0.45f, x2 = b.pos.x - 0.45f;
    const float y1 = b.pos.y + 0.45f, y2 = b.pos.y - 0.45f;
    const float z1 = b.pos.z + 2.25f;   // where the feet would go
    const float z2 = b.pos.z - 1.35f;   // where the head would go

    // Airborne: nothing to push off, so the body simply lowers its feet and
    // stays where it is. Note this branch does NOT move pos.
    if (b.airborne && !(clip_box(map, x1, y1, z1) || clip_box(map, x1, y2, z1) ||
                        clip_box(map, x2, y1, z1) || clip_box(map, x2, y2, z1)))
        return true;

    // Grounded: raise the head, which means pos.z decreases.
    if (!(clip_box(map, x1, y1, z2) || clip_box(map, x1, y2, z2) ||
          clip_box(map, x2, y1, z2) || clip_box(map, x2, y2, z2))) {
        b.pos.z -= 0.9f;
        b.eye.z -= 0.9f;
        return true;
    }
    return false;
}

bool set_crouch(const SolidMap& map, Body& b, bool want) {
    if (want == b.crouching) return true;
    if (want) {
        // The body shrinks upward from the feet, so the hip drops: +z.
        b.pos.z += 0.9f;
        b.eye.z += 0.9f;
        b.crouching = true;
        return true;
    }
    if (!try_uncrouch(map, b)) return false;   // a ceiling says no
    b.crouching = false;
    return true;
}

namespace {

// boxclipmove(), with the body's own dt rather than a global fsynctics.
void box_clip_move(const SolidMap& map, Body& b, const Input& in, float dt,
                   bool& climbed) {
    float f = dt * kVelocityToBlocks;
    const float nx = f * b.vel.x + b.pos.x;
    const float ny = f * b.vel.y + b.pos.y;

    float offset, m;
    if (b.crouching) { offset = 0.45f; m = 0.9f; }
    else             { offset = 0.9f;  m = 1.35f; }

    float nz = b.pos.z + offset;
    bool climb = false;

    // --- x ----------------------------------------------------------------
    // The probe is put on the leading face of the box, so a body moving -x
    // tests its own -x side.
    f = (b.vel.x < 0.0f) ? -0.45f : 0.45f;
    float z = m;
    while (z >= -1.36f && !clip_box(map, nx + f, b.pos.y - 0.45f, nz + z) &&
                          !clip_box(map, nx + f, b.pos.y + 0.45f, nz + z))
        z -= 0.9f;
    if (z < -1.36f) {
        b.pos.x = nx;                      // the whole body is clear
    } else if (!in.crouch && b.ori.z < 0.5f && !in.sprint) {
        // Blocked somewhere. Try again from 0.35 instead of m, which skips the
        // bottom row of probes: an obstruction only at the feet is a step, and
        // a step is climbable. Looking down (ori.z >= 0.5), crouching, or
        // sprinting all turn this off, which is why you cannot sprint up a
        // ledge and cannot climb while staring at your boots.
        z = 0.35f;
        while (z >= -2.36f && !clip_box(map, nx + f, b.pos.y - 0.45f, nz + z) &&
                              !clip_box(map, nx + f, b.pos.y + 0.45f, nz + z))
            z -= 0.9f;
        if (z < -2.36f) { b.pos.x = nx; climb = true; }
        else            { b.vel.x = 0.0f; }
    } else {
        b.vel.x = 0.0f;
    }

    // --- y ----------------------------------------------------------------
    f = (b.vel.y < 0.0f) ? -0.45f : 0.45f;
    z = m;
    while (z >= -1.36f && !clip_box(map, b.pos.x - 0.45f, ny + f, nz + z) &&
                          !clip_box(map, b.pos.x + 0.45f, ny + f, nz + z))
        z -= 0.9f;
    if (z < -1.36f) {
        b.pos.y = ny;
    } else if (!in.crouch && b.ori.z < 0.5f && !in.sprint && !climb) {
        z = 0.35f;
        while (z >= -2.36f && !clip_box(map, b.pos.x - 0.45f, ny + f, nz + z) &&
                              !clip_box(map, b.pos.x + 0.45f, ny + f, nz + z))
            z -= 0.9f;
        if (z < -2.36f) { b.pos.y = ny; climb = true; }
        else            { b.vel.y = 0.0f; }
    } else if (!climb) {
        b.vel.y = 0.0f;
    }

    // --- z ----------------------------------------------------------------
    if (climb) {
        // A climb is not a jump and does not go through the velocity: the body
        // is simply moved up a whole block, this tick, and pays for it with
        // half its horizontal speed. Gravity is skipped entirely for the tick.
        b.vel.x *= 0.5f;
        b.vel.y *= 0.5f;
        b.since_climb = 0.0f;
        nz -= 1.0f;
        m = -1.35f;
    } else {
        if (b.vel.z < 0.0f) m = -m;   // rising: test the head, not the feet
        nz += b.vel.z * dt * kVelocityToBlocks;
    }

    // Airborne until something under us says otherwise - set before the test,
    // so a body that walks off an edge is airborne on the tick it leaves it.
    b.airborne = true;

    if (clip_box(map, b.pos.x - 0.45f, b.pos.y - 0.45f, nz + m) ||
        clip_box(map, b.pos.x - 0.45f, b.pos.y + 0.45f, nz + m) ||
        clip_box(map, b.pos.x + 0.45f, b.pos.y - 0.45f, nz + m) ||
        clip_box(map, b.pos.x + 0.45f, b.pos.y + 0.45f, nz + m)) {
        if (b.vel.z >= 0.0f) {
            // Landed. Deep enough and it is water we landed in, not ground.
            b.wade = b.pos.z > kWadeZ;
            b.airborne = false;
        }
        b.vel.z = 0.0f;
    } else {
        b.pos.z = nz - offset;
    }

    reposition_player(b);
    climbed = climb;
}

}  // namespace

StepResult step(const SolidMap& map, Body& b, const Input& in, float dt) {
    StepResult out;
    if (dt <= 0.0f) { reposition_player(b); return out; }

    b.since_climb += dt;

    // The jump. pyspades consumes a one-shot `p->jump` that world.pyx only
    // ever sets when the player is not airborne; the gate is written here
    // instead, in the one place that knows, so that holding the key does not
    // fly and a caller cannot get it wrong.
    if (in.jump && !b.airborne) b.vel.z = kJumpVelocity;

    // Acceleration for this tick. One branch wins - these are else-ifs, so an
    // airborne sprinting player accelerates at the airborne rate and a
    // crouching one at the crouch rate no matter what else is held.
    float f = dt;
    if (b.airborne)                                 f *= 0.1f;
    else if (in.crouch)                             f *= 0.3f;
    else if ((in.secondary_fire && in.holding_gun) || in.sneak) f *= 0.5f;
    else if (in.sprint)                             f *= 1.3f;

    if ((in.mf || in.mb) && (in.ml || in.mr)) f *= kDiagonal;

    float sx, sy;
    strafe_vector(b.ori, sx, sy);

    if (in.mf)      { b.vel.x += b.ori.x * f; b.vel.y += b.ori.y * f; }
    else if (in.mb) { b.vel.x -= b.ori.x * f; b.vel.y -= b.ori.y * f; }
    if (in.ml)      { b.vel.x -= sx * f;      b.vel.y -= sy * f; }
    else if (in.mr) { b.vel.x += sx * f;      b.vel.y += sy * f; }

    // "this is a garbage way to simulate friction" - the original's own
    // comment, kept because it is the truth and because the arithmetic is
    // load-bearing anyway: gravity goes on before the divisor, so terminal
    // velocity falls out of the same line.
    f = dt + 1.0f;
    b.vel.z += dt;
    b.vel.z /= f;
    if (b.wade)          f = dt * 6.0f + 1.0f;
    else if (!b.airborne) f = dt * 4.0f + 1.0f;
    b.vel.x /= f;
    b.vel.y /= f;

    const float fell_at = b.vel.z;
    box_clip_move(map, b, in, dt, out.climbed);

    // Landing. move_player returns -1 for a landing that only slowed us and a
    // positive number for one that hurt, so both branches are a landing.
    if (b.vel.z == 0.0f && fell_at > kFallSlowDown) {
        b.vel.x *= 0.5f;
        b.vel.y *= 0.5f;
        out.landed = true;
        if (fell_at > kFallDamageVelocity) {
            const float over = fell_at - kFallDamageVelocity;
            out.fall_damage = static_cast<int>(over * over * kFallDamageScalar);
        }
    }
    return out;
}

}  // namespace physics
