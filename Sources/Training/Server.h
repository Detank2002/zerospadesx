#pragma once
// The training course's server: a small Ace of Spades 0.75 server, run on a
// thread of the game, that hosts the course (TrainingCourse.h) on 127.0.0.1.
// The game's own client connects to it like to any other server, so the
// tutorial is the real game: the same packets, guns, damage, block rules and
// floating-block check as pyspades, which every client repeats on its own and
// the server has to agree with.
//
// It plays nothing but the course. The range's targets are its own players,
// with no connection behind them: they walk a lane and fall over.
//
// Where pyspades makes a choice because its players are on the internet, this
// one makes the other choice, because they are not: world updates go out at
// `update_rate` a second (pyspades sends 10), a position a client reports is
// taken as it is, and the map goes out as fast as the link takes it.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "TrainingModes.h"

namespace host {

struct HostConfig {
    uint16_t port = 32887;
    // World updates a second.
    int update_rate = 60;
    // Midnight, through the Daytime and Weather extension: a client that
    // claims it draws a night, and only lamps and flashes light it.
    bool night = false;
    float respawn_sec = 5.0f;
    int max_players = 32;
    // Where the server's log lines go; nowhere if empty.
    std::function<void(const std::string&)> log;
    // The tutorial, or an aim scenario in the arena.
    training::Mode mode = training::Mode::Tutorial;
    training::Scenario scenario = training::Scenario::Gridshot;
    // Aim: outline the targets (a Teamplay mark), through walls too.
    bool highlight_targets = false;
    // Aim: the run's status for the HUD, and its restart requests. Shared with
    // the game, which runs this server on a thread of its own.
    std::shared_ptr<training::AimShared> aim;
};

class Server {
public:
    explicit Server(const HostConfig& cfg);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Build the course and bind the port. False with a reason.
    bool start(std::string& err);
    // Run until `stop` goes true. Call start() first.
    void run(const std::atomic<bool>& stop);
    // One pass of the loop, waiting at most `wait_ms` for packets. What run()
    // calls; a test can drive it by hand.
    void pump(int wait_ms);

    // For tests: what the server believes.
    int player_count() const;
    int player_hp(int id) const;          // -1 if no such player or dead
    bool player_alive(int id) const;
    int player_team(int id) const;        // -2 if no such player
    // Where a living player is; false if there is no such living player.
    bool player_position(int id, float& x, float& y, float& z) const;
    // Whether the map's block here is solid, as the server has it.
    bool map_solid(int x, int y, int z) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace host
