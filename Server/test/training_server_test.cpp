// Plays the training course's server (Sources/Training) from a bare ENet client,
// headless: joins it as a player would, then shoots one of the range's
// targets. Built by CMake as `training_server_test` (Sources/Training), and run
// by CTest; or by hand from the repository root against an ENet and a zlib.
//
// The handshake and the map: the server asks for the version, sends a map that
// decompresses to a whole .vxl, and the state after it. The join: a person
// asking for green is put on blue (the course has one team), and spawns at the
// start. The range: three targets, alive, on green, walking; a rifle shot to
// a target's head (after the trigger is down) kills it, and one with the
// trigger up does nothing (the control).
#include <Training/Server.h>
#include <Training/TrainingCourse.h>

#include <enet/enet.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

namespace {
	constexpr uint16_t kPort = 39187;
	enum : uint8_t {
		kWeaponInput = 4, kHit = 5, kExistingPlayer = 9, kCreatePlayer = 12, kStateData = 15,
		kKillAction = 16, kMapStart = 18, kMapChunk = 19, kHandshakeInit = 31,
		kHandshakeResponse = 32, kVersionGet = 33, kVersionResponse = 34
	};

	struct Client {
		ENetHost* host = nullptr;
		ENetPeer* peer = nullptr;
		bool connected = false;
		std::vector<std::vector<uint8_t>> received;

		void Send(const std::vector<uint8_t>& data) {
			ENetPacket* p = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(peer, 0, p);
		}
		void Service() {
			ENetEvent ev;
			while (enet_host_service(host, &ev, 0) > 0) {
				if (ev.type == ENET_EVENT_TYPE_CONNECT) {
					connected = true;
				} else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
					received.emplace_back(ev.packet->data, ev.packet->data + ev.packet->dataLength);
					enet_packet_destroy(ev.packet);
				}
			}
		}
		const std::vector<uint8_t>* Find(uint8_t id, size_t from = 0) const {
			for (size_t i = from; i < received.size(); i++)
				if (!received[i].empty() && received[i][0] == id)
					return &received[i];
			return nullptr;
		}
	};

	// Runs the server and the client for `ms`, or until `done` says so.
	template <class F> bool Run(host::Server& s, Client& c, int ms, F done) {
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
		while (std::chrono::steady_clock::now() < until) {
			s.pump(1);
			c.Service();
			if (done())
				return true;
		}
		return false;
	}
} // namespace

int main(int argc, char** argv) {
	if (enet_initialize() != 0) {
		std::printf("FAIL: no ENet\n");
		return 1;
	}
	host::HostConfig cfg;
	cfg.port = kPort;

	// `--serve`: host the course until killed, for a game to join by hand
	// (aos:// address of 127.0.0.1, port 39187) without the menu's button.
	if (argc > 1 && std::string(argv[1]) == "--serve") {
		cfg.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); std::fflush(stdout); };
		host::Server server(cfg);
		std::string why;
		if (!server.start(why)) {
			std::printf("start: %s\n", why.c_str());
			return 1;
		}
		std::atomic<bool> stop{false};
		server.run(stop);
		return 0;
	}

	host::Server s(cfg);
	std::string err;
	CHECK(s.start(err));
	if (!err.empty())
		std::printf("start: %s\n", err.c_str());

	// The range's targets are there before anybody joins: three, on green, alive.
	CHECK(s.player_count() == training::kLaneCount);
	for (int id = 0; id < training::kLaneCount; id++) {
		CHECK(s.player_team(id) == 1);
		CHECK(s.player_alive(id));
	}
	float x0, y0, z0;
	CHECK(s.player_position(0, x0, y0, z0));

	Client c;
	c.host = enet_host_create(nullptr, 1, 1, 0, 0);
	ENetAddress addr;
	enet_address_set_host(&addr, "127.0.0.1");
	addr.port = kPort;
	c.peer = enet_host_connect(c.host, &addr, 1, 3); // protocol version 3: 0.75
	CHECK(Run(s, c, 3000, [&] { return c.connected && c.Find(kHandshakeInit); }));

	// The version exchange, as a client answers it.
	if (const auto* init = c.Find(kHandshakeInit)) {
		std::vector<uint8_t> resp{kHandshakeResponse};
		resp.insert(resp.end(), init->begin() + 1, init->end());
		c.Send(resp);
	}
	CHECK(Run(s, c, 3000, [&] { return c.Find(kVersionGet) != nullptr; }));
	c.Send({kVersionResponse, 'o', 0, 1, 5, 't', 'e', 's', 't', 0});

	// The map, whole, then the state.
	CHECK(Run(s, c, 5000, [&] { return c.Find(kStateData) != nullptr; }));
	const auto* start = c.Find(kMapStart);
	CHECK(start && start->size() >= 5);
	size_t mapBytes = 0;
	for (const auto& p : c.received)
		if (!p.empty() && p[0] == kMapChunk)
			mapBytes += p.size() - 1;
	if (start && start->size() >= 5) {
		uint32_t announced;
		std::memcpy(&announced, start->data() + 1, 4);
		CHECK(mapBytes == announced);
	}

	// Join asking for green, with a rifle: put on blue, at the start.
	const size_t beforeJoin = c.received.size();
	std::vector<uint8_t> join{kExistingPlayer, 0, 1, 0, 2, 0, 0, 0, 0, 0x80, 0x80, 0x80};
	for (char ch : std::string("Tester"))
		join.push_back(static_cast<uint8_t>(ch));
	join.push_back(0);
	c.Send(join);
	int me = -1;
	CHECK(Run(s, c, 3000, [&] {
		for (size_t i = beforeJoin; i < c.received.size(); i++) {
			const auto& p = c.received[i];
			if (p.size() > 2 && p[0] == kCreatePlayer && p[1] >= training::kLaneCount)
				me = p[1];
		}
		return me >= 0;
	}));
	CHECK(me >= 0 && s.player_team(me) == 0);
	float x, y, z;
	CHECK(me >= 0 && s.player_position(me, x, y, z) &&
	      std::abs(x - training::kSpawnX) < 4.0F && std::abs(y - training::kSpawnY) < 4.0F);

	// The targets walk: a second later, the first is somewhere else.
	Run(s, c, 1000, [] { return false; });
	float x1, y1, z1;
	CHECK(s.player_position(0, x1, y1, z1) && std::abs(y1 - y0) > 0.5F);

	// The control: a head shot with the trigger up does nothing.
	c.Send({kHit, 0, 1});
	Run(s, c, 300, [] { return false; });
	CHECK(s.player_alive(0));

	// Trigger down, then the head shot: the target falls, and everybody is told.
	const size_t beforeShot = c.received.size();
	c.Send({kWeaponInput, static_cast<uint8_t>(me), 1});
	Run(s, c, 100, [] { return false; });
	c.Send({kHit, 0, 1});
	CHECK(Run(s, c, 1000, [&] {
		const auto* k = c.Find(kKillAction, beforeShot);
		return k && k->size() > 2 && (*k)[1] == 0 && (*k)[2] == me;
	}));
	CHECK(!s.player_alive(0));

	// And it is back on its lane a couple of seconds later.
	CHECK(Run(s, c, 4000, [&] { return s.player_alive(0); }));

	enet_peer_disconnect_now(c.peer, 0);
	enet_host_destroy(c.host);
	enet_deinitialize();
	if (failures == 0)
		std::printf("training_server_test: all passed\n");
	return failures == 0 ? 0 : 1;
}
