#!/usr/bin/env python3
"""
Checks Server/vr_presence.py against a real piqueserver, from fake clients that
speak just enough of AoS 0.75 to join.

    python3 Server/test/test_vr_presence.py     (with piqueserver installed)

It needs piqueserver (and so pyspades and pyenet) importable, and g++ for the
format check against the client's own C++ encoder, which it skips without.

What it holds:
  - the C++ encoder and the script agree on every byte of a pose;
  - a pose from a client that negotiated the extension reaches other clients
    that negotiated it, with the sender's id, untouched;
  - it never reaches a client that did not negotiate it (a stock client);
  - poses faster than max_rate are dropped, implausible ones are refused;
  - body_sync moves the body by a small step and refuses a big one;
  - body_is_clear keeps a body out of a wall (unit test, with a fake map);
  - the server sends each VR client the config sub packet.
"""

import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))  # Server/, for vr_presence

import enet  # noqa: E402
import vr_presence  # noqa: E402

EXT = vr_presence.EXTENSION_ID
PACKET = vr_presence.PACKET_ID


def pack_pose(flags, head=(0, 0, 0), left=(0, 0, 0), right=(0, 0, 0),
              body=(0.0, 0.0, 0.0), buttons=0):
    """A pose payload, built the way VRNetProtocol.h lays it out."""
    out = bytes((flags,))
    for pos in (head, left, right):
        out += struct.pack('<hhh', *(int(round(v * 512)) for v in pos))
        out += struct.pack('<hhhh', 0, 0, 0, 32767)
    out += struct.pack('<fff', *body) + bytes((buttons,))
    assert len(out) == vr_presence.POSE_PAYLOAD_BYTES
    return out


class FormatTest(unittest.TestCase):
    """The C++ encoder, compiled from the client's own header, against the
    offsets the script reads."""

    def test_cpp_fixture_matches_script_layout(self):
        gxx = shutil.which('g++')
        if gxx is None:
            self.skipTest('g++ not available')
        exe = os.path.join(tempfile.mkdtemp(), 'vr_pose_fixture')
        subprocess.check_call([
            gxx, '-std=c++17', '-I', os.path.join(REPO, 'Sources'),
            '-o', exe, os.path.join(HERE, 'vr_pose_fixture.cpp'),
            os.path.join(REPO, 'Sources', 'Client', 'VRState.cpp')])
        data = bytes.fromhex(subprocess.check_output([exe]).decode().strip())

        self.assertEqual(data[0], PACKET)
        self.assertEqual(data[1], vr_presence.SUB_POSE)
        payload = data[2:]
        self.assertEqual(len(payload), vr_presence.POSE_PAYLOAD_BYTES)
        self.assertEqual(payload[0], 0b0111)  # both hands and the body

        head = struct.unpack_from('<hhh', payload, 1)
        self.assertEqual([v / 512 for v in head], [0.25, -0.5, 0.125])
        left = struct.unpack_from('<hhh', payload, 15)
        self.assertEqual([v / 512 for v in left], [-0.5, 0.75, 0.5])
        right = struct.unpack_from('<hhh', payload, 29)
        self.assertEqual([v / 512 for v in right], [1.0, 0.25, 0.375])
        head_rot = struct.unpack_from('<hhhh', payload, 7)
        self.assertEqual(head_rot, (0, 0, 23170, 23170))
        body = struct.unpack_from('<fff', payload, vr_presence.BODY_OFFSET)
        self.assertEqual(body, (256.5, 128.25, 60.0))
        self.assertEqual(payload[55], 0b1001)
        self.assertTrue(vr_presence.pose_is_plausible(payload, 4.0))


class FakeMap:
    def __init__(self, solid):
        self.solid = set(solid)

    def get_solid(self, x, y, z):
        return (x, y, z) in self.solid


class RulesTest(unittest.TestCase):
    def test_reach(self):
        self.assertTrue(vr_presence.pose_is_plausible(
            pack_pose(3, left=(1, 1, 1), right=(-2, 0.5, 1)), 4.0))
        self.assertFalse(vr_presence.pose_is_plausible(
            pack_pose(3, right=(9.0, 0, 0)), 4.0))
        self.assertFalse(vr_presence.pose_is_plausible(
            pack_pose(4, body=(float('nan'), 0.0, 0.0)), 4.0))

    def test_body_stays_out_of_walls(self):
        # A wall at x = 101, head to shins, for a body with its eye at z = 60.
        wall = [(101, y, z) for y in range(98, 103) for z in range(59, 62)]
        game_map = FakeMap(wall)
        self.assertTrue(vr_presence.body_is_clear(game_map, 100.3, 100.5, 60.0))
        # 100.6 + 0.45 reaches into x = 101.
        self.assertFalse(vr_presence.body_is_clear(game_map, 100.6, 100.5, 60.0))
        # Off the map is never clear.
        self.assertFalse(vr_presence.body_is_clear(game_map, 0.2, 100.0, 60.0))
        # Open sky above the map is.
        self.assertTrue(vr_presence.body_is_clear(FakeMap([]), 50.0, 50.0, 0.1))

    def test_ground_under_a_resting_body_is_not_a_wall(self):
        # Ground at z = 63. At rest the feet (eye + 2.25) sit anywhere inside
        # that block, so every eye height in [60.75, 61.75) is standing on it.
        ground = FakeMap([(x, y, 63) for x in range(95, 106) for y in range(95, 106)])
        for eye in (60.75, 61.2, 61.74):
            self.assertTrue(vr_presence.body_is_clear(ground, 100.3, 100.5, eye), eye)
        # A one-block step on that ground is in the way.
        step = FakeMap([(x, y, 63) for x in range(95, 106) for y in range(95, 106)] +
                       [(101, y, 62) for y in range(95, 106)])
        self.assertFalse(vr_presence.body_is_clear(step, 100.6, 100.5, 60.8))
        # Crouched, the eye is 0.9 lower and the same ground is still open.
        self.assertTrue(vr_presence.body_is_clear(ground, 100.3, 100.5, 62.6, True))


class FakeClient:
    """Speaks enough 0.75 to join: the handshake, the version reply, the
    extension exchange, the map download and ExistingPlayer."""

    def __init__(self, port, name, extensions):
        self.host = enet.Host(None, 1, 1)
        self.host.compress_with_range_coder()
        self.peer = self.host.connect(enet.Address(b'127.0.0.1', port), 1, 3)
        self.name = name
        self.extensions = extensions
        self.player_id = None
        self.map_done = False
        self.packets = []
        self.configs = []  # VR config sub packets, never cleared
        self.server_extensions = None

    def send(self, data, reliable=True):
        flags = enet.PACKET_FLAG_RELIABLE if reliable else enet.PACKET_FLAG_UNSEQUENCED
        self.peer.send(0, enet.Packet(bytes(data), flags))

    def pump(self, seconds=0.05):
        end = time.time() + seconds
        while True:
            event = self.host.service(5)
            while event is not None and event.type != enet.EVENT_TYPE_NONE:
                if event.type == enet.EVENT_TYPE_RECEIVE:
                    self.handle(bytes(event.packet.data))
                event = self.host.service(0)
            if time.time() >= end:
                return

    def handle(self, data):
        kind = data[0]
        self.packets.append(data)
        if kind == PACKET and len(data) > 1 and data[1] == vr_presence.SUB_CONFIG:
            self.configs.append(data)
        if kind == 31 and len(data) >= 5:  # HandShakeInit
            self.send(bytes((32,)) + data[1:5])
        elif kind == 33:  # VersionRequest: say we are OpenSpades 0.1.5
            self.send(bytes((34,)) + b'o' + bytes((0, 1, 5)) + b'VR test\0')
        elif kind == 60:  # ProtocolExtensionInfo
            count = data[1]
            offered = {data[2 + 2 * i]: data[3 + 2 * i] for i in range(count)}
            self.server_extensions = offered
            reply = [(k, v) for k, v in offered.items() if k in self.extensions]
            self.send(bytes((60, len(reply))) + b''.join(bytes(e) for e in reply))
        elif kind == 15:  # StateData: the map is in and this is our id
            self.player_id = data[1]
            self.map_done = True

    def join(self, team):
        name = self.name.encode() + b'\0'
        self.send(bytes((9, 0, team & 0xFF, 0, 0)) + struct.pack('<I', 0) +
                  bytes((0, 0, 0)) + name)

    def vr_packets(self, sub=vr_presence.SUB_POSE):
        return [p for p in self.packets if p[0] == PACKET and len(p) > 1 and p[1] == sub]

    def world_position(self, player_id):
        """The latest position the server reported for a player."""
        for p in reversed(self.packets):
            if p[0] == 2 and len(p) >= 1 + 24 * (player_id + 1):
                return struct.unpack_from('<fff', p, 1 + 24 * player_id)
        return None


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from pyspades.vxl import VXLData

        cls.dir = tempfile.mkdtemp(prefix='vr_presence_')
        os.makedirs(os.path.join(cls.dir, 'maps'))
        os.makedirs(os.path.join(cls.dir, 'scripts'))
        # An empty map: open floor everywhere, so a sideways step is never
        # blocked and the only thing refusing one is the script's step limit.
        with open(os.path.join(cls.dir, 'maps', 'flat.vxl'), 'wb') as f:
            f.write(VXLData().generate())
        shutil.copy(os.path.join(os.path.dirname(HERE), 'vr_presence.py'),
                    os.path.join(cls.dir, 'scripts', 'vr_presence.py'))
        cls.port = free_udp_port()
        with open(os.path.join(cls.dir, 'config.toml'), 'w') as f:
            f.write(f'''name = "vr presence test"
master = false
port = {cls.port}
max_connections_per_ip = 8
rotation = ["flat"]
scripts = ["vr_presence"]
game_mode = "ctf"
respawn_time = "1sec"
respawn_waves = false
balanced_teams = 0
# The empty map spawns bodies at the top of the sky; the landing would kill.
fall_damage = false
[vr_presence]
max_rate = 20
body_sync = true
max_step = 0.6
''')
        cls.server = subprocess.Popen(
            [sys.executable, '-m', 'piqueserver', '-d', cls.dir],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(3.0)
        if cls.server.poll() is not None:
            raise RuntimeError(cls.server.stdout.read().decode())

        cls.vr = FakeClient(cls.port, 'vrplayer', {EXT})
        cls.flat = FakeClient(cls.port, 'flatplayer', {EXT})
        cls.stock = FakeClient(cls.port, 'stockplayer', set())
        cls.clients = [cls.vr, cls.flat, cls.stock]
        deadline = time.time() + 20
        while not all(c.map_done for c in cls.clients):
            for c in cls.clients:
                c.pump(0.02)
            if time.time() > deadline:
                raise RuntimeError('clients never received the map')
        for i, c in enumerate(cls.clients):
            c.join(i % 2)
        # Poses from a player who has not spawned are ignored, so wait for the
        # VR player's CreatePlayer.
        deadline = time.time() + 20
        while not any(p[0] == 12 and p[1] == cls.vr.player_id for p in cls.vr.packets):
            cls.pump_all(0.1)
            if time.time() > deadline:
                raise RuntimeError('the VR player never spawned')
        cls.pump_all(0.5)

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        try:
            out, _ = cls.server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            cls.server.kill()
            out, _ = cls.server.communicate()
        if os.environ.get('VR_TEST_SERVER_LOG'):
            print(out.decode(errors='replace'))
        shutil.rmtree(cls.dir, ignore_errors=True)

    @classmethod
    def pump_all(cls, seconds):
        end = time.time() + seconds
        while time.time() < end:
            for c in cls.clients:
                c.pump(0.01)

    def send_pose(self, payload):
        self.vr.send(bytes((PACKET, vr_presence.SUB_POSE)) + payload, reliable=False)

    def clear(self):
        for c in self.clients:
            c.packets.clear()

    def relayed(self, send, timeout=10.0):
        """Runs `send` (one pose, or a burst) and returns the poses the flat
        player heard from it, trying again until it hears some or `timeout`
        passes. The poses go unreliable, and the server drops every one sent
        while the VR player is dead or between lives, so a single try can
        come back empty on a slow machine without anything being wrong."""
        deadline = time.time() + timeout
        while True:
            time.sleep(0.2)  # past the rate limit of the try before
            self.clear()
            send()
            self.pump_all(0.4)
            got = self.flat.vr_packets()
            if got or time.time() > deadline:
                return got

    def test_1_extension_is_advertised(self):
        self.assertIn(EXT, self.vr.server_extensions)

    def test_2_pose_reaches_extension_clients_only(self):
        payload = pack_pose(3, head=(0.1, 0.0, -0.05), left=(-0.5, 0.4, 0.6),
                            right=(0.6, 0.3, 0.5), buttons=1)
        got = self.relayed(lambda: self.send_pose(payload))
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0][2], self.vr.player_id)
        self.assertEqual(got[0][3:], payload)
        self.assertEqual(self.stock.vr_packets(), [])
        self.assertEqual(self.vr.vr_packets(), [])  # nobody hears their own echo

    def test_3_config_is_sent_once(self):
        # On spawn, to every client that negotiated the extension.
        self.assertEqual(len(self.vr.configs), 1)
        flags, rate = self.vr.configs[0][2], self.vr.configs[0][3]
        self.assertEqual(flags, vr_presence.CONFIG_BODY_SYNC)
        self.assertEqual(rate, 20)
        self.assertEqual(self.stock.configs, [])

    def test_4_rate_limit(self):
        def burst():
            for _ in range(6):
                self.send_pose(pack_pose(3))
        # Six at once, and one gets through
        self.assertEqual(len(self.relayed(burst)), 1)

    def test_5_implausible_pose_is_dropped(self):
        time.sleep(0.2)
        self.clear()
        self.send_pose(pack_pose(3, right=(20.0, 0.0, 0.0)))
        self.pump_all(0.4)
        self.assertEqual(self.flat.vr_packets(), [])

    def test_6_body_sync(self):
        # A new body drops onto the map; wait for it to land.
        last = None
        for _ in range(40):
            self.pump_all(0.2)
            now = self.flat.world_position(self.vr.player_id)
            if now is not None and last is not None and abs(now[2] - last[2]) < 1e-3:
                break
            last = now
        before = self.flat.world_position(self.vr.player_id)
        self.assertIsNotNone(before, 'no WorldUpdate for the VR player')
        x, y, z = before

        # A small step is taken (sent again until the server moves the body,
        # as a pose can be dropped)...
        deadline = time.time() + 10.0
        while True:
            time.sleep(0.2)
            self.send_pose(pack_pose(4, body=(x + 0.3, y, z)))
            self.pump_all(0.6)
            after = self.flat.world_position(self.vr.player_id)
            if abs(after[0] - (x + 0.3)) <= 0.05 or time.time() > deadline:
                break
        self.assertAlmostEqual(after[0], x + 0.3, delta=0.05,
                               msg=f'body at {before}, then {after}')

        # ...a big one is not.
        time.sleep(0.2)
        self.send_pose(pack_pose(4, body=(after[0] + 2.0, after[1], after[2])))
        self.pump_all(0.6)
        final = self.flat.world_position(self.vr.player_id)
        self.assertAlmostEqual(final[0], after[0], delta=0.05)


if __name__ == '__main__':
    unittest.main(verbosity=2)
