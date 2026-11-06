#!/usr/bin/env python3
"""
A VR player's pose as a client without a headset reads it: what a macOS build
(no OpenXR) has to get right to draw other players' arms and guns.

    python3 Server/test/test_vr_puppet.py PATH/TO/vr_pose_fixture

vr_puppet.py's own packing (what a pretend VR player sends) goes through the
client's decoder (VRNetProtocol.h, in vr_pose_fixture --decode), and comes out
as it went in, to the packing's precision. Needs no server and no pyenet; CTest
runs it with the fixture CMake builds.
"""

import math
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vr_puppet  # noqa: E402

FIXTURE = None
POS_STEP = 1.0 / 512  # positions are packed in 1/512ths of a block
UNIT_STEP = 1.0 / 32767


class PuppetPoseTest(unittest.TestCase):
    def decode(self, payload):
        out = subprocess.check_output([FIXTURE, '--decode', payload.hex()]).decode().split()
        flags, rest = int(out[0]), [float(v) for v in out[1:]]
        poses = [(rest[i:i + 3], rest[i + 3:i + 7]) for i in (0, 7, 14)]
        return flags, poses, rest[21:24], int(out[-1])

    def assertNear(self, got, want, step):
        for g, w in zip(got, want):
            self.assertLessEqual(abs(g - w), step, (got, want))

    def test_puppet_pose_reads_back(self):
        head_q = vr_puppet.yaw_quat(0.7, -0.3)
        left_q = vr_puppet.yaw_quat(-1.2, 0.4)
        right_q = vr_puppet.yaw_quat(2.5, 0.1)
        head, left, right = (0.1, -0.2, -1.6), (-0.25, -0.3, -1.1), (0.3, 0.2, -1.2)
        body = (256.5, 300.25, 40.0)
        payload = vr_puppet.pack_pose(
            vr_puppet.FLAG_LEFT | vr_puppet.FLAG_RIGHT, head, head_q, left, left_q, right,
            right_q, body, buttons=0b101)
        flags, poses, got_body, buttons = self.decode(payload)

        self.assertEqual(flags, vr_puppet.FLAG_LEFT | vr_puppet.FLAG_RIGHT)
        self.assertEqual(buttons, 0b101)
        self.assertEqual(got_body, list(body))
        for (pos, q), want_pos, want_q in zip(poses, (head, left, right),
                                              (head_q, left_q, right_q)):
            self.assertNear(pos, want_pos, POS_STEP)
            # The decoder normalises; the same rotation either way round
            sign = 1.0 if sum(a * b for a, b in zip(q, want_q)) >= 0 else -1.0
            self.assertNear([sign * v for v in q], want_q, 4 * UNIT_STEP)
            self.assertAlmostEqual(math.sqrt(sum(v * v for v in q)), 1.0, places=4)

    def test_a_different_pose_reads_differently(self):
        # The control: the decoder is not handing back a fixed pose
        a = vr_puppet.pack_pose(vr_puppet.FLAG_RIGHT, (0, 0, -1.6), vr_puppet.yaw_quat(0.0),
                                (0, 0, 0), (0, 0, 0, 1), (0.3, 0.2, -1.2),
                                vr_puppet.yaw_quat(1.0), (10.0, 20.0, 30.0))
        b = vr_puppet.pack_pose(vr_puppet.FLAG_RIGHT, (0, 0, -1.6), vr_puppet.yaw_quat(0.0),
                                (0, 0, 0), (0, 0, 0, 1), (-0.3, 0.2, -1.2),
                                vr_puppet.yaw_quat(-1.0), (10.0, 20.0, 30.0))
        self.assertNotEqual(self.decode(a)[1][2], self.decode(b)[1][2])


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    FIXTURE = sys.argv.pop(1)
    unittest.main()
