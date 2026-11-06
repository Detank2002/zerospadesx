"""
VR presence: lets players in a VR headset (ZeroSpades VR, ``--vr``) be seen as
VR players, with their tracked head and hands, by everyone on the server who uses
a client that understands it - in a headset or on a monitor.

A VR client sends its head and hands up to 30 times a second as protocol
extension 58 (packet 122). This script advertises the extension, checks each
pose, and relays it to every other client that negotiated the extension.
Clients that did not (stock OpenSpades, BetterSpades, the Voxlap client) never
see a single extra byte, and a VR player still plays on a stock server; they are
simply drawn with the ordinary animation there.

It also lets room-scale walking reach the server. A VR player who physically
walks moves their body without pressing any keys, and PositionData is only
accepted once every 0.7s, so on a stock server other players see them hop every
second. With ``body_sync`` on, the position in each pose moves the body, a small
step at a time and never into a wall.

The packet format is documented in the client's ``Sources/Client/VRNetProtocol.h``
and ``VR.md``; ``Server/test_vr_presence.py`` runs this script on a real
piqueserver and checks it byte for byte.

Commands
^^^^^^^^

* ``/vr`` lists the players currently in VR

Options
^^^^^^^

.. code-block:: toml

    [vr_presence]
    # Poses relayed per second per VR player; extra ones are dropped.
    max_rate = 30
    # Move a VR player's body to the position in their poses (room-scale).
    body_sync = true
    # The furthest a single pose may move the body, in blocks.
    max_step = 0.6
    # Hands and head further than this from the eye are refused, in blocks.
    max_reach = 4.0
"""

import math
import struct
import time

from pyspades.loaders import Loader
from pyspades.packet import register_packet, register_packet_handler
from piqueserver.commands import command
from piqueserver.config import config

EXTENSION_ID = 58
EXTENSION_VERSION = 1
PACKET_ID = 64 + EXTENSION_ID  # 122, the same convention as Teamplay's 112

SUB_POSE = 0
SUB_CONFIG = 1

# flags u8, head/left/right (3 x i16 position + 4 x i16 rotation), body 3 x f32,
# buttons u8
POSE_PAYLOAD_BYTES = 56
POSE_FLAG_BODY = 1 << 2
CONFIG_BODY_SYNC = 1 << 0

# Byte offsets of the three positions inside the payload.
POSE_POSITION_OFFSETS = (1, 15, 29)
BODY_OFFSET = 43

section = config.section('vr_presence')
MAX_RATE = section.option('max_rate', 30)
BODY_SYNC = section.option('body_sync', True)
MAX_STEP = section.option('max_step', 0.6)
MAX_REACH = section.option('max_reach', 4.0)

# The body box the client's room-scale checks too (Player::VRPushBody): 0.45
# either side, heights below the eye from the top of the head down to a block
# above the feet. A resting body's feet sit anywhere inside the top block of
# the ground, so a sample nearer the feet than 1.05 can land in that block and
# call open ground a wall; 1.2 below the eye never does, and still meets a
# one-block step. Crouched, the feet are 0.9 nearer the eye.
BODY_HALF_WIDTH = 0.45
BODY_LEVELS = (-0.35, 0.45, 1.2)
BODY_LEVELS_CROUCHED = (-0.35, 0.3)


class VRPresencePacket(Loader):
    """Packet 122. Byte 1 is the sub packet; the rest is kept as bytes, since
    the server reads only what it checks and relays everything else untouched.

    Client to server a pose is ``[122][0][payload]``; server to client it is
    ``[122][0][player id][payload]``. A config is ``[122][1][flags][max rate]``.
    """
    id = PACKET_ID

    def __init__(self, reader=None):
        self.sub = SUB_POSE
        self.player_id = None
        self.data = b''
        Loader.__init__(self, reader)

    def read(self, reader):
        self.sub = reader.readByte(True)
        self.data = reader.read()

    def write(self, writer):
        writer.writeByte(self.id, True)
        writer.writeByte(self.sub, True)
        if self.player_id is not None:
            writer.writeByte(self.player_id, True)
        writer.write(self.data)


register_packet(VRPresencePacket)


@register_packet_handler(VRPresencePacket)
def _on_vr_presence(connection, contained):
    handler = getattr(connection, 'on_vr_presence_packet', None)
    if handler is not None:
        handler(contained)


def pose_is_plausible(payload, max_reach):
    """Head and hands within reach of the eye, and a finite body position.
    Anything else is a broken or doctored client and is not relayed."""
    for offset in POSE_POSITION_OFFSETS:
        x, y, z = struct.unpack_from('<hhh', payload, offset)
        if max(abs(x), abs(y), abs(z)) / 512.0 > max_reach:
            return False
    body = struct.unpack_from('<fff', payload, BODY_OFFSET)
    return all(math.isfinite(v) for v in body)


def body_is_clear(game_map, x, y, z, crouched=False):
    """Whether the body fits at (x, y) with its eye at z."""
    for dz in (BODY_LEVELS_CROUCHED if crouched else BODY_LEVELS):
        for ox in (-BODY_HALF_WIDTH, BODY_HALF_WIDTH):
            for oy in (-BODY_HALF_WIDTH, BODY_HALF_WIDTH):
                bx, by, bz = int(math.floor(x + ox)), int(math.floor(y + oy)), \
                    int(math.floor(z + dz))
                if not (0 <= bx < 512 and 0 <= by < 512):
                    return False
                if bz < 0:
                    continue  # above the top of the map is open air
                if game_map.get_solid(bx, by, bz):
                    return False
    return True


@command('vr')
def vr_command(connection):
    """
    List the players currently in VR
    /vr
    """
    names = [p.name for p in connection.protocol.players.values()
             if getattr(p, 'vr_active', False)]
    if not names:
        return 'Nobody is in VR right now.'
    return 'In VR: ' + ', '.join(names)


def apply_script(protocol, connection, config):
    class VRPresenceProtocol(protocol):
        def __init__(self, *args, **kwargs):
            protocol.__init__(self, *args, **kwargs)
            ext = (EXTENSION_ID, EXTENSION_VERSION)
            if ext not in self.available_proto_extensions:
                self.available_proto_extensions.append(ext)

    class VRPresenceConnection(connection):
        vr_last_relay = 0.0
        vr_last_seen = None
        vr_config_sent = False

        @property
        def vr_active(self):
            return (self.vr_last_seen is not None and
                    time.monotonic() - self.vr_last_seen < 1.0)

        def supports_vr_presence(self):
            return EXTENSION_ID in self.proto_extensions

        def send_vr_config(self):
            if self.vr_config_sent or not self.supports_vr_presence():
                return
            self.vr_config_sent = True
            packet = VRPresencePacket()
            packet.sub = SUB_CONFIG
            flags = CONFIG_BODY_SYNC if BODY_SYNC.get() else 0
            packet.data = bytes((flags, max(1, min(255, int(MAX_RATE.get())))))
            self.send_contained(packet)

        def on_spawn(self, pos):
            connection.on_spawn(self, pos)
            self.send_vr_config()

        def on_vr_presence_packet(self, contained):
            # Only from a client that agreed to the extension, and only poses:
            # a config is the server's to send.
            if contained.sub != SUB_POSE or not self.supports_vr_presence():
                return
            if self.world_object is None or not self.hp or self.team is None \
                    or self.team.spectator:
                return
            if len(contained.data) < POSE_PAYLOAD_BYTES:
                return

            now = time.monotonic()
            interval = 1.0 / max(1, int(MAX_RATE.get()))
            # A little slack, so a client pacing itself at exactly the limit
            # does not lose every other pose to timer jitter.
            if now - self.vr_last_relay < interval * 0.8:
                return

            payload = bytes(contained.data[:POSE_PAYLOAD_BYTES])
            if not pose_is_plausible(payload, float(MAX_REACH.get())):
                return
            self.vr_last_relay = now
            self.vr_last_seen = now
            self.send_vr_config()

            if BODY_SYNC.get() and payload[0] & POSE_FLAG_BODY:
                self.vr_sync_body(payload)

            relay = VRPresencePacket()
            relay.sub = SUB_POSE
            relay.player_id = self.player_id
            relay.data = payload
            self.protocol.broadcast_contained(
                relay, unsequenced=True, sender=self,
                rule=lambda player: EXTENSION_ID in player.proto_extensions)

        def vr_sync_body(self, payload):
            x, y, _ = struct.unpack_from('<fff', payload, BODY_OFFSET)
            position = self.world_object.position
            # Sideways only: height is the server's physics (gravity, steps,
            # jumps), which the client agrees with anyway.
            step = math.hypot(x - position.x, y - position.y)
            if step < 0.005 or step > float(MAX_STEP.get()):
                return
            if not body_is_clear(self.protocol.map, x, y, position.z,
                                 self.world_object.crouch):
                return
            self.world_object.set_position(x, y, position.z)

    return VRPresenceProtocol, VRPresenceConnection
