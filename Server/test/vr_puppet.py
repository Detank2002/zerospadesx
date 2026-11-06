#!/usr/bin/env python3
"""
A pretend VR player: joins a server running vr_presence.py and sends a VR pose
twenty times a second - looking around, the right hand aiming, the left hand
drawing circles - so a VR avatar can be looked at from a client without a
second headset.

    python3 Server/test/vr_puppet.py HOST [PORT] [--team 0|1] [--name NAME]
                                     [--follow PLAYER_ID]

With --follow it walks up to that player and stands four blocks in front of
them, facing them, so it is always in view. Needs pyenet (it comes with
piqueserver). Runs until interrupted.
"""

import argparse
import math
import struct
import time

PACKET = 122
SUB_POSE = 0
FLAG_LEFT, FLAG_RIGHT = 1 << 0, 1 << 1


def yaw_quat(angle, pitch=0.0):
    """A heading `angle` (radians, the game's yaw) then a pitch, as the Hamilton
    quaternion of the frame (forward, right, down) in world axes, where the
    game's z points down: yaw turns about +z, pitch about the right axis."""
    qy = (0.0, 0.0, math.sin(angle / 2), math.cos(angle / 2))
    # About the local right axis (+y), a positive turn tips forward towards
    # -z, which is up.
    qp = (0.0, math.sin(pitch / 2), 0.0, math.cos(pitch / 2))
    x1, y1, z1, w1 = qy
    x2, y2, z2, w2 = qp
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def pack_pose(flags, head, head_q, left, left_q, right, right_q, body, buttons=0):
    out = bytes((flags,))
    for pos, q in ((head, head_q), (left, left_q), (right, right_q)):
        out += struct.pack('<hhh', *(int(round(v * 512)) for v in pos))
        out += struct.pack('<hhhh', *(int(round(v * 32767)) for v in q))
    return out + struct.pack('<fff', *body) + bytes((buttons,))


class Puppet:
    # pyenet only for the network side, so tests can use pack_pose without it
    def __init__(self, host, port, name, team):
        global enet
        import enet
        self.host = enet.Host(None, 1, 1)
        self.host.compress_with_range_coder()
        self.peer = self.host.connect(enet.Address(host.encode(), port), 1, 3)
        self.name, self.team = name, team
        self.player_id = None
        self.spawned = False
        self.position = None
        self.vr_ok = False
        self.world = {}  # player id -> (position, orientation), from WorldUpdate

    def send(self, data, reliable=True):
        flags = enet.PACKET_FLAG_RELIABLE if reliable else enet.PACKET_FLAG_UNSEQUENCED
        self.peer.send(0, enet.Packet(bytes(data), flags))

    def pump(self):
        event = self.host.service(0)
        while event is not None and event.type != enet.EVENT_TYPE_NONE:
            if event.type == enet.EVENT_TYPE_RECEIVE:
                self.handle(bytes(event.packet.data))
            elif event.type == enet.EVENT_TYPE_DISCONNECT:
                raise SystemExit('disconnected')
            event = self.host.service(0)

    def handle(self, data):
        kind = data[0]
        if kind == 31 and len(data) >= 5:
            self.send(bytes((32,)) + data[1:5])
        elif kind == 33:
            self.send(bytes((34,)) + b'o' + bytes((0, 1, 5)) + b'vr_puppet\0')
        elif kind == 60:
            offered = {data[2 + 2 * i]: data[3 + 2 * i] for i in range(data[1])}
            self.vr_ok = 58 in offered
            reply = [(58, offered[58])] if self.vr_ok else []
            self.send(bytes((60, len(reply))) + b''.join(bytes(e) for e in reply))
            if not self.vr_ok:
                print('the server does not run vr_presence.py; nobody will see the pose')
        elif kind == 15:
            self.player_id = data[1]
            name = self.name.encode() + b'\0'
            self.send(bytes((9, 0, self.team, 0, 0)) + struct.pack('<I', 0) +
                      bytes((0, 0, 0)) + name)
        elif kind == 12 and len(data) > 1 and data[1] == self.player_id:
            self.spawned = True
            print(f'spawned as player {self.player_id}')
        elif kind == 2:
            for pid in range((len(data) - 1) // 24):
                x, y, z, ox, oy, oz = struct.unpack_from('<6f', data, 1 + 24 * pid)
                self.world[pid] = ((x, y, z), (ox, oy, oz))
            if self.player_id in self.world:
                self.position = self.world[self.player_id][0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('host')
    ap.add_argument('port', nargs='?', type=int, default=32887)
    ap.add_argument('--team', type=int, default=0)
    ap.add_argument('--name', default='VR Puppet')
    ap.add_argument('--follow', nargs='?', const='nearest', default=None,
                    help='a player id, or nothing for the nearest other player')
    args = ap.parse_args()

    p = Puppet(args.host, args.port, args.name, args.team)
    start = time.time()
    next_pose = 0.0
    next_move = 0.0
    base_facing = 0.0
    while True:
        p.pump()
        now = time.time()

        # Follow: stand in front of the player, facing them. PositionData is only
        # accepted every 0.7s and within the rubberband distance, so step there.
        target = None
        if args.follow == 'nearest':
            if p.position:
                others = [(math.dist(pos, p.position), pid)
                          for pid, (pos, _) in p.world.items()
                          if pid != p.player_id and any(pos)]
                target = min(others)[1] if others else None
        elif args.follow is not None:
            target = int(args.follow)
        if target is not None and p.spawned and now >= next_move and \
                target in p.world and p.position:
            next_move = now + 1.0
            (tx, ty, tz), (ox, oy, _) = p.world[target]
            flat = math.hypot(ox, oy) or 1.0
            goal = (tx + ox / flat * 4.0, ty + oy / flat * 4.0)
            px, py, pz = p.position
            dx, dy = goal[0] - px, goal[1] - py
            dist = math.hypot(dx, dy)
            if dist > 0.5:
                step = min(dist, 6.0) / dist
                p.send(bytes((0,)) + struct.pack('<fff', px + dx * step, py + dy * step, tz))
            base_facing = math.atan2(ty - py, tx - px)
            p.send(bytes((1,)) + struct.pack('<fff', math.cos(base_facing),
                                             math.sin(base_facing), 0.0))

        if p.spawned and p.vr_ok and now >= next_pose:
            next_pose = now + 0.05
            t = now - start
            # looks slowly left and right
            facing = base_facing + 0.8 * math.sin(t * 0.4)
            head_q = yaw_quat(facing, 0.25 * math.sin(t * 0.7))

            def around(forward, right, down):
                c, s = math.cos(facing), math.sin(facing)
                return (forward * c - right * s, forward * s + right * c, down)

            aim_pitch = 0.3 * math.sin(t * 1.3)
            right_hand = around(0.55, 0.3, 0.45 - 0.2 * math.sin(t * 1.3))
            circle = (0.5 + 0.15 * math.cos(t * 2), -0.35, 0.3 + 0.15 * math.sin(t * 2))
            left_hand = around(*circle)
            body = p.position or (0.0, 0.0, 0.0)
            payload = pack_pose(FLAG_LEFT | FLAG_RIGHT, (0.0, 0.0, 0.0), head_q,
                                left_hand, yaw_quat(facing - 0.6, 0.4),
                                right_hand, yaw_quat(facing, aim_pitch), body,
                                buttons=1 if int(t) % 3 == 0 else 0)
            p.send(bytes((PACKET, SUB_POSE)) + payload, reliable=False)
        time.sleep(0.005)


if __name__ == '__main__':
    main()
