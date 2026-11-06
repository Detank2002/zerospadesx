// Encodes one known pose with the client's own VRNetProtocol.h and prints it as
// hex, so test_vr_presence.py can check the Python side reads the same bytes.
// With `--decode HEX` it reads a pose payload the other way, as a client does
// when a VR player's pose arrives, and prints what it got (test_vr_puppet.py).
//
//   g++ -std=c++17 -I Sources Server/test/vr_pose_fixture.cpp Sources/Client/VRState.cpp

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Client/VRNetProtocol.h>

using namespace spades;
using namespace spades::client;

namespace {
	// `--decode HEX`: the payload (no packet or sub packet id) decoded, one line:
	// flags, head xyz + xyzw, left hand, right hand, body xyz, buttons.
	int Decode(const char* hex) {
		std::vector<char> payload;
		for (std::size_t i = 0; hex[i] && hex[i + 1]; i += 2)
			payload.push_back(static_cast<char>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
		if (payload.size() < kVRPosePayloadBytes) {
			std::printf("short payload: %u bytes\n", static_cast<unsigned>(payload.size()));
			return 1;
		}
		const VRNetPose pose = vrnet::DecodePose(payload.data());
		std::printf("%u", static_cast<unsigned>(pose.flags));
		auto put = [](const Vector3& p, const VRQuat& q) {
			std::printf(" %.6f %.6f %.6f %.6f %.6f %.6f %.6f", p.x, p.y, p.z, q.x, q.y, q.z, q.w);
		};
		put(pose.headOffset, pose.headRotation);
		put(pose.handOffset[0], pose.handRotation[0]);
		put(pose.handOffset[1], pose.handRotation[1]);
		std::printf(" %.6f %.6f %.6f %u\n", pose.body.x, pose.body.y, pose.body.z,
		            static_cast<unsigned>(pose.buttons));
		return 0;
	}
} // namespace

int main(int argc, char** argv) {
	if (argc > 2 && std::strcmp(argv[1], "--decode") == 0)
		return Decode(argv[2]);

	VRNetPose pose;
	pose.flags = VRPoseLeftHand | VRPoseRightHand | VRPoseBody;
	pose.headOffset = MakeVector3(0.25F, -0.5F, 0.125F);
	pose.headRotation = VRQuat(0.0F, 0.0F, 0.70710678F, 0.70710678F); // 90 deg about z
	pose.handOffset[0] = MakeVector3(-0.5F, 0.75F, 0.5F);
	pose.handOffset[1] = MakeVector3(1.0F, 0.25F, 0.375F);
	pose.handRotation[1] = VRQuat(1.0F, 0.0F, 0.0F, 0.0F); // 180 deg about x
	pose.body = MakeVector3(256.5F, 128.25F, 60.0F);
	pose.buttons = VRButtonRightTrigger | VRButtonLeftGrip;

	std::vector<char> out;
	out.push_back(static_cast<char>(kVRPresencePacketType));
	out.push_back(static_cast<char>(VRSubPose));
	vrnet::EncodePose(out, pose);

	for (char c : out)
		std::printf("%02x", static_cast<unsigned char>(c));
	std::printf("\n");

	// And back: the decoder must return what went in, to the packing's precision.
	VRNetPose back = vrnet::DecodePose(out.data() + 2);
	bool ok = back.flags == pose.flags && back.buttons == pose.buttons &&
	          back.body.x == pose.body.x && back.headOffset.y == pose.headOffset.y &&
	          back.handOffset[1].x == pose.handOffset[1].x;
	return ok ? 0 : 1;
}
