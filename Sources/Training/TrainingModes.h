#pragma once
// What Training runs: the tutorial (the course, TrainingCourse.h) or one of the
// aim scenarios, in an arena of its own beside the course on the same map.
// Shared by the server (Server.cpp, MapGen.cpp) and the client (the HUD reads
// AimStatus through gui::TrainingHost, in the same process).
//
// Header only and dependency free but <mutex>, so the client can include it.
// AoS axes: x and y across the map, z down (0 the sky, 63 the bottom).

#include <mutex>

namespace training {

enum class Mode { Tutorial = 0, Aim = 1 };

enum class Scenario {
    Gridshot = 0,  // three still targets on stands; each one shot is replaced
    Flick,         // one target at a time, somewhere around you, pinged first
    Tracking,      // one target strafing and jumping; keep your aim on it
    StrafeDuel,    // targets that strafe and stop like players, with real HP
    LongRange,     // walkers far across the arena
    Count
};
constexpr int kScenarioCount = static_cast<int>(Scenario::Count);

// Seconds a run lasts, and the countdown before it.
constexpr float kRunSeconds = 60.0f;
constexpr float kCountdownSeconds = 3.0f;

// Whether a scenario is scored on the time your aim is on the target, rather
// than on kills.
constexpr bool scored_on_tracking(Scenario s) { return s == Scenario::Tracking; }
// Whether any hit kills (the scenario trains aim, not damage).
constexpr bool one_shot(Scenario s) { return s == Scenario::Gridshot || s == Scenario::Flick; }

// --- the arena: a walled yard south of the course (x < 160, where the ground
// is at kFloorZ everywhere) ---------------------------------------------------
constexpr int kArenaX0 = 20, kArenaX1 = 150, kArenaY0 = 300, kArenaY1 = 440;
constexpr int kArenaFloorZ = 40;       // the floor's top voxel, as the course's
constexpr int kAimSpawnX = 40, kAimSpawnY = 370;  // the player, facing +x
// Gridshot's stands: three steps across the arena ahead of the player, each
// kStandDepth deep and higher than the one before.
constexpr int kStandCount = 3;
constexpr int kStandX[kStandCount] = {56, 61, 66};
constexpr int kStandDepth = 5;
constexpr int kStandRise[kStandCount] = {2, 4, 6};
constexpr int kStandY0 = 357, kStandY1 = 383;
// Long range's lanes: walkers crossing the arena 80 to 105 blocks out.
constexpr int kFarLaneCount = 3;
constexpr int kFarLaneX[kFarLaneCount] = {120, 133, 145};
constexpr float kFarLaneY0 = 335.5f, kFarLaneY1 = 405.5f;

// --- the tutorial's steps, in the order the course asks for them -------------
enum Step { StepWalk = 0, StepShoot, StepDig, StepClimb, StepBlast, StepFinish, StepCount };
// The VR-only steps, shown after the course's when in a headset.
enum VRStep { VRStepWrist = 0, VRStepHolster, VRStepTwoHands, VRStepReload, VRStepCount };
// Targets to down at the range for the shooting step.
constexpr int kRangeKills = 3;

// --- what a run of a scenario has come to, for the HUD -----------------------
enum class AimPhase { Waiting = 0, Countdown, Running, Finished };

struct AimStatus {
    Scenario scenario = Scenario::Gridshot;
    int weapon = 0;               // 0 rifle, 1 SMG, 2 shotgun
    AimPhase phase = AimPhase::Waiting;
    float countdown = 0.0f;       // seconds left of the countdown
    float time_left = 0.0f;       // seconds left of the run
    int kills = 0;
    int shots = 0;                // rounds fired (a shotgun shell is one)
    int hits = 0;                 // shots that hit a target, a pellet or more
    int headshots = 0;            // shots with a head hit
    float on_target = 0.0f;       // seconds the aim was on the target (Tracking)
    float elapsed = 0.0f;         // seconds of the run so far
    float ttk_sum = 0.0f;         // seconds from a target's appearing to its fall
    int ttk_count = 0;
    int run = 0;                  // counts runs, so the HUD sees a new one start

    float accuracy() const { return shots > 0 ? static_cast<float>(hits) / static_cast<float>(shots) : 0.0f; }
    float tracking() const { return elapsed > 0.0f ? on_target / elapsed : 0.0f; }
    float average_ttk() const { return ttk_count > 0 ? ttk_sum / static_cast<float>(ttk_count) : 0.0f; }
    // The scenario's score: kills weighted by accuracy, or the time on target
    // in hundredths of a percent.
    int score() const {
        if (scored_on_tracking(scenario)) return static_cast<int>(tracking() * 10000.0f + 0.5f);
        float acc = accuracy();
        if (acc > 1.0f) acc = 1.0f;
        return static_cast<int>(static_cast<float>(kills) * 100.0f * (0.5f + 0.5f * acc) + 0.5f);
    }
};

// Shared by the server's thread and the client's: the status the server
// writes, and what the client asks of it.
struct AimShared {
    std::mutex mutex;
    AimStatus status;
    bool restart = false;     // the client asks for another run
};

}  // namespace training
