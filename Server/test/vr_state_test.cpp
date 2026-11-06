// Checks the pure VR geometry in Sources/Client/VRState.cpp. From the repository
// root:
//   g++ -std=c++17 -I Sources Server/test/vr_state_test.cpp Sources/Client/VRState.cpp
//
// VRPlaceWristPanel: the panel is up when the back of the wrist faces the eyes,
// and down (the controls) when the same hand holds a foregrip or turns its palm
// to the eyes.
// VRWristReached / VRRayHitsWristPanel: the other hand at the panel opens the
// menu (not from across the room: the control), and its ray lands where it
// points on the panel's face, not from behind.
// VRToolPushback: a hand and a tool stop at a wall, and nothing moves in the open
// (the control).
// VRAimFilter: a hand that turns is followed (0.1.7 froze: the gun stayed put),
// a still hand's tremor is cut down, and 0 passes the aim through (control).
// VRHolsterAt: each tool is where it is carried, nothing is where you aim (the
// control), the slots turn with the head, and a left hand gets the mirror image.
// VRGripUpdate: a holster counts only where the grip begins; a grip begun as
// the tool's action stays that through a holster (testers lost lines of blocks
// and digs to the hip), with the old behaviour (any frame at a holster) as the
// control. VRGripMayHolster: never two-handed, nor with a block in reach.
// VRStockUpdate: the stock takes hold only right at the shoulder with the gun
// already along its line, and eases in; the old rule (within 22cm, at once) is
// the control: a hand passing 18cm from the shoulder, 20 degrees off, took it.
// VRRayHitsHudPanel: a panel standing in the room is hit where it stands, and
// not where a head-locked one would be (the control).
#include <Client/VRState.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace spades;
using namespace spades::client;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// A grip pose at `pos` whose back-of-hand direction is `back` (for `hand`),
// with +Z (towards the wrist) as close to `wristward` as it can be.
static VRPose Grip(int hand, Vector3 pos, Vector3 back, Vector3 wristward) {
	back = back.Normalize();
	Vector3 x = hand == VRHandLeft ? back * -1.0F : back;
	Vector3 z = (wristward - x * Vector3::Dot(wristward, x)).Normalize();
	Vector3 y = Vector3::Cross(z, x);
	VRPose p;
	p.position = pos;
	p.orientation = VRQuat::FromAxes(x, y, z);
	p.valid = true;
	return p;
}

int main() {
	VRFrameState fs;
	fs.head.position = MakeVector3(0, 1.2F, 0);
	fs.head.valid = true; // identity: looking down -Z

	for (int hand = 0; hand < 2; hand++) {
		VRControllerState& c = fs.hands[hand];
		c.active = true;
		Vector3 pos = MakeVector3(0, 0.95F, -0.35F);
		Vector3 toEyes = (fs.head.position - pos).Normalize();

		// Looking at a watch: the back of the wrist towards the eyes.
		c.grip = Grip(hand, pos, toEyes, MakeVector3(hand == 0 ? -1.0F : 1.0F, 0, 0));
		VRPose panel;
		CHECK(VRPlaceWristPanel(fs, hand, false, panel));
		// The panel's +Z faces the eyes, and its up is the head's up.
		Vector3 pz = panel.orientation.Rotate(MakeVector3(0, 0, 1));
		Vector3 pe = (fs.head.position - panel.position).Normalize();
		CHECK(Vector3::Dot(pz, pe) > 0.999F);
		CHECK(panel.orientation.Rotate(MakeVector3(0, 1, 0)).y > 0.8F);
		CHECK(std::fabs(panel.orientation.Rotate(MakeVector3(1, 0, 0)).y) < 1.0E-3F);

		// Control: the same hand on a foregrip, back of the hand to the side.
		c.grip = Grip(hand, pos, MakeVector3(hand == 0 ? -1.0F : 1.0F, 0, 0),
		              MakeVector3(0, -1, 0.3F));
		CHECK(!VRPlaceWristPanel(fs, hand, false, panel));
		CHECK(!VRPlaceWristPanel(fs, hand, true, panel));

		// Control: palm towards the eyes is not the back of the wrist.
		c.grip = Grip(hand, pos, toEyes * -1.0F, MakeVector3(1, 0, 0));
		CHECK(!VRPlaceWristPanel(fs, hand, true, panel));

		// Hysteresis: half-turned (about 60 degrees off) stays up once up, but
		// does not come up.
		Vector3 side = Vector3::Cross(toEyes, MakeVector3(0, 1, 0)).Normalize();
		Vector3 half = (toEyes * 0.45F + side * 0.89F).Normalize();
		c.grip = Grip(hand, pos, half, MakeVector3(0, -1, 0));
		CHECK(!VRPlaceWristPanel(fs, hand, false, panel));
		CHECK(VRPlaceWristPanel(fs, hand, true, panel));

		// Out of view: the wrist behind the head.
		Vector3 behind = MakeVector3(0, 1.1F, 0.4F);
		c.grip = Grip(hand, behind, (fs.head.position - behind).Normalize(),
		              MakeVector3(1, 0, 0));
		CHECK(!VRPlaceWristPanel(fs, hand, true, panel));

		// Arm's length: too far.
		Vector3 far = MakeVector3(0, 1.0F, -1.2F);
		c.grip = Grip(hand, far, (fs.head.position - far).Normalize(), MakeVector3(1, 0, 0));
		CHECK(!VRPlaceWristPanel(fs, hand, true, panel));

		c.active = false;
		c.grip = Grip(hand, pos, toEyes, MakeVector3(1, 0, 0));
		CHECK(!VRPlaceWristPanel(fs, hand, true, panel));
	}

	// ---- Holsters ----
	{
		VRFrameState hs;
		hs.head.position = MakeVector3(0, 1.6F, 0);
		hs.head.valid = true; // identity: looking down -Z, right is +X
		auto at = [&](int hand, Vector3 pos, int hover = VRHolsterNone) {
			VRControllerState& c = hs.hands[hand];
			c.active = true;
			c.grip.position = pos;
			c.grip.valid = true;
			return VRHolsterAt(hs, hand, hover);
		};
		const Vector3 h = hs.head.position;
		// Right hand (a right-handed player's gun hand).
		CHECK(at(VRHandRight, h + MakeVector3(0.20F, -0.10F, 0.15F)) == VRHolsterGun);
		CHECK(at(VRHandRight, h + MakeVector3(-0.20F, -0.10F, 0.15F)) == VRHolsterSpade);
		CHECK(at(VRHandRight, h + MakeVector3(0.22F, -0.60F, 0.0F)) == VRHolsterBlocks);
		CHECK(at(VRHandRight, h + MakeVector3(-0.22F, -0.60F, 0.0F)) == VRHolsterGrenades);
		// Controls: where a gun is held and aimed, and at arm's length.
		CHECK(at(VRHandRight, h + MakeVector3(0.15F, -0.30F, -0.35F)) == VRHolsterNone);
		CHECK(at(VRHandRight, h + MakeVector3(0.05F, -0.05F, -0.45F)) == VRHolsterNone);
		CHECK(at(VRHandRight, h + MakeVector3(0.60F, -0.40F, -0.20F)) == VRHolsterNone);
		// A left hand is the mirror image: its own shoulder is on the left.
		CHECK(at(VRHandLeft, h + MakeVector3(-0.20F, -0.10F, 0.15F)) == VRHolsterGun);
		CHECK(at(VRHandLeft, h + MakeVector3(0.22F, -0.60F, 0.0F)) == VRHolsterGrenades);
		// Hysteresis: 1.1 radii out is outside a slot, unless the hand was in it.
		const Vector3 gunSlot = h + MakeVector3(0.20F, -0.10F, 0.15F);
		const Vector3 justOut = gunSlot + MakeVector3(0.0F, 0.18F * 1.1F, 0.0F);
		CHECK(at(VRHandRight, justOut) == VRHolsterNone);
		CHECK(at(VRHandRight, justOut, VRHolsterGun) == VRHolsterGun);
		// Turned 90 degrees to the right (now looking down +X, right is +Z): the
		// slots turn with the head, and the old place is nothing.
		hs.head.orientation = VRQuat::FromAxisAngle(MakeVector3(0, 1, 0), -1.5707963F);
		CHECK(at(VRHandRight, h + MakeVector3(-0.15F, -0.10F, 0.20F)) == VRHolsterGun);
		CHECK(at(VRHandRight, h + MakeVector3(0.20F, -0.10F, 0.15F)) == VRHolsterNone);
		// Looking straight down gives no heading, and no holsters.
		hs.head.orientation = VRQuat::FromAxisAngle(MakeVector3(1, 0, 0), -1.5707963F);
		CHECK(at(VRHandRight, h + MakeVector3(0.20F, -0.10F, 0.15F)) == VRHolsterNone);
	}

	// ---- Wrist menu ----
	{
		VRFrameState ws;
		ws.head.position = MakeVector3(0, 1.6F, 0);
		ws.head.valid = true;
		ws.wristShown = true;
		ws.wristWidth = 0.24F;
		ws.wristPanel.position = MakeVector3(0, 1.3F, -0.35F);
		ws.wristPanel.valid = true; // identity: facing +Z, towards the eyes
		VRControllerState& c = ws.hands[VRHandRight];
		c.grip.valid = c.aim.valid = true;
		// The other hand 15cm off the panel opens it; 30cm keeps it open only
		// if it was open; across the body at 60cm is nothing (control).
		c.grip.position = ws.wristPanel.position + MakeVector3(0.1F, 0.0F, 0.11F);
		CHECK(VRWristReached(ws, VRHandRight, false));
		c.grip.position = ws.wristPanel.position + MakeVector3(0.3F, 0.0F, 0.0F);
		CHECK(!VRWristReached(ws, VRHandRight, false));
		CHECK(VRWristReached(ws, VRHandRight, true));
		c.grip.position = ws.wristPanel.position + MakeVector3(0.6F, 0.0F, 0.0F);
		CHECK(!VRWristReached(ws, VRHandRight, true));
		ws.wristShown = false;
		c.grip.position = ws.wristPanel.position;
		CHECK(!VRWristReached(ws, VRHandRight, true));
		ws.wristShown = true;

		// Pointing straight at it (aim down -Z) from 10cm in front of its
		// upper-left quarter.
		Vector2 uv;
		float d;
		c.aim.orientation = VRQuat();
		c.aim.position = ws.wristPanel.position + MakeVector3(-0.06F, 0.0375F, 0.1F);
		CHECK(VRRayHitsWristPanel(ws, VRHandRight, uv, d));
		CHECK(std::fabs(uv.x - 0.25F) < 1.0E-3F && std::fabs(uv.y - 0.25F) < 1.0E-3F);
		CHECK(std::fabs(d - 0.1F) < 1.0E-4F);
		// From behind the panel: nothing (control).
		c.aim.position = ws.wristPanel.position + MakeVector3(0.0F, 0.0F, -0.1F);
		CHECK(!VRRayHitsWristPanel(ws, VRHandRight, uv, d));
		// Beside it: a miss.
		c.aim.position = ws.wristPanel.position + MakeVector3(0.3F, 0.0F, 0.1F);
		CHECK(!VRRayHitsWristPanel(ws, VRHandRight, uv, d));
	}

	// ---- Solid tools ----
	{
		// A wall filling x >= 2.
		auto wall = [](const Vector3& v) { return v.x >= 2.0F; };
		auto open = [](const Vector3&) { return false; };
		const Vector3 eye = MakeVector3(0, 0, 0), fwd = MakeVector3(1, 0, 0);
		// In the open, nothing moves (control).
		Vector3 push = VRToolPushback(eye, MakeVector3(1.5F, 0, 0), fwd, 0.85F, 0.03F, open);
		CHECK(push.GetLength() < 1.0E-5F);
		// Clear of the wall, nothing moves either.
		push = VRToolPushback(eye, MakeVector3(1.0F, 0, 0), fwd, 0.85F, 0.03F, wall);
		CHECK(push.GetLength() < 1.0E-5F);
		// A gun at 1.5 reaching 0.85 would poke 0.35 into the wall: it slides
		// back so its tip is short of the wall.
		push = VRToolPushback(eye, MakeVector3(1.5F, 0, 0), fwd, 0.85F, 0.03F, wall);
		const float tip = 1.5F + push.x + 0.85F;
		CHECK(push.x < -0.3F && tip < 2.0F && tip > 1.85F);
		CHECK(std::fabs(push.y) < 1.0E-5F && std::fabs(push.z) < 1.0E-5F);
		// A hand through the wall comes back to this side of it.
		push = VRToolPushback(eye, MakeVector3(2.5F, 0, 0), fwd, 0.0F, 0.03F, wall);
		CHECK(2.5F + push.x < 2.0F && 2.5F + push.x > 1.85F);
		// A gun held along the wall (pointing sideways) does not slide.
		push = VRToolPushback(eye, MakeVector3(1.5F, 0, 0), MakeVector3(0, 1, 0), 0.85F, 0.03F,
		                      wall);
		CHECK(push.GetLength() < 1.0E-5F);
	}

	// ---- Aim smoothing ----
	{
		const float dt = 1.0F / 90.0F;
		auto dir = [](float yaw) { return MakeVector3(std::cos(yaw), std::sin(yaw), 0.0F); };
		auto angle = [](Vector3 a, Vector3 b) {
			float d = Vector3::Dot(a, b);
			return std::acos(d > 1.0F ? 1.0F : d < -1.0F ? -1.0F : d);
		};
		// A hand turning 90 degrees over a second, then held: the gun follows
		// it closely on the way and is there within a quarter second after.
		VRAimFilter f;
		Vector3 out;
		float worstLag = 0.0F;
		for (int i = 0; i <= 90; i++) {
			out = f.Step(dir(1.5707963F * i / 90.0F), dt, 0.5F);
			worstLag = std::max(worstLag, angle(out, dir(1.5707963F * i / 90.0F)));
		}
		CHECK(worstLag < 0.1F); // under 6 degrees behind while turning
		for (int i = 0; i < 23; i++)
			out = f.Step(dir(1.5707963F), dt, 0.5F);
		CHECK(angle(out, dir(1.5707963F)) < 0.01F);
		// A second turn, the other way: the gun is not stuck where it was.
		for (int i = 0; i < 45; i++)
			out = f.Step(dir(0.0F), dt, 0.5F);
		CHECK(angle(out, dir(0.0F)) < 0.01F);

		// A still hand shaking half a degree either way, every frame: the gun
		// shakes far less.
		VRAimFilter g;
		float maxDev = 0.0F;
		for (int i = 0; i < 180; i++) {
			out = g.Step(dir((i % 2 ? 1.0F : -1.0F) * 0.0087F), dt, 0.5F);
			if (i > 60)
				maxDev = std::max(maxDev, angle(out, dir(0.0F)));
		}
		CHECK(maxDev < 0.0087F * 0.5F);
		// Control: smoothing off passes every frame's aim straight through.
		VRAimFilter h;
		out = h.Step(dir(0.3F), dt, 0.0F);
		CHECK(angle(out, dir(0.3F)) < 1.0E-5F && !h.IsActive());
	}

	// VRRecoil: the monitor's per-shot recoil, as a turn of the gun.
	{
		const float rifle = 0.05F, smg = 0.0125F;
		const float dt = 1.0F / 90.0F;
		auto run = [&](VRRecoil& r, float seconds) {
			for (int i = 0; i < (int)(seconds / dt); i++)
				r.Step(dt);
		};

		// A rifle shot: the full turn at once, the snap gone in a fifth of a
		// second, most of the rest by the next shot (0.5 s), all but a sliver
		// soon after, and never below where it was aimed.
		VRRecoil r;
		r.Kick(rifle, 0.0F);
		CHECK(std::fabs(r.Pitch() - rifle) < 1.0E-6F);
		float lowest = 1.0F;
		for (int i = 0; i < 18; i++) {
			r.Step(dt);
			lowest = std::min(lowest, r.Pitch());
		}
		CHECK(r.Pitch() < rifle * 0.7F);
		CHECK(lowest > 0.0F);
		run(r, 0.3F);
		CHECK(r.Pitch() < rifle * 0.4F && r.Pitch() > 0.0F);
		run(r, 2.0F);
		CHECK(r.Pitch() < rifle * 0.02F && r.Pitch() >= 0.0F);

		// An SMG held on the trigger, ten shots a second for three seconds,
		// climbs well past one shot's turn, as the view does on a monitor ...
		VRRecoil s;
		for (int shot = 0; shot < 30; shot++) {
			s.Kick(smg, 0.0F);
			run(s, 0.1F);
		}
		CHECK(s.Pitch() > smg * 8.0F);
		// ... and comes back once you stop.
		run(s, 3.0F);
		CHECK(s.Pitch() < smg * 0.5F);
		// Control: the same thirty shots a second apart do not build up.
		VRRecoil t;
		float peak = 0.0F;
		for (int shot = 0; shot < 30; shot++) {
			t.Kick(smg, 0.0F);
			peak = std::max(peak, t.Pitch());
			run(t, 1.0F);
		}
		CHECK(peak < smg * 1.5F);

		// Sideways too, and both ways back.
		VRRecoil y;
		y.Kick(0.0F, -0.02F);
		CHECK(y.Yaw() < -0.019F && std::fabs(y.Pitch()) < 1.0E-6F);
		run(y, 3.0F);
		CHECK(std::fabs(y.Yaw()) < 0.001F);

		// Never softer than a monitor. Player has already doubled a walking
		// shot whose sights are not up; braced (the headset's aiming down the
		// sights) takes exactly that back, so it lands on a monitor's aimed
		// recoil, and one hand keeps the doubling (control). Standing still or
		// with the sights up, bracing changes nothing.
		for (int braced = 0; braced < 2; braced++)
			for (int walking = 0; walking < 2; walking++)
				for (int sights = 0; sights < 2; sights++) {
					const float doubled = (walking && !sights) ? 2.0F : 1.0F; // Player's
					const float total = doubled * VRRecoilHoldScale(braced, walking, sights);
					const float monitorAimed = 1.0F;
					CHECK(total >= monitorAimed);
					if (!braced)
						CHECK(total == doubled); // control: one hand is the monitor unaimed
				}
		CHECK(VRRecoilHoldScale(true, true, false) == 0.5F);
		CHECK(VRRecoilHoldScale(true, false, false) == 1.0F);
	}

	// The comfort vignette closes in for walking, falling and smooth turning,
	// and not at all standing still or creeping (the control): a vignette that
	// shows when nothing moves would just be a smaller view.
	{
		CHECK(VRVignetteTarget(0.0F, 0.0F, 0.0F) == 0.0F);
		CHECK(VRVignetteTarget(0.3F, 0.0F, 0.0F) == 0.0F);
		CHECK(VRVignetteTarget(4.3F, 0.0F, 0.0F) == 1.0F);  // walking
		CHECK(VRVignetteTarget(0.0F, 2.0F, 0.0F) == 0.0F);  // a hop
		CHECK(VRVignetteTarget(0.0F, 15.0F, 0.0F) == 1.0F); // a fall
		CHECK(VRVignetteTarget(0.0F, 0.0F, -2.62F) == 1.0F); // 150 deg/s left
		CHECK(VRVignetteTarget(2.0F, 0.0F, 0.0F) > 0.3F);
		CHECK(VRVignetteTarget(2.0F, 0.0F, 0.0F) < 0.7F);
		// In fast, out slow, never past the target.
		float v = 0.0F;
		for (int i = 0; i < 9; i++)
			v = VRVignetteStep(v, 1.0F, 1.0F / 60.0F);
		CHECK(v > 0.9F && v <= 1.0F);
		for (int i = 0; i < 9; i++)
			v = VRVignetteStep(v, 0.0F, 1.0F / 60.0F);
		CHECK(v > 0.6F);
		for (int i = 0; i < 60; i++)
			v = VRVignetteStep(v, 0.0F, 1.0F / 60.0F);
		CHECK(v == 0.0F);
		// No amount or no strength leaves the view open; both close it.
		const float open = VRVignetteClearAngle(0.0F, 1.0F);
		CHECK(VRVignetteClearAngle(1.0F, 0.0F) == open);
		CHECK(open > 1.0F);
		CHECK(VRVignetteClearAngle(1.0F, 1.0F) < 0.4F);
		CHECK(VRVignetteClearAngle(1.0F, 0.5F) > VRVignetteClearAngle(1.0F, 1.0F));
	}

	// Rumble shapes: a shot's kick starts at its amplitude and dies away, a
	// magazine's double tap has two knocks with nothing between, and a flat
	// rumble (the control, and what a runtime without envelopes plays) is the
	// same all through. The count follows the length, within its limits.
	{
		float e[4000];
		int n = VRHapticEnvelope(VRHapticShape::Kick, 0.8F, 0.06F, e, 4000);
		CHECK(n == 30);
		CHECK(e[0] == 0.8F);
		bool falling = true;
		for (int i = 1; i < n; i++)
			falling &= e[i] < e[i - 1];
		CHECK(falling);
		CHECK(e[n - 1] < 0.8F * 0.1F);
		n = VRHapticEnvelope(VRHapticShape::DoubleTap, 0.7F, 0.08F, e, 4000);
		CHECK(n == 40);
		CHECK(e[0] == 0.7F && e[n - 1] == 0.7F && e[n / 2] == 0.0F);
		n = VRHapticEnvelope(VRHapticShape::Flat, 0.5F, 0.04F, e, 4000);
		bool flat = true;
		for (int i = 0; i < n; i++)
			flat &= e[i] == 0.5F;
		CHECK(flat && n == 20);
		CHECK(VRHapticEnvelope(VRHapticShape::Kick, 2.0F, 0.001F, e, 4000) == 2);
		CHECK(e[0] == 1.0F); // amplitude clamped
		CHECK(VRHapticEnvelope(VRHapticShape::Flat, 1.0F, 100.0F, e, 4000) == 4000);
	}

	// An input trace line reads back as the frame it was written from, and a
	// short or garbled line is refused without touching the frame (the
	// control): a replay must not half-apply a line.
	{
		VRFrameState a;
		a.head.position = MakeVector3(0.1F, 1.62F, -0.25F);
		a.head.orientation = VRQuat(0.0F, 0.3826834F, 0.0F, 0.9238795F);
		a.head.valid = true;
		a.eyes[1].position = MakeVector3(0.13F, 1.62F, -0.25F);
		a.hands[1].active = true;
		a.hands[1].grip.position = MakeVector3(0.2F, 1.1F, -0.4F);
		a.hands[1].grip.valid = true;
		a.hands[1].trigger = 0.75F;
		a.hands[1].thumbstick = MakeVector2(-0.5F, 1.0F);
		a.hands[1].buttonSecondary = true;
		a.hands[0].linearVelocity = MakeVector3(1.5F, -2.0F, 0.25F);
		a.hands[0].velocityValid = true;
		const std::string line = VRTraceFormat(a);
		VRFrameState b;
		CHECK(VRTraceParse(line, b));
		CHECK(VRTraceFormat(b) == line);
		CHECK(b.head.valid && b.head.position.y == 1.62F);
		CHECK(std::fabs(b.head.orientation.y - 0.3826834F) < 1e-6F);
		CHECK(b.hands[1].active && b.hands[1].trigger == 0.75F && b.hands[1].buttonSecondary);
		CHECK(!b.hands[1].buttonPrimary && !b.hands[0].active);
		CHECK(b.hands[0].velocityValid && b.hands[0].linearVelocity.y == -2.0F);
		VRFrameState c;
		c.hands[0].trigger = 0.5F;
		CHECK(!VRTraceParse(line.substr(0, line.size() / 2), c));
		CHECK(!VRTraceParse("not a trace", c));
		CHECK(c.hands[0].trigger == 0.5F && !c.head.valid);
	}

	// The held map: above the hand that holds it, facing the eyes, level; it
	// follows the hand (moved hand, moved map) and needs a hand (the control).
	{
		VRFrameState f;
		f.head.position = MakeVector3(0, 1.6F, 0);
		f.head.valid = true;
		f.hands[0].grip.position = MakeVector3(-0.15F, 1.2F, -0.4F);
		f.hands[0].grip.valid = true;
		VRPose m;
		CHECK(VRPlaceHeldMap(f, 0, m));
		CHECK(m.position.y > 1.2F + 0.1F && m.position.y < 1.2F + 0.15F);
		const Vector3 face = m.orientation.Rotate(MakeVector3(0, 0, 1));
		CHECK(Vector3::Dot(face, (f.head.position - m.position).Normalize()) > 0.999F);
		CHECK(std::fabs(m.orientation.Rotate(MakeVector3(1, 0, 0)).y) < 1e-4F); // level
		VRPose m2;
		f.hands[0].grip.position.x += 0.2F;
		CHECK(VRPlaceHeldMap(f, 0, m2) && m2.position.x > m.position.x + 0.15F);
		CHECK(!VRPlaceHeldMap(f, 1, m2)); // the other hand has no pose
	}

	// The grip and the holsters.
	{
		// A grip pressed away from the holsters is the tool's action, and stays it
		// as the hand goes through the blocks' holster on the way.
		VRGripState g;
		CHECK(VRGripUpdate(g, true, VRHolsterNone) == VRHolsterNone && g.action);
		CHECK(!VRGripMayHolster(g, false, false)); // the caller looks no further
		CHECK(VRGripUpdate(g, true, VRHolsterNone) == VRHolsterNone && g.action);
		CHECK(VRGripUpdate(g, false, VRHolsterNone) == VRHolsterNone && !g.action);
		// Control, the old rule: any frame the hand was at a holster, the grip was
		// not the action, so the line stopped there.
		{
			const int slotsOnTheWay[4] = {VRHolsterNone, VRHolsterBlocks, VRHolsterBlocks,
			                              VRHolsterNone};
			bool oldLineHeld = true, newLineHeld = true;
			VRGripState n;
			for (int i = 0; i < 4; i++) {
				oldLineHeld = oldLineHeld && slotsOnTheWay[i] < 0;
				// The caller passes no slot while the grip is the action.
				const int slot = VRGripMayHolster(n, false, false) ? slotsOnTheWay[i] : VRHolsterNone;
				CHECK(VRGripUpdate(n, true, slot) == VRHolsterNone);
				newLineHeld = newLineHeld && n.action;
			}
			CHECK(!oldLineHeld);
			CHECK(newLineHeld);
		}
		// A grip pressed at a holster takes its tool once, and is not an action.
		VRGripState h;
		CHECK(VRGripUpdate(h, true, VRHolsterSpade) == VRHolsterSpade && !h.action);
		CHECK(VRGripUpdate(h, true, VRHolsterSpade) == VRHolsterNone); // not again while held
		CHECK(VRGripUpdate(h, false, VRHolsterNone) == VRHolsterNone && !h.fromHolster);
		// No holster with the gun in both hands, nor while a block is in reach.
		VRGripState idle;
		CHECK(VRGripMayHolster(idle, false, false));
		CHECK(!VRGripMayHolster(idle, true, false));
		CHECK(!VRGripMayHolster(idle, false, true));
	}

	// The shoulder stock.
	{
		const float deg = 0.0174532925F, dt = 1.0F / 72.0F;
		// A two-handed rifle moved past the shoulder, 18cm off, its line 20
		// degrees from the stock's: the old rule took hold (and tilted the gun
		// 20 degrees at once); the new one does not.
		const float oldRuleHolds = 0.18F < 0.22F;
		CHECK(oldRuleHolds);
		VRStockState s;
		CHECK(!VRStockUpdate(s, true, 0.18F, 20.0F * deg, dt) && !s.on && s.blend == 0.0F);
		// Right at the shoulder but 20 degrees off: still no.
		CHECK(!VRStockUpdate(s, true, 0.05F, 20.0F * deg, dt) && !s.on);
		// At the shoulder and lined up: it takes hold, once, and eases in.
		CHECK(VRStockUpdate(s, true, 0.05F, 3.0F * deg, dt) && s.on);
		CHECK(s.blend > 0.0F && s.blend < 0.2F);
		for (int i = 0; i < 20; i++)
			CHECK(!VRStockUpdate(s, true, 0.08F, 8.0F * deg, dt)); // held: no second buzz
		CHECK(s.on && s.blend == 1.0F);
		// Leaving: further than 20cm lets go, and it eases back out.
		VRStockUpdate(s, true, 0.25F, 3.0F * deg, dt);
		CHECK(!s.on && s.blend < 1.0F && s.blend > 0.8F);
		// A rifle brought to the shoulder as testers hold it, 14cm off and 10
		// degrees from the stock's line, takes hold. Control, the rule before
		// (12cm, 6 degrees): it refused that, and testers thought the stock gone.
		{
			const bool previousRuleHolds = 0.14F < 0.12F && 10.0F < 6.0F;
			CHECK(!previousRuleHolds);
			VRStockState u;
			CHECK(VRStockUpdate(u, true, 0.14F, 10.0F * deg, dt) && u.on);
		}
		// Not allowed (one hand, or not a rifle): off at once in hold, eased out.
		VRStockState t;
		VRStockUpdate(t, true, 0.05F, 1.0F * deg, dt);
		VRStockUpdate(t, false, 0.05F, 1.0F * deg, dt);
		CHECK(!t.on);
	}

	// The front panel, standing in the room.
	{
		VRFrameState f;
		f.head.valid = true;
		f.head.position = MakeVector3(0, 1.6F, 0);
		f.hudWidth = 1.6F;
		f.hudDistance = 1.5F;
		VRControllerState& c = f.hands[1];
		c.active = true;
		c.aim.valid = true;
		// Pointing straight ahead (-Z) from beside the head.
		c.aim.position = MakeVector3(0.2F, 1.4F, 0);
		const float aspect = 9.0F / 16.0F;
		Vector2 uv;
		float dist = 0;
		// Standing 1.5 m off to the right, turned to face the head (+Z towards
		// it): a turn of -90 degrees about +Y takes -Z to +X.
		f.hudAnchored = true;
		f.hudPanel.valid = true;
		f.hudPanel.position = MakeVector3(1.5F, 1.6F, 0);
		f.hudPanel.orientation =
		  VRQuat::FromAxisAngle(MakeVector3(0, 1, 0), std::atan2(-1.0F, -0.0F));
		CHECK(!VRRayHitsHudPanel(f, 1, aspect, uv, dist)); // pointing ahead, it is to the right
		// Turn the controller to point at it (-Z to +X).
		c.aim.orientation = f.hudPanel.orientation;
		CHECK(VRRayHitsHudPanel(f, 1, aspect, uv, dist));
		CHECK(std::fabs(dist - 1.3F) < 1e-3F);
		CHECK(std::fabs(uv.x - 0.5F) < 1e-3F); // straight in: the middle across
		CHECK(uv.y > 0.5F);                    // 0.2 m low: below the middle
		// Control: head-locked, the panel is before the head, where the turned
		// controller does not point.
		f.hudAnchored = false;
		f.hudPanel.position = MakeVector3(0, 1.6F, -1.5F);
		f.hudPanel.orientation = VRQuat();
		CHECK(!VRRayHitsHudPanel(f, 1, aspect, uv, dist));
		c.aim.orientation = VRQuat();
		CHECK(VRRayHitsHudPanel(f, 1, aspect, uv, dist) && std::fabs(dist - 1.5F) < 1e-3F);
	}

	// VRWalkDither: walked for 5s at 90fps in steps of the server's eight
	// directions (diagonals as fast as straight lines), the path goes where it
	// was asked, within a degree, for every angle; rounded to the nearest
	// direction (the old rule, the control) it went up to 22.5 degrees off.
	{
		float worst = 0.0F, worstRounded = 0.0F;
		for (int deg = -180; deg < 180; deg += 3) {
			const float want = deg * 3.14159265F / 180.0F;
			float lean = 0.0F, hold = 0.0F;
			bool second = false;
			float x = 0.0F, y = 0.0F;
			int switches = 0, last = -1;
			for (int i = 0; i < 450; i++) {
				int d = VRWalkDither(want, 1.0F / 90.0F, lean, hold, second);
				if (d != last && last >= 0)
					switches++;
				last = d;
				x += std::cos(d * 0.78539816F);
				y += std::sin(d * 0.78539816F);
			}
			float off = std::fabs(std::remainder(std::atan2(y, x) - want, 6.2831853F));
			worst = std::max(worst, off);
			CHECK(switches <= 100); // at most 20 a second
			float rounded = std::round(want / 0.78539816F) * 0.78539816F;
			worstRounded =
			  std::max(worstRounded, std::fabs(std::remainder(rounded - want, 6.2831853F)));
		}
		std::printf("walk: worst %.2f degrees off (rounded: %.1f)\n", worst * 57.29578F,
		            worstRounded * 57.29578F);
		CHECK(worst < 1.0F * 3.14159265F / 180.0F);
		CHECK(worstRounded > 20.0F * 3.14159265F / 180.0F); // the control
	}

	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
