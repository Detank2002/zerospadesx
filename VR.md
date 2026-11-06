# ZeroSpades X: VR

Play ZeroSpades in a VR headset through OpenXR, with tracked head and hands, on
the same servers as everyone else. Players on a monitor using this client see VR
players' real head and hand movement; players on stock clients see an ordinary
player. A small piqueserver script (`Server/vr_presence.py`) passes the head and
hand poses between clients and lets room-scale walking reach the server.

VR is opt in. Without `--vr` (or `vr_enable 1`) this is the ordinary flat client,
on either renderer.

## Running it

1. Start your OpenXR runtime (SteamVR, Oculus/Meta Link, Virtual Desktop, WMR)
   and connect the headset.
2. In the launcher's **Graphics** tab tick **VR Headset** (next to the renderer
   choice) and press Start. VR draws with Vulkan, so ticking it selects Vulkan,
   and picking OpenGL or Software unticks it. The checkbox is greyed out when no
   OpenXR runtime is installed; the launcher's system report says which runtime
   it found.
3. If the headset is not there when the game starts, it says so in
   `SystemMessages.log` and plays on the monitor.

`--vr` on the command line does the same for one run without touching your
saved settings (useful with a server address, which skips the launcher):

```
ZeroSpades --vr aos://1234567890:32887
```

The window on the monitor shows the left eye, or the menu panel when there is
no 3D scene. The window's size makes no difference in the headset: the menus
and HUD have a layout of their own there (`vr_uiWidth`), drawn at twice its
resolution (`vr_uiSupersample`) and filtered down.

## Training

**The training course**, for somebody who has never played: a walled corridor
of stations (`Sources/Training/TrainingCourse.h`). A shooting range with three
targets walking across their lanes (they never shoot back, and are back two
seconds after they fall), a dirt wall to dig through, a ledge too high to jump
to build steps up, a plugged tunnel to blow open with a grenade, and the
finish. Each station is a gate by its shape, so nothing has to be scripted:
you cannot get past it without doing it.

The course is hosted by the game itself: a small 0.75 server
(`Sources/Training/Server.h`) on a thread of the game, on 127.0.0.1, started by
the menu's **Training** button (`Gui/TrainingHost`); the menu connects to it as
to any other server, and it stops when you leave or the game closes. It plays
nothing but the course. The targets are its own players, with no connection
behind them: they walk a lane and fall over, which needs no AI, and a headset
hosting its own game has no CPU to spare for more. While the course is up the
client sends its position 30 times a second instead of once, since the server
takes every one.

A tip for the station you are at is shown as you reach it
(`Client::TrainingTip`): in a headset 2m ahead in each eye, naming the holsters
and the controller; on a monitor at the top of the screen, naming your keys.
The client knows the course by a voxel of a colour no other map has, read from
the same header. It was first put at the bottom of a column, where a `.vxl`
keeps no colour, and the course went unrecognised; it is on the surface now.
Seen on the null headset: the start's tip at the start, the range's on walking
up to it, the targets on their lanes.

## Controls

The tool hand is the right one (`vr_leftHanded 1` swaps them). For players,
[VR-CONTROLS.md](VR-CONTROLS.md) has a cheat sheet, a first game step by step, and
a picture of each controller family (Quest 2, Quest 3, Index, Vive, WMR) with
every control pointed out, plus one of the holsters and the wrist. In a game,
the menu's **Controls** button shows those pictures (`VRControlsWindow`),
starting at the one for the controllers in use (the interaction profile, and
the headset's name to tell Touch Plus from Touch).

| Input | Action |
|---|---|
| Head | look; the server sees your facing |
| Tool hand | aim the gun, spade, block or grenade. The spade, blocks and grenades act from the hand; a gun fires from your eye at what it points at (see "Fair play") |
| Tool trigger | fire, hit, place a block, pull the pin (release throws) |
| Tool grip | dig a tunnel (spade), build a line (blocks); pressed at a holster, take that tool |
| A | jump |
| B | reload (with `vr_reloadGesture`: drop the magazine; B again reloads at once) |
| Tool stick left/right | turn (snap 45 degrees; `vr_snapTurn 0` turns smoothly) |
| Tool stick click | toggle crouch (crouching for real also crouches) |
| Other stick | walk, relative to where you look |
| Other stick click | toggle sprint (`vr_sprintHold`: sprint while held in) |
| X | copy the colour of the block you point at, while holding blocks |
| Y | open and close the big map |
| X / Y with `vr_toolButtons` | previous / next tool, instead of the two above |
| Other grip | on the gun's barrel: hold it in both hands; at your head: the headlamp; elsewhere: scoreboard |
| Other trigger | holding the gun's barrel: brace the gun on your shoulder while held; otherwise open and close the big map |
| B and Y together, or Menu on the wrist menu | Escape: the in-game menu. On a Quest the controllers' own menu buttons belong to the headset and to SteamVR or Virtual Desktop, and never reach the game |
| Both sticks held in for a second | recentre the play space (both controllers buzz); the menu's **Reset Height** does the same |
| Mouse (seated) | turns the body |

**Rumble with a shape** (`VRHapticShape`, `VRHapticEnvelope`). A shot is a
kick: full at once and dying away, harder and longer for the shotgun (1.0 for
90ms) than the rifle (0.85, 70ms) or the SMG (0.5, 35ms), and felt at half
strength in the off hand too when the gun is held in both. A magazine seating
is two short knocks. Where the runtime has XR_FB_haptic_amplitude_envelope (a
Quest) the shape is played, 500 samples a second; elsewhere each is a plain
rumble at its peak for its length, as every rumble was before, so a PC headset
only notices the per-gun strengths. `vr_state_test` checks the shapes, with a
flat rumble as the control.

**The menus stand on a map** (`vr_menuScene`, `MainScreen::DrawVRMenuScene`).
Out of a game the menu panel used to float in an empty void. Now the first time
a menu is seen in a headset, the training server's map generator
(`Training/MapGen.h`) makes a grassland map for a fixed seed on a thread of its own (a headset must not stall a frame),
and the menus stand on it, 4 blocks over the ground at the blue end, looking
down the map. Your head's movement moves the view, and turning is all yours.
Until the map is ready (a second or so) the void shows as before. After a game
the menu's map is put back, since the game swapped its own in. The first try
stood 12 blocks up, where the fog washed everything out. On a Quest,
passthrough is the other choice the plan names; that is for the port.

**The main menu** is a column of buttons, as a game's usually is: Training,
Multiplayer, Demos, Mods, Settings, Credits, Quit, on a monitor and in a
headset alike. Multiplayer, Demos and Mods open the tabbed page they always
were (its first tab, Servers, is now Multiplayer),
with Back (or Escape) to the buttons; Escape on the buttons quits.

**Learning the controls** happens on the training course: the menu's
**Training** button starts it and puts you on it, with a rifle, no team to
pick (the button says "Starting the course..." while it loads), in daylight on
the course's own map. Each station's tip names the controller's buttons and
the holsters, for whichever hand holds the gun, and the settings that change
them (`vr_reloadGesture`, `vr_toolButtons`). There was a first-run tutorial in
ordinary games too, six tips in a row (`vr_tutorial`, `VRTutorialUpdate`); it
was taken out once the course covered all of it, reloading included, since
tips in a real game are in the way.

**The big map in your hand** (`vr_heldMap`, on by default). The off hand's
trigger opens the big map as before, but instead of hanging in front of you it
is held in that hand like a tablet: the wrist panel's image, 32cm wide, its
bottom edge just above the hand and its face turned to your eyes, level with
your head (`VRPlaceHeldMap`). Bring it closer to read it, lower it to see past
it. The wrist menu does not open while it is held. `vr_heldMap 0` hangs it in
front again; so does turning the wrist HUD off, since the map borrows its
image. `vr_state_test` checks it is above the hand, faces the eyes, stays
level and follows the hand.

**Long chat lines wrap** to what they are drawn on (`ChatWindow::Draw`), at a
space where there is one: the wrist panel's 960 pixels in a headset, where a
long line used to run off the edge, and the screen's edge (or the expanded
chat box's) on a monitor, where nothing that fitted before changes. An entry's
height follows the lines it took, so the ones around it make room, and a word
wider than a whole line breaks at the edge. The kill feed wraps the same way.

**Playing seated.** Sit down and hold both sticks in for a second: that
recentres, and the height your head is at becomes your character's standing
eye, so a seated player sees from exactly as high as a standing one (the view
never goes above the character's eye, sitting or standing). Turn on `vr_seated`
(Setup, VR tab: Seated) so leaning forward in the chair does not crouch you;
the tool stick's click still does.

There is no sneak in a headset (the keyboard's V does nothing while it is on).
Y was sneak until 0.3, but in the game sneak only walks at half speed, and
testers took Y for a button that did nothing; it opens the big map now. The
left trigger, held while the left hand holds a gun's barrel, braces the gun on
the shoulder: any gun aims from the shoulder through the front hand at once,
as the rifle's stock does when the gun hand is brought to the shoulder
(testers asked for a stock weld on every gun). With the hand off the barrel the
trigger still opens the map, so controllers without a Y (Vive, WMR) keep it.
With `vr_toolButtons` Y steps through the tools.

Stick clicks count when you let go, and not at all when the other stick went in
too, so the recentre chord never toggles crouch or sprint on the way.

**The laser.** The gun, spade and blocks carry a red laser along the line they
act on, with a dot where it meets the map: shots go exactly where the dot is.
A gun's shot leaves your eye (see "Fair play"), so its laser runs from the top
of the gun's muzzle (0.72 blocks ahead of the grip and 0.14 up, in the gun's
frame: past the scope's front, at the front sight on all three guns; it used
to start at the controller, under the gun) to where that shot lands: on the corner in front of you, if the eye cannot see
what the gun points at. It is amber while the gun reloads, and the gun hand
buzzes when it is done. It is worked out where it is drawn, from the hand the
gun is drawn in (`Client::VRGunShot`, called for the shot and again for the
laser): kept from the game's update, it trailed the gun by however far you had
moved since. It hides while the hand changes tools, since it is the chosen
tool's and the hand still holds the old one, and while the barrel is through a
wall (see "Solid tools"). The muzzle flash and the tracer leave from the same
point: from the skin's own muzzle they came out under the laser, which showed
when the gun was lowered against cover (a tester's report).
The spade's and blocks' laser is short, and its dot turns green when the block
is in reach (blocks reach 4, one further than the stock client's 3, for
monitor players too; servers accept up to 6). Grenades have none: they leave with your hand's swing. Only you
see your laser. `vr_laser 0` turns it off.

The spade swings a little in your hand when it hits (a quick chop and back), and
leans in as a dig builds up, so you can see the tool working. Others see it too.
**Swinging the spade hits**, as the trigger does: a swing faster than
`vr_swingSpeed` (2.5 m/s) is one hit.

**Two hands on a gun.** Put your off hand on the gun's barrel and squeeze its
grip: the gun now points from your gun hand to your off hand, which is steadier
than one wrist. Let go (or pull your hands apart) and it is back in one hand.
The controller ticks when it takes hold.

**Recoil.** Every shot turns the gun up (and a little sideways) by as much as
it turns the view on a monitor, and the shots follow the gun. A rifle snaps up
and settles before you can fire again; an SMG held on the trigger climbs, and
you hold it down by pulling your hand down, as you would the mouse. A gun
braced in two hands or at the shoulder counts as aiming down the sights: it is
spared the doubling for walking, as on a monitor, and nothing more. There is no
setting for it: recoil is the client's own doing, so a softer one would be a
cheat.

**Shoulder stock** (any gun). Holding the gun in both hands, hold the off hand's
trigger: the controller ticks, and the gun aims from your shoulder through your
front hand, like a stock, which is far steadier than two wrists; it eases over
in a fifth of a second (`VRStockUpdate`). Let go of the trigger to unshoulder.
`vr_shoulderStock 0` turns it off.

Until 0.3 the rifle took the stock by itself, with the gun hand brought to the
shoulder (within 15cm) and the gun along the stock's line (within 12 degrees),
and only the rifle had one. Testers found it took hold again, unasked, whenever
the gun came back that way after a brace, and asked for a stock on every gun;
the trigger is now the only way in. (Before that rule the first stock took hold
anywhere within 22cm, at once, and the gun jumped as a rifle came up to the
face; the next, within 12cm and 6 degrees, was so hard to hit that testers
thought it had been removed.) `vr_state_test` still checks `VRStockUpdate`.

**Aim smoothing.** A nearly still gun can be steadied against hand tremor,
which makes long shots easier; a quick swing is not slowed. `vr_aimSmooth` (0
to 1) sets how much. It is off by default: testers wanted the gun and its laser
rigid in the hand, and the smoothing made both float behind it. (It was
`vr_aimSmoothing`, at 0.5; renamed so saved configs take the new default.)

**Holsters.** Your tools are carried on your body. Reach with the gun hand and
squeeze its grip: over the same shoulder for the gun, over the other shoulder
for the spade, at the same hip for blocks, at the other hip for grenades. The
controller buzzes lightly when your hand is at one. `vr_toolButtons` puts the
tools on X/Y as well, for when the holsters are out of reach (seated).

A holster only counts for a grip that begins there. Testers found the grip
taking blocks when they meant to dig or build: a hand hanging loose at the hip
while a wall was being drawn crossed the blocks' holster and the line stopped
(the grip let go of its action at every holster it passed), and the spade
pointed at the ground beside the feet put the hand right in it. Now a grip that
began as the tool's action stays that until it is let go, wherever the hand
goes (`VRGripUpdate`), and no holster counts while the gun is held in both
hands, for the tool already held, or while the spade or blocks point down at a
block in reach (the green dot, more than 30 degrees down): then the grip is
theirs (`VRGripMayHolster`). It used to be any block in reach, and in a tunnel
there always is one, so no holster counted there and the spade and blocks could
not be put away (a tester's report). `vr_state_test` checks it, with the old
rule as the control.

**Labels at their depth.** Names over players, pings, marks and damage numbers
are drawn into each eye separately, so they appear where they are, rather than
on a sheet in front of you.

**Damage numbers, kills and your colour** (in a headset and on a monitor
alike). A damage number waits about a round trip before it shows, and is
dropped if you die meanwhile: when two players fired at once, the one who died
first had sent a hit the server refused, and its number was already up
(`Client::DamageIndicatorDelay`). Hits on teammates show none, unless the mode
is free-for-all: with friendly fire off they take nothing, and the grenade's
numbers already knew it. Every kill of yours plays a short sound
(`cg_killConfirmSound`; the streak sounds start at two), and in a headset the
gun hand buzzes. The block colour you held when you died is back in your hand
after the respawn; it used to reset to `cg_defaultBlockColor`. In a headset,
copying a colour (X) at the sky does nothing: it took the fog's grey, which
looked like the colour resetting by itself.

**Your head stops at walls**, as your body does: leaning into a wall does not
put your eyes through it.

**Tools can be solid** (`vr_toolCollision`, off by default for now). Your
hands, and the gun, spade, block or grenade in them, stop at blocks instead of
going through. Push a gun at a wall and it slides back through your hands; the
controller knocks as it touches. Players aren't solid, so you don't snag on
teammates. It was on by default (as `vr_solidTools`) until it got in the way of
aiming round cover: the gun stopped at the corner and slid back, and the aim
went with it. Off, a gun held into a wall still cannot shoot through it, since
a gun fires from the eye (see "Fair play"), and a spade or block whose tip the
eye cannot see acts from the eye.

**The wrist menu.** With the wrist panel up, bring your other hand close to it
and it grows into a menu. Touch a button with the controller's tip, or point
at it and pull the trigger (the trigger works the menu while you point at it,
and doesn't fire). On it:

- **Chat** and **Team chat** open the chat in front of you with the keyboard
  under it.
- **Light** switches your headlamp (as F does on a keyboard), and says whether
  it is on.
- **Quick chat**: eight common lines ("Help!", "Enemy spotted!", "Need blocks",
  and so on), sent at one touch. The button under them switches between your
  team and everyone.
- **Menu** opens the game's menu (as Escape does), and **Team / weapon** the
  screen where you pick a team and a gun (as L does), to switch team without
  leaving the server.
- **Block colours**: the palette, the only place to pick one now (the turn
  stick's up/down walked it too, and a stick pushed by accident changed the
  colour, which looked like the colour resetting). Touch a colour to build in it. The one you
  hold has a white frame, with a large swatch under the palette.

Take your hand away (40cm) and it shrinks back to the HUD.

The wrist shows the game's frame rate at the bottom. If it is below your
headset's refresh rate the headset fills in frames, and the world wobbles and
feels less solid; lower `vr_renderScale` or the graphics settings until it holds.
`vr_showFps 0` hides it.

**The HUD.** The HUD is on your off hand's wrist: health, ammo, the tool bar,
the block palette, the minimap, the player count, chat, the kill feed,
messages and alerts, and the respawn timer. Health and ammo (or blocks, or
grenades) are a watch's face, large along the bottom (`Client::DrawVRWatch`):
the monitor's corner numbers were small on a panel a hand's width across
(testers asked). Turn the back of that wrist
towards your eyes, as if checking a watch, and it comes up; turn it away and it
goes. Holding a gun two-handed puts the same hand in view with its back to the
side, so it stays down while you aim.

What stays in front of you is what points at places in the world, which only
lines up with them there (player names, pings, marks, damage numbers, and the
rings showing where hits came from), and what you call up on purpose: the big
map, the scoreboard, the menus, and the chat while you are typing into it.
`vr_wristHud 0` puts the whole HUD back on the front panel.

In a headset there is no crosshair or scope (the gun in your hand is the
sight), no hit marker (the controller buzzes instead), and no full-screen hurt
or heal effects, which on a floating panel are a tinted rectangle in the air.

**Menus.** Every 2D screen (main menu, server browser, team and weapon
picker, scoreboard, in-game menu) is drawn on a panel. A menu stands in the
room: it is put down in front of you, level, at eye height, and stays there,
so you can look around it or step up to it. Look away from it (over 55
degrees) for a second, or walk off, and it is put down in front of you again.
While playing, the panel follows your head instead, since what it carries
then (names, pings) is drawn through it onto the world and only lines up with
the world from in front of your eyes (`OpenXRSystem::PlaceHudPanel`). Point at it with either controller; a dot shows where. The trigger
clicks and the stick scrolls. When something wants typing (chat, a server
address) a keyboard comes up on the panel, in the half the text box is not in;
point and pull the trigger to type. Shift is for one capital. A key answers
when pressed: the menus' soft tick (a little different each time, lower for
the big keys), a tick in the hand, and the key lit and pressed in for a tenth
of a second.

**Settings in the headset.** Setup has a **VR** tab with every VR setting:
turning, comfort, hands and tools, your body, the HUD and performance. Text
size, text sharpness and eye resolution take effect on the next start.

**Reloading.** B reloads, as R does on a keyboard; the laser is amber until it
is done, and the gun hand buzzes. **By hand** (`vr_reloadGesture`): B drops
the magazine; take a new one from your belt with the off hand's grip (anywhere
at your waist) and bring it to the gun, just under its front: that is the
reload, with its usual time to finish. Pressing B again at any point just
reloads. It was the default (as `vr_physicalReload`) until testers found two
actions for every reload too many, and the sound alone too quiet a sign that
it was done; it was renamed so that configs which had saved the old default
get the new one.

**The guns in your hand** are the first-person ones, with their own front and
rear sights, as a monitor player's are: the skins draw them for your own hand
(`IVRWeaponSkin`, in each gun's `ThirdPerson.as`), and everyone else still sees
the third-person gun. With the Reflex Sight setting on (Setup, VR tab, or the
monitor's, `cg_reflexScope`), every gun carries ZeroSpades' reflex sight
instead of the rear sight, and its red dot is drawn where each eye's line along
the barrel crosses the sight's window (`Client::DrawVRReflexDot`), so it sits at
infinity on the barrel's line, as a real one's does, and is seen only through
the window. A skin without `IVRWeaponSkin` (a mod's) is drawn as it always was.

**The rifle scope.** The rifle's scope is virtual: look down the rifle's iron
sights and the magnified view appears at the rear sight, with a cross. (Until
0.3 a voxel tube stood on the receiver, placed for the stock rifle; on any other
skin it sat wrong, and it stayed when the skin changed. With the reflex sight
on, the rifle has the reflex sight and no scope.) The picture is wider than the
lens, and its rim is a mild fisheye: the centre
is magnified as a monitor player's scoped rifle is (2.4x), and the edge is
squeezed to show up to 60% more around the target, within a monitor's zoomed
view (except with the eye pressed right up to the lens; see "Fair play"). Shots aim along the scope's line, so they land on the cross. The scope
comes and goes with the rifle in the hand, not the one chosen: the hand only
changes tools once the old one is put away, and the scope used to show a
moment before the rifle came or after it went. The fisheye is drawn as a grid
of cells, each a pixel bigger than it needs to be so neighbours overlap (they
are parallelograms, and missed by a hair: a grid of black lines, the first
time), over the plain picture.

**Your body.** Look down and you see your torso and legs, leaning to where
your head is; your arms come from its shoulders. Looking further down, past 40
degrees, it fades out, and is gone by 65: on a tall platform it hid what was
below (a tester's report). `vr_showBody 0` hides it.

**Comfort when dead or spectating.** The flat game's death and spectator
cameras glide after a corpse or another player, which makes many people sick
in a headset. In VR the view holds still and catches up in cuts, behind a quick
blink to black. Free flight as a spectator, which you steer, is unchanged.

**Holding the frame rate.** When the GPU cannot finish both eyes in the
headset's frame time, the eyes are drawn smaller (down to 60%) and scaled up to
the headset, and grow back when there is room. The wrist shows the GPU time and
the eyes' size next to the frame rate.

**Walking in the room** moves your character as far as the walls allow. Where a
wall stops it, the view stops too: your eyes are always where your character's
are, so you cannot lean round a corner your body is hidden behind (see "Fair
play").

**Grenades** leave with the speed and direction of your hand when you release
the trigger. A throw at `vr_throwSpeed` metres per second (default 5) goes as
far as a normal throw.

## Settings

| Setting | Default | |
|---|---|---|
| `vr_enable` | 0 | play in a headset (the launcher's VR Headset box; `--vr` for one run) |
| `vr_worldScale` | 1.4 | world blocks per real metre; 1.4 puts a 1.6m eye near the game's 2.25 |
| `vr_renderScale` | 1 | eye resolution relative to what the runtime recommends |
| `vr_hudDistance` | 1.5 | menu panel distance, metres |
| `vr_hudWidth` | 1.6 | menu panel width, metres |
| `vr_uiWidth` | 1280 | the menus' and HUD's layout width in the headset (height is 9/16 of it); smaller makes text bigger |
| `vr_uiSupersample` | 2 | how many times the layout's resolution the 2D layer is drawn at, 1 to 3; sharpness, not size |
| `vr_twoHandedGrip` | 1 | the off hand's grip on the barrel holds a gun in both hands |
| `vr_shoulderStock` | 1 | the off hand's trigger, held on a gun's barrel, braces it on the shoulder |
| `vr_toolCollision` | 0 | hands and held tools stop at blocks (was `vr_solidTools`, on) |
| `vr_aimSmooth` | 0 | 0 to 1: how much a nearly still gun is steadied |
| `vr_swingSpeed` | 2.5 | swinging the spade faster than this (m/s) hits; 0 turns it off |
| `vr_holsters` | 1 | take tools from the body with the gun hand's grip |
| `vr_showFps` | 1 | the frame rate on the wrist |
| `vr_reloadGesture` | 0 | B drops the magazine; the off hand fetches and inserts one (0: B reloads) |
| `vr_toolButtons` | 0 | X and Y step through the tools (0: X copies a colour, Y opens the map) |
| `vr_sprintHold` | 0 | sprint while the walking stick is held in, instead of a click toggling it |
| `vr_scope` | 1 | a magnifying scope on the rifle, seen looking down its iron sights |
| `vr_showBody` | 1 | your own torso and legs |
| `vr_comfortCamera` | 1 | dead or spectating, the view holds still and moves in blinks |
| `vr_vignette` | 0.5 | walking, falling or smooth turning close in the edges of the view; 0 off, 1 the most |
| `vr_dynamicResolution` | 1 | draw the eyes smaller when the GPU cannot keep up |
| `vr_snapTurn` | 45 | degrees per flick; 0 for smooth turning |
| `vr_smoothTurnSpeed` | 150 | degrees per second when turning smoothly |
| `vr_symmetricFrustum` | 0 | 1: draw each eye with a centred frustum covering the runtime's (10-20% more pixels; the old way) |
| `r_debugOffCentre` | 0 | a test: draw the monitor's view off-centre, as a headset eye is (1 = its right half, stretched) |
| `vr_multiview` | 0 | 1: draw both eyes in one pass (VK_KHR_multiview), as a standalone headset needs; read at start |
| `vr_foveation` | 0 | not used for now (crashed a Quest 2's driver, no gain measured on a Quest 3); was foveated rendering: each eye's edges shaded at half or a quarter density (0 off, 1 low, 2 medium, 3 high), where the GPU has `VK_EXT_fragment_density_map` (a Quest's does); read at start |
| `vr_mirrorEye` | 0 | which eye the monitor shows: 0 left, 1 right |
| `vr_debugFixedLook` | (empty) | for tests: "yaw pitch" in degrees holds the head in that direction |
| `vr_menuScene` | 1 | the menus stand on a generated map instead of in a void |
| `vr_heldMap` | 1 | the big map is held in the off hand like a tablet |
| `vr_traceRecord` | (empty) | for tests: write every frame's head, eyes and controllers to this file |
| `vr_traceReplay` | (empty) | for tests: play this file's frames in place of the headset's input |
| `vr_profile` | auto | `mobile` drops the full-screen polish passes for 4x MSAA and multiview (a standalone headset's pass set); `auto` is mobile on Android, `pc` elsewhere; read at start |
| `s_hrtf` | -1 | 3D sound over headphones, for everyone: -1 on in a headset and OpenAL's choice on a monitor, 1 on, 0 off; read at start |
| `vr_mirror` | 1 | 0: no window on the monitor, only the headset (as on a Quest) |
| `vr_controllersOnly` | 0 | in a headset, ignore the keyboard and mouse entirely, as a Quest has neither |
| `vr_disableExtensions` | (empty) | OpenXR extensions not to use even when the runtime has them, comma separated; the log lists which are on |
| `vr_crouchAt` | 0.27 | how far below standing height your head goes to crouch, metres |
| `vr_seated` | 0 | playing sitting down: the head dropping never crouches, only the crouch button |
| `vr_roomScale` | 1 | walking in the room moves the character |
| `vr_throwSpeed` | 5 | hand speed, m/s, for a full-strength grenade throw |
| `vr_leftHanded` | 0 | hold the tools in the left hand |
| `vr_showArms` | 1 | your own arms: 0 hands only, 1 forearms, 2 whole arms |
| `vr_laser` | 1 | a laser from the gun, spade and blocks, with a dot where it lands |
| `vr_wristHud` | 1 | your HUD on the off hand's wrist; 0 on the front panel |
| `vr_wristWidth` | 0.24 | wrist panel width, metres |
| `vr_aimTilt` | 15 | degrees the tools point below the controller's aim |
| `vr_brightness` | 0.75 | the brightest a lit block or model gets, 0.5 to 1; above about half of it, highlights roll off instead of clipping at white (1: as on a monitor); next map |

## The server script

`Server/vr_presence.py` is a piqueserver script. Copy it into your server's
`scripts` folder and add it to `config.toml`. It is also collected, with its
tests and the fake VR player, in AoSTools
([tools/vr-presence](https://github.com/Detank2002/AoSTools/tree/main/tools/vr-presence)):

```toml
scripts = ["vr_presence", ...]

[vr_presence]
max_rate = 30      # poses per second relayed per VR player
body_sync = true   # room-scale walking moves the body on the server
max_step = 0.6     # furthest one pose may move it, blocks
max_reach = 4.0    # hands/head further than this from the eye are refused
```

`/vr` lists who is in VR.

Without the script a VR player still plays. Nobody sees their hands, and their
room-scale steps reach the server once a second through the ordinary position
update.

### Testing without a second headset

`Server/test/vr_puppet.py HOST [PORT]` joins a server running the script and
plays a VR player that looks around and waves its hands. `--follow` makes it
walk up to the nearest player and stand in front of them.
`Server/test/test_vr_presence.py` runs the script on a real piqueserver
against fake clients and checks it byte for byte against the client's own
encoder (it builds `Server/test/vr_pose_fixture.cpp` with g++ when it can).
Both need piqueserver installed (Python 3.10 to 3.12).
`Server/test/vr_state_test.cpp` checks when the wrist HUD comes up, when the
wrist menu opens and where its pointer lands, where tools stop at a wall, and
where the holsters are; the build line is at its top.

## How it works

### Rendering

The Vulkan device is created through the OpenXR runtime
(`XR_KHR_vulkan_enable2`), so the runtime picks the GPU the headset is on and
adds the extensions it needs to share images with its compositor. See
`Sources/VR/OpenXRSystem.cpp`.

Each eye is drawn as a frame of its own: its own command buffer, fence and
frame slot (`Sources/Draw/Vulkan/VulkanRendererVR.cpp`). The obvious approach,
both eyes in one command buffer, does not work with this renderer: the post
filters, the water and the sprite batches all reset their per-frame descriptor
pools and buffers by frame slot on every call, so the second eye would reset
descriptors the first eye's commands still use. Giving each eye its own slot
reuses the renderer's existing frames-in-flight machinery instead of rewriting
every filter.

Work that belongs to the frame rather than to a view is done once, in the left
eye's pass, whose commands run first: map chunk uploads, the map shadow and
lighting updates, water wave uploads, and the model shadow cascade. The cascade
matrices live in single buffers written while recording, so the right eye
reuses the left eye's cascade rather than rewriting it under commands that have
not run yet. The eyes are centimetres apart; the cascade covers the same ground.

Anything the scene pass uses up has to survive the first eye. The renderer
already cleared its scene lists (models, lights, debug lines, particles) only
after the last eye, but the particle and tracer renderers also cleared their
own lists at the end of every draw, so the right eye had no particles or
tracers: smoke, blood and debris were in one eye only. They no longer clear
themselves; the renderer does, once.

State that carries from frame to frame must advance once a frame, not once an
eye. Auto exposure blends each frame's brightness into a running exposure;
both eyes blending it moved the exposure twice a frame and showed the right eye
a step further along than the left. The second eye now applies the exposure
the first eye worked out and skips the update.

Each eye is rendered with **the runtime's own frustum**, which is not centred:
an eye sees further to its own side than towards the nose. It used to be drawn
with the smallest centred frustum covering that one, because the fog, water,
sky and lens flare shaders rebuilt view rays from `tan(fov/2)`; that cost
10-20% more pixels, which a mobile GPU cannot spare. Now a view's edges are
four tangents (`FovTangents`, `SceneDefinition::GetFovTangents()`; left and
down negative), and everything that turns a screen position into a ray or back
goes through them: both projection matrices (an off-centre `glFrustum`), the
culling planes, the shadow cascade's corners, the sky and classic fog's rays,
the water's view-position rebuild and projection (`waterViewToNdc`), the lens
flare and the per-eye labels. A centred view gives the matrices and planes it
always had. `vr_symmetricFrustum 1` draws the old way, at the larger size.

It was tested without a headset, which has no off-centre eyes to show:
`r_debugOffCentre 1` draws the monitor's view with its left edge moved to the
middle, so the picture must be the usual one's right half stretched over the
window. In one run, flipping between the two every five seconds while the
player stood still, the off-centre frames matched the centred ones' right half
to a mean difference of 1.2-1.6 levels in 255 (the stretch's own blur; the
frame-to-frame noise was 0.2-0.5), against 7-34 for the left half or the
unstretched frame (the controls), with the volumetric fog and the best water
on and off.

The 2D layer is drawn once per frame into a transparent image and handed to the
runtime as a head-locked quad layer, so the whole existing UI works in VR
unchanged. The single "head" view the client builds is
given the panel's angular size as its field of view, so anything the 2D layer
pins to a world position (names, pings, markers) lines up with that position
seen through the panel.

Every pass that draws into the 3D image has to size itself from the render
target, not the window: in a headset the two differ (an eye is 2624x2750 on a
Quest 3, the window 1600x900). The water pass took its viewport from the window
and drew the water into a window-sized corner of each eye, which showed as a
flat rectangle of slightly-wrong picture fixed at the top of the view. The
lens flare took its aspect ratio from the window the same way.

The wrist panel is a second 2D canvas (`IRenderer::SetVRCanvas`), 960x600
pixels, with its own image renderer and its own quad layer. The client draws
the player HUD onto it between `SetVRCanvas(1)` and `SetVRCanvas(0)`, and
`ScreenWidth`/`ScreenHeight` report the canvas meanwhile, so the HUD lays itself
out as on a small screen with no code of its own. `SetVRCanvas(1)` refuses on
frames the wrist is not being looked at, so the HUD is not even drawn then. The
panel sits in tracking space over the back of the wrist, turned to face the
eyes with the head's up as its up, so text stays level whatever the wrist's
roll. Whether it is up is decided from the grip pose (`VRPlaceWristPanel` in
`Sources/Client/VRState.cpp`): the back of the wrist towards the eyes, within
40 degrees of the view and 70cm, each loosened a little once it is up so it
does not flicker on an edge. OpenXR's grip +X points to the user's right on both
hands (out of the left palm, into the right one), so the back of the hand is -X
on the left and +X on the right.

### Both eyes in one pass

`vr_multiview 1` draws the scene once for both eyes (VK_KHR_multiview), which
roughly halves the draw calls and the CPU time of recording them: what a
standalone headset needs, and a saving on PC too. It is read when the renderer
starts, because every pipeline is built for it. Without the extension the eyes
are drawn one at a time as before.

The scene shaders get their matrices from many places: push constants, a
buffer per model, the water's own buffer. Rather than give every one of them a
matrix per eye, the scene is drawn with the left eye's, and each scene vertex
shader ends with `gl_Position = correction[gl_ViewIndex] * gl_Position`, where
`correction[i] = PV_i * PV_0^-1` takes the left eye's clip space to eye i's.
It is exact (every PV is invertible and the product is linear in homogeneous
coordinates), one line per shader, and worked out in double because a
perspective inverse loses digits in float. The water's mirror pass needs no
second set: the reflection cancels out of the product. Culling is done once,
against a frustum that covers both eyes (the left eye's left plane, the right
eye's right plane, the looser top and bottom).

The shaders are variants, not changes: CMake compiles every shader that
mentions `ZS_MULTIVIEW` or `ZS_VIEWS_SET` a second time with `ZS_MULTIVIEW`
defined, into `NAME.mv.spv`, and the SPIR-V loader takes the variant only in
multiview, so a monitor's shaders are exactly what they were. The per-eye data
is a uniform buffer per frame slot (`ZsViews`: the corrections, each eye's view
and projection, its position and edge tangents), declared by `Multiview.glsl` and
bound within sets the pipelines bind anyway, never a fifth set (a Quest 2's
Adreno 650 binds only four; with a fifth, multiview was off there). Each shader
names where with `ZS_VIEWS_SET` and `ZS_VIEWS_BINDING`: binding 2 of the scene
view's set (3) for the map and models, binding 3 of the sprite's set (3) for
the sprites, set 1 for the water (the *sceneLights* directive's set before the
lights, empty without multiview), set 0 for the sky and debug lines. The
water and soft-particle shaders, which read the screen, depth and mirror
images, read their own eye's layer (`ZS_SCREEN_*`), and the water's reflections
and refraction use their own eye's view matrix, edge tangents and water plane.
Soft particles have no quarter-resolution layer in multiview (that layer is
one view's); every sprite is drawn at full resolution. Screen-space ambient
occlusion is off in multiview and in the scope's view: its passes are one
view's, sized to the eyes.

The scene's targets have a layer per eye and its render passes a view mask.
All of it is recorded into one command buffer and submitted once.

The post-process chain is a list of steps (`BuildPostSteps`: the same filters
and conditions, in the same order, that a monitor runs). Every filter a
headset can run has a multiview twin, used when it is given two layers: a
render pass with the view mask (`CreateSimpleColorRenderPass` takes one),
pipelines built from `.mv` fragment shaders that read a `sampler2DArray` at
`gl_ViewIndex`, two-layer scratch images from the pool. Bloom (all six
levels, each two layers), FXAA, colour correction (without its sharpening,
whose blur has no layered pass) and the outlines draw both eyes at once; the
outlines read the scene's stencil, which has a layer per eye and its own
layered resolve under MSAA. Auto exposure measures the left eye's layer (through a one-layer
view of it, `VulkanImage::GetLayerView`), as the per-eye path does, and applies
the one exposure to both eyes in one pass. Fog runs once per eye (below): its
view differs per eye. Depth of field, camera blur, TAA and the
lens flare never run in a headset (the view denies them), so they have no twin;
a filter without one still works: the chain runs it once per eye on a copy of
that eye's layer, on the eye's own frame slot, splitting the layers out or
putting them back between runs (`CopyImageLayer`). `r_multiviewFilters 0` runs
every filter that way, which is the control each twin was checked against.

Checked on SteamVR's null headset, each eye in turn, against the eyes drawn
one at a time and against `r_multiviewFilters 0`: bloom and colour correction
0.35 / 0.33 (left / right, of 255); with fog, FXAA and the outlines too 0.23 /
0.24; with HDR auto exposure 0.28 / 0.25; the left and right eyes differ by
11-34 in the same runs. Fog2 (`r_fogShadow 2` with radiosity) matched between
the two multiview modes to 0.52 with no pixel more than 20 off. Every barrier on the scene images covers
all their layers (`VK_REMAINING_ARRAY_LAYERS`, which is the same thing for a
one-layer image), and every copy as many layers as there are views.

It was tested on SteamVR's null headset by drawing the same frame both ways
and mirroring each eye in turn (`vr_mirrorEye`): multiview matched the eyes
drawn one at a time to a mean of 0.35 levels in 255 for the left eye and 0.31
for the right (no pixel more than 8 off), against 34.6 between the two eyes
(the control: the right eye really is the right eye, not a copy of the left).
The validation layers report nothing in multiview that the one-at-a-time path
does not. It found one bug that predated it: the water's occlusion query was
reset inside its render pass, which is not allowed; it is now reset before the
pass, one query per view in multiview.

Multiview takes MSAA (`r_multisamples`) as the one-at-a-time path does. Under
MSAA the scene depth is resolved by a full-screen pass in the middle of the
frame (`VulkanDepthResolveFilter`); it has a multiview twin, a render pass with
the view mask and a shader that reads the two-layer multisampled depth at
`gl_ViewIndex`, and the colour is resolved by `vkCmdResolveImage` over both
layers. The post chain and `ExtractView` then start from the resolved images
rather than the multisampled ones.

Tested at 4x with the head held still (`vr_debugFixedLook 0 -60`, below), each
eye against the eyes drawn one at a time: 0.30 / 0.32 of 255, no pixel more than
20 off in either eye. The controls are pixel counts, because from the overview
camera, 64 blocks above the map, a mean barely moves: with MSAA off, 150 pixels
along the block edges differ by more than 20 (so MSAA was really on), and
between the right eye and the left 2230 do (so the right eye is its own).

Timed on the null headset (fixed look, 600-frame means, runs repeat within
0.3ms), one eye at a time against both in one pass: recording and submitting
the eyes 6.2ms against 3.7ms of CPU; GPU work 1.15ms against 0.89ms single
sampled, 1.44ms against 1.32ms at 4x MSAA. That is a desktop GPU and a small
scene; the CPU's 40% is what a standalone headset, short of CPU, is after.

**Input traces** (`vr_traceRecord`, `vr_traceReplay`, `VRTraceFormat`). A
trace is a text file, `zsvrtrace 1` and then a line per frame of what the
headset said: the head, both eyes, and each controller's grip and aim poses,
velocity, trigger, grip, stick and buttons, in tracking space. Recording writes
every frame (flushed each frame, so a test that kills the game keeps them all).
Replaying puts each line in place of the headset's own input, one a frame,
right after it is read and before anything is worked out from it (the wrist
panel, the rig), and holds the last line at the end. Being text, a test can
write one as easily as record it. Checked on the null headset: a replayed trace
holding the head 60 degrees down drew the picture `vr_debugFixedLook 0 -60`
does, to 0.33 of 255 with 2 pixels more than 20 off; one at 20 degrees down
(the control) differed in 866k pixels. `vr_state_test` checks a line reads back
as written and that a short or garbled line changes nothing. Replay is by frame,
not by time, so a frame rate that differs moves the game's clock a little
differently; for pictures and input that is no matter.

`vr_debugFixedLook` ("yaw pitch" in degrees) replaces the headset's head
direction with a fixed one, keeping the eyes' spacing. Without it a test on the
null headset sees wherever the head happens to point, which is often the sky,
and two runs cannot be compared.

### Labels at their depth

The first headset tests said the game did not quite feel 3D. The stereo itself
was right (each eye has its own position, orientation and field of view from
the runtime, the eye separation scales with the world, and water reflections
are rendered per eye). What was flat was everything the 2D layer pins to a place
in the world: names, pings, marks, damage numbers. They were drawn onto the
front panel, 1.5m away, so a name over a player 30 blocks off sat 1.5m in front
of you: the eyes put it at one depth and the thing it labelled at another.

They are now drawn into each eye. Each eye's finished picture is kept in an
image of its own (`vrEyeImages`) instead of going straight to the headset; the
client runs its label drawing once per eye (`Client::DrawInEachVREye`), with
`Project` rebuilt from that eye's position and field of view and the 2D canvas
set to that eye (`SetVRCanvas(2 + eye)`); Flip draws each eye's labels over its
picture and only then copies it to the headset, taking the headset's images
there rather than in the eye passes. The canvas is half the eye's pixels, drawn
at 2x, so label text is the panels' size and sharpness. Labels are drawn over
the scene, not depth-tested, as on a monitor, but at the right disparity: they
appear at the depth of what they label. The window's mirror is the left eye's
kept image, so it shows them too.

None of the label functions changes state while drawing (checked: they only
read the time), so running them twice a frame is safe. Pings pushed to the edge
of the view stay well inside each eye's picture, whose edges are beyond what
the lens shows.

Lens flares are off in a headset: they are a camera's, drawn over the picture
at no depth, and in stereo their ghosts float in front of everything.

### Sharp text

The first version drew the 2D layer at the window's size, with text rasterised
at 1x, and the headset stretched it over a panel 56 degrees across. The window
then decided two things it should not have: how big the text was (the menus
lay themselves out in fixed pixels, so a bigger window made them smaller in the
headset) and how sharp it was (never very: small thin glyphs, resampled once by
the compositor with nothing to smooth them). Text was small, soft and
shimmering, and raising the resolution only changed the menus' size.

The two are now separate:

- **Size** is the layout: while a headset is in use the 2D layer is laid out at
  `vr_uiWidth` by 9/16 of that (1280x720), fixed for the whole run whatever the
  window (`VulkanRenderer::SetupVRUILayout`). The menus lay themselves out once,
  so it cannot follow the session starting or stopping. The desktop window
  shows the same layout stretched, and the mouse on it is scaled onto it.
- **Sharpness** is the pixel ratio: headset panels are drawn at
  `vr_uiSupersample` (2) times the layout into an image of their own, then
  blitted to the headset's image at the layout's size. A 2:1 linear blit reads
  each output texel exactly between four input ones, so it is a true 2x2 box
  filter, in linear light since both images are sRGB: every edge in the UI gets
  four samples.
- **Glyphs** are rasterised at the pixel ratio (`IRenderer::GetUIPixelRatio`;
  `FTFont` asks once, when made). Supersampling alone would only magnify the 1x
  glyph bitmaps and gain nothing. Their metrics (advances, line heights) stay
  in layout pixels, taken at 1x with no hinting, so text measures and wraps the
  same at any ratio, and glyphs are placed on the drawn image's pixel grid
  rather than the layout's. Blurred shadows are made from the glyph shrunk back
  to 1x (a blur gains nothing from resolution, and its kernel is in pixels), and
  outlines are dilated by the ratio, so both look as they do on a monitor.

The image renderer draws in layout pixels onto any size of surface: the
viewport stretches one over the other, and only the clips need converting.
Clip rectangles are kept in layout pixels and become scissors at record time;
the circular clip, tested against `gl_FragCoord` in the fragment shader, is
scaled the same way.

On a monitor the layout is the window and the ratio 1, and none of this
changes anything.

The chat and kill feed decide which lines are showing in `Update()`, from the
screen height, and are drawn later onto whichever canvas is current. Moved to
the wrist, they would have laid out for the window's 900 pixels and drawn onto
the panel's 600, running down over the health. So each remembers the height it
last drew on and lays out for that.

Comfort: no camera shake, roll, zoom, motion blur, depth of field or vignette
in VR. Screen tints (hit flash, underwater) are dropped for now; they multiply
the 3D view, and the panel is transparent.

### Aiming, and what the server sees

AoS has one facing per player. The server checks every hit against it (a
5-block tube around the aim ray, from the server's position of the shooter),
scales walking speed by it, and refuses to climb while it points steeply down.

- If the facing always followed the gun hand, a spade or block held low would
  slow the player and stop them climbing.
- If it always followed the head, a shot from the hip at something you are not
  looking at would fail validation.

So the facing follows the head, and follows the tool hand from the moment a
tool is used until 0.4s after (`Client_VR.cpp`).

Except while a gun is held on a server without vr_presence: there the facing is
always where the gun points. Such a server's players (and anyone on a client
without vr_presence) see only the facing, and a head looking about while the
gun pointed elsewhere looked like a cheat's snapping aim (a tester's report).
A gun held pointing at the floor then walks slower and does not climb, as a
monitor player looking at the floor does; the spade and blocks keep the head's
facing, since they are held low all the time.

The hand's axes are the controller's aim pose turned down by `vr_aimTilt`
(15 degrees). Held naturally, a Touch controller's aim ray points higher than a
gun in that hand would, and the first headset test found the gun aiming a bit
high. The gun drawn in the hand, the ray it shoots along and the pose others
see all come from the same axes, so they stay in agreement at any tilt. The
menu pointer keeps the untilted aim ray. Shots, digs, block placement
and the colour picker always cast from the hand itself: the player's tool ray
(`Player::GetToolOrigin` / `GetToolDirection`) is the hand's aim pose.

Walking uses the movement keys, which the server interprets relative to the
facing, in eight directions. While walking the facing is the head's, unless the
gun is in use (fired in the last 0.4s), so the stick's direction (relative to
the head) is rounded to the nearest of the eight, as keys are, and straight ahead
is straight. Against the gun's facing, rounding walked up to 22.5 degrees off
where the head looked (off a tower's ledge, a tester's report): there the walk
alternates between the two directions either side, 1/20s at a time, in the mix
that averages to the wanted one (`VRWalkDither`). That alternation was used for
all walking before 0.3, and testers found the zigzag it put in the view too much.
On a server without vr_presence, standing still with a gun, the facing is
still the gun's.

### Room-scale

The head's offset from where the body stands is pushed into the body each frame
(`Player::VRPushBody`), per axis and never into a wall, like `BoxClipMove`.
The collision samples are at the head, the chest and 1.2 below the eye: a
resting body's feet sit anywhere inside the top block of the ground, so a
sample nearer the feet can land in the ground and call open floor a wall. 1.2
never does and still meets a one-block step. (The first version sampled at
1.35 and refused every step on flat ground whenever the body happened to rest
low in its block; the server test caught it.)

Height works the same way from a standing height taken when the play space is
centred. A head above it raises the view by at most 15cm (scaled with the
world); crouching for real below it crouches the character. The first version
allowed 35cm, and a floor measured while the player was bent over, or while the
headset sat on their lap as the session started, then held the head 50cm (half a
block) above the game's eye for the rest of the session. Others saw the head
floating over a chest that looked too low. Three things now stop that:

- The session's own recentre waits for the app to get focus rather than taking
  the floor the moment the session starts.
- Recentring by hand takes both sticks held in for a second. Both sticks are
  also crouch and sprint, and go in together easily.
- A recentre the player did not ask for may raise the standing height but not
  drop it by a crouch or more.

That last one has a cost: one taken with the headset held high (lifted off a
table and put back on, say) raises the floor, and nothing automatic brings it
down again, so the player stays crouched however straight they stand (a
tester's report). Stand up and press **Reset Height** in the menu (Esc, or the
wrist's menu button), or hold both sticks in: either takes the height your head
is at as standing.

PositionData is only accepted once every 0.7s, so on a stock server others see
a room-scale walker hop about once a second. The script accepts the body
position carried in each VR pose instead, a step of at most `max_step` at a
time, sideways only, and only into open space, checked with the same body
samples.

### Hands and tools

**Two hands on a gun** (`Client::UpdateVRTwoHandedGrip`). The off hand's grip
takes the gun when that hand is on the barrel: ahead of the gun hand along its
aim (8 to 70cm) and within 18cm of that line. The gun hand's forward becomes the
line from it to the off hand, its roll kept by squaring its own down to that.
It lets go when the grip opens past a lower threshold than took it, or the
hands are more than 90cm apart. Everything held follows the hand's axes, so the
gun drawn, the ray it fires along, and what others see all move together. The
hand axes are rebuilt when the scene is built (`ApplyVRToSceneDefinition` calls
`UpdateVRRigTransforms` again), so the two-handed aim is applied at the end of
that too, or the drawn gun would snap back to one hand. The same grip is the
scoreboard when it is not on the gun.

**Swinging the spade** presses the tool's trigger when the hand moves faster
than `vr_swingSpeed`, and is latched until it slows to half that, so one swing
is one hit. The press is held 0.15s: a swing that comes while the spade is
still recovering lands the moment it is ready instead of being lost.

**Holsters** (`VRHolsterAt` in `VRState.cpp`) are placed from the eyes, turned
with the head but kept level: over a shoulder is 20cm to the side, 15cm back and
10cm down; a hip is 22cm to the side and 60cm down. Looking straight down gives
no heading, so no holsters. The slot the hand is in gets 25% more room to leave
than to enter, so its edge does not buzz on and off. The grip that took a tool
is not also that tool's second action until it is let go, and no second action
starts at a holster. `Server/test/vr_state_test.cpp` has the geometry, with
controls: where a gun is held and aimed is no holster.

**Recoil** at first was the gun model's alone: the muzzle kicked up (rifle 8
degrees, shotgun 12, SMG 3) and the gun back 6cm, settling before the next
shot, while the aim never moved. That jolt is now half the size and sits on top
of the real recoil, which turns the aim (see "Recoil" below).

**The head stops at walls.** The body is walked through the room as far as the
map allows. The head used to lean past it by up to 30cm (and a head inside a
block sees through the map); since the fair-play rules it does not lean at all,
and this clearance is what is left of it. `Client::VRHeadClearance` keeps the view 0.12 blocks from
any solid block by scaling back the head's offset from the body's eye (binary
search along it), as a head against a wall stops. What is sent to others is the
same clamped head.

### Fair play

Ace of Spades trusts the client: recoil, where a shot starts, what the player
can see are all worked out by the game, and the server checks almost none of
it. So nothing a headset does may make its player stronger than a monitor
player, whatever it would be like in another VR shooter. Three things were
taken out on 2026-10-10 for that reason:

- **Recoil cannot be turned down.** There was a `vr_recoil` setting from 0 to
  2, and a braced gun took 40% (two hands) or 60% (the stock) off. Now a
  headset gets exactly a monitor's recoil, and a braced gun counts as aiming
  down the sights, which only spares the walking doubling
  (`VRRecoilHoldScale`, tested).
- **No leaning past cover.** Where room-scale walking was stopped by a wall,
  the view could still lean 30cm sideways and 15cm up past the character's eye
  (about 0.4 and 0.2 blocks at the default scale): a peek round a corner, or
  over a wall, with the hitbox still behind it. Now the view is where the
  character's eye is, never higher; against a wall it stops with the body.
- **Shots from the hand only where the eye can see the hand.** A gun held out
  round a corner could fire with the body hidden. On a server running
  `vr_presence`, where everybody sees the VR player's arms and gun, a spade or
  block still acts from the hand freely. Anywhere else, a tool whose tip the
  eye has no line of sight to (`VRHeadClearance` from the eye to it) acts from
  the eye, along the tool's direction, as a monitor player's does, so it
  reaches only what the player can see.
- **A gun fires from the eye, at what it points at** (2026-10-12, every
  server). The rule above left a gap a tester found at once: a muzzle just past
  a corner is in the eye's sight, and it sees round the corner, so the shot hit
  what the eye could not see, with the body behind the wall. Now a gun's shot
  leaves the eye, as a monitor player's does, towards the first block or player
  hitbox on the gun's own line (`Client::VRGunAimPoint`). Whatever stands
  between the eye and that point takes the shot: round a corner, the corner.
  The facing the server is sent while aiming is the shot's, so others see you
  looking where you shoot. Players count on the gun's line, or a shot at
  someone close, from an eye an arm's length off the line, would pass beside
  them. It applies on `vr_presence` servers too: there others see the arm, but
  the body is still behind the wall. The laser shows the shot as it will land.
- **The gun's line starts at the eye** (2026-10-27). Found from the gun, the
  point it aimed at could be past a ledge or a corner the eye's line met, and
  the laser's dot jumped back onto the cover a good way short of the edge; and
  with the gun held at the chest, a window sill or low cover in front of the
  feet stopped the gun's line where the eye saw over (testers' reports). A
  first answer raised the gun's line 0.2m (`vr_aimHeight`), which put the laser
  and the tracer in the air over the muzzle, and still left the jump. Now the
  line runs from the eye, along the gun's direction: it is the shot's own line,
  so nothing the eye clears stops it and the dot never jumps. The cost is up
  close: the dot is the eye's offset from the barrel's line away from where the
  barrel points (the laser bends a few degrees inside ten blocks). The scope's
  line is unchanged.
- **Reach is measured from the eye.** Building always was (`InBuildRange`, 3
  blocks from the eye, the server's rule), but the spade's dig and melee
  reach were measured from the tool, which in a headset is the hand, up to a
  block ahead of the eye: a VR spade reached further than a monitor's. Now
  both measure from the eye. The laser's green dot uses the same measures (it
  measured from the hand, so it went green where a block could not be built,
  which made building feel short).
- **The scope is never stronger than a monitor's.** A monitor player's scoped
  rifle is GetAimDownZoomScale's 2.4x, over a field of 15.6 degrees each side
  at the default `cg_fov` 68. The headset's scope showed a fixed 6 degrees in a
  lens magnified by how close the eye was: 4x at 0.3 blocks, over 10x at 0.1.
  Now the lens's centre is 2.4x wherever the eye is, and its fisheye rim
  (alpha = C rho + (E - C) rho^3) adds up to 60% more field, within the
  monitor's 15.6 (`ClientPlayer`, scope; `Client::DrawVRScopeLens`). One gap
  is left: with the eye pressed so close that the lens covers more than
  2.4 x 15.6 degrees each side, the centre's own field passes 15.6. A
  stronger rim (rho^4, always out to 15.6) with that case capped (the picture
  stopped growing, ringed in black) was tried, and then eleven scopes side by
  side (`vr_scopeStyle`, for testing); this one, 1d5eff8's, was the one kept
  (2026-10-12). Testers asked for a big wide scope ("Scope PRO",
  with a fisheye and a "fantastic field of view"); a field wider than a
  monitor's at the same magnification would see more than a monitor player
  can, so it stops there. Shots aim along the scope's line: the barrel is 0.23
  blocks under it, and aimed through the scope at a head, a barrel shot took
  the neck.

Kept, because they give nothing a monitor does not: the laser (a crosshair:
it shows where the shot goes, before the spread), aim smoothing and the
two-handed grip (they steady the hand; recoil is added after them), the scope
(the monitor's zoom), and the comfort options.

**What servers are told.** The version string a server asks for gets the kind
of headset on the end: "Windows 11 | ZeroSpades 0.0.9 40e684d (x64) PCVR" from a
PC headset, "QuestVR" from a standalone one, nothing from a monitor (or with
`vr_enable` on and no headset found), so an admin can see who is playing in VR.
"You are connected with" in the chat says the same. Checked against piqueserver
with a script printing `client_info`. And on a server without `vr_presence`, a
headset player is told so as the game starts, in the chat and in a box in
front of them for ten seconds (the training tips' box, edged blue):
others see a normal player, without arms or the gun in the hand, and a gun the
eye cannot see along fires from the eye. Not shown with `vr_presence` running,
or on a monitor (the controls). Not on the training course either
(`TrainingHost::IsActive`): its server does not relay poses, but the only other
players there are its targets, so it does not matter, and the box was only in
the way (it used to show there too).

The same rule shapes what a headset gets that is not about the hand. HRTF
(OpenAL Soft's 3D sound over headphones: where a footstep comes from, above and
behind included) is on in a headset by default, since a headset is worn with
headphones, but it is the setting `s_hrtf` for everyone, in the Game Options'
sound settings, not a VR one: a monitor player in headphones can have exactly
the same ears. Its auto (-1) is OpenAL Soft's own choice on a monitor, which
turns it on where the output says it is headphones. Checked by the log line
`HRTF: on/off` at start: auto in a headset on, auto on a monitor OpenAL's
choice, 1 on, 0 off in both (the control). The listener already follows the
head: the scene definition takes the head's pose every frame, and the sound is
placed from it.

### Round two: comfort, performance and the tools

**The comfort camera** (`Client::UpdateVRComfortCamera`). Moving the view with
nothing of yours moving it is what makes VR sick: the eyes see motion the inner
ear does not feel. Cuts do not. So for the camera modes that move by
themselves (the third-person death camera, following a player in first or
third person) the view is anchored where it was, and catches up in a cut: to
black at once, the jump, and back over 0.3s, when what is watched is more than
6 blocks off, or more than 1.5 for two seconds. Every change of camera mode
(dying, respawning, switching whom you follow) is a cut already, and gets the
same blink. Free flight is steered by your own stick, as walking is, and is left
alone. The blink is a black rectangle drawn into each eye's picture.

**The comfort vignette** (`vr_vignette`, `Client::DrawVRVignette`). The other
half of the same sickness: walking on the stick, falling and smooth turning move
the view while the body stands still, and the motion the eyes see most is at
the edges. So while the body moves under its own velocity (`VRVignetteTarget`:
full at a walk's 4 blocks a second, at a fall past a jump's own speed, at the
default smooth turn of 150 degrees a second) the view closes to a circle, with
a soft edge, centred where the eye looks straight ahead (projected per eye, not
the middle of an off-centre picture). It comes in over 0.15s and goes over 0.5s,
so a walk's steps do not flicker it. Walking in the room moves the body without
a velocity and snap turns are cuts, so neither shows it. `vr_vignette` is how
far it closes: the clear circle goes from 60 degrees (only the corners of a
headset's view) to 20 at 1, and 40 at the default 0.5, which on a flat test
shows as a darkening at the edges; at 0.5 the band started at 50 degrees at
first and was not visible at all. It only takes away what the player sees, so
it is no help to anyone (see "Fair play"). The vignette and the blink are drawn
in their own pass over each eye, so the blink now also shows before you join a
team. `vr_state_test` checks the ramps, the timing and the angles, with
standing still and creeping as the control.

**Holding the frame rate** (`VulkanRenderer::UpdateVRResolution`). Below the
headset's rate the runtime makes up frames by warping old ones, and the world
wobbles. The GPU's time over both eyes is measured with timestamps around each
command buffer's own work, added up (one span per eye, or one for both in
multiview; two sets alternating, so a set is read two frames after it was
written, when its fence says it is done) against the headset's display period
from OpenXR. It used to be one span from the start of the left eye's commands
to the end of the right's, which also counted the GPU sitting idle while the CPU
recorded the right eye: on the null headset that read 3.1ms for 1.15ms of work,
so a frame short of CPU, not GPU, would have shrunk the eyes for nothing. Over 85% of it for half a
second steps the eyes down (90, 80, 70, 60% of their full size); a size up is
taken after four seconds in which its predicted cost (by its pixels) would fit
in 72%. At least two seconds between steps, since each rebuilds the render
targets. The headset's images stay full size and the smaller picture is scaled
up into them; the per-eye label canvas is laid out on the full size, so labels
do not grow when the resolution drops.

**The scope** (`SetVRScopeView`). A real scope, not a zoomed crop of the eye:
a third view, 512x512, 6 degrees across, looking along the gun, with the
head's up for its up so the world in the lens stands level with the world around
it however the gun is rolled. Its line is the iron sights' (from the rear
notch to the front sight's top, as the skin gives it: `GetVRGunSights`), in the
gun's frame so it kicks with it. A skin that does not give it gets the line the
old tube had, 0.33 above the grip, with nothing drawn. The reticle is drawn along the gun's own right and up projected
into each eye, so it cants with the gun; the picture itself is rendered with
the head's up and stays level with the world, as a real scope's does (it first
drew the cross level with the screen, so it turned with the head). The view is taken from just ahead of the front sight, on the sights' line (the
first version looked from the back of the gun at receiver height, straight into
its own front sight, which blocked the end of the view). It is rendered only while an eye is
behind the lens. It needs a frame slot of its own: the eyes use slots 0 and 1,
so the post filters now have three (`MAX_FRAME_SLOTS`), and the particle
renderers at least three. It renders after both eyes: the first view recorded
places the model shadow cascade, which has to be around the player for the
eyes, not down the scope. It has its own small framebuffer manager, swapped in
for the pass, and no water (the water renderer holds the eye-sized manager's
images). Its picture is blitted to an sRGB image (the encode) and copied to a
UNORM one that the 2D layer samples, and Flip draws it into each eye as a round
lens at the lens's place, from a flipped source rectangle (render targets are
top row first, the 2D layer's images bottom row first). The last view of the
frame is the one that clears the scene lists (`vrViewLast`).

**Physical reloading** (`Client::UpdateVRReload`) only decides when the reload
key is pressed: the server sees the ordinary reload. While the magazine is
out, the rifle and SMG skins draw the gun without it, for a reload progress
below zero, which the game itself never gives, so nothing changes for anyone
else. The rifle's `WeaponNoMagazine.kv6` is the first-person model, at twice the
resolution of the third-person `Weapon.kv6` (6x68x14 to 3x34x7, its pivot
doubled too), so it is drawn at half size to line up voxel for voxel; drawn
as it was, the rifle doubled in size the moment the magazine came out. The
SMG's two models are at the same resolution and line up as they are. The magazine in the off hand is the game's own model (a shell for the
shotgun).

**Your body** is the third-person torso and legs from your own eyes, 15cm
behind the head so that looking down shows the chest rather than the inside of
the neck, with the torso leaning to under the head as remote VR players' do.

**The keyboard** belongs to the pointer (`VRMenuPointer`), so it works for any
screen that takes typing. A view that takes typing but not the mouse (chat
during play) gets only the keys: clicks elsewhere would be shots, and the
client counts typing as a menu, so the trigger does not fire either.

The keyboard sits from a little above the middle of the panel to a little
below (0.36 to 0.76 of its height). The panel turns with the head, so a part of
it can only be looked at by moving the eyes, and eyes rest slightly down; 0.1.7
put the keyboard at the top whenever the text field was in the lower half,
which is where the chat's is, and typing meant looking up. It moves to the top
or bottom only when the text field is in that band.

Each key typed is also let go (`KeyEvent(key, false)` after the text). The UI's
`UIManager` repeats a typed character until some key comes up, which a real
keyboard always sends; without it, one press of B typed "bbbbbb" until the
next key (0.1.7).

### Recoil

The first VR builds had no recoil you could feel in your aim: the gun model
jolted and settled before the next shot, but the aim never moved, so an SMG
could be held on a head forever. On a monitor every shot turns the view up by
the weapon's recoil (rifle 0.05 rad, SMG 0.0125, shotgun 0.1, doubled walking
or in the air, halved crouching, with the vanilla sideways triangle wave), and
nothing turns it back: the player pulls the mouse down. In VR that code still
ran, but the next frame set the facing from the hand and threw it away.

Now `Player` hands a headset player's shot to the listener
(`PlayerRecoiledInVR`) with exactly the monitor's numbers, multipliers
included, and `VRRecoil` (in `VRState`, unit-tested) turns the gun by it,
after the aim smoothing so the filter never sees the kick. Pavlov, Onward and
H3VR all work this way: the gun moves, the aim follows the gun, a spring takes
most of it back, and a braced gun moves much less. Each shot is split into a
kick (40%) that springs back in about a fifth of a second and a climb (60%)
that only starts back 0.12 s after the last shot. That delay is longer than an
SMG's 0.1 s between shots, so a held trigger keeps all of the climb: 13
degrees in three seconds standing, against the monitor's 21 with nobody
pulling down, which in a headset the hand does a little of without thinking.
Within a second of letting go it is back. A rifle's settles between shots.
Both parts are critically damped springs stepped exactly, so the muzzle never
dips below where it was aimed and a long frame cannot make it ring. The climb
is capped at 30 degrees. A braced gun (two hands or the stock) counts as aiming
down the sights: the walking doubling is taken back, and nothing else. The
first version also took 40% off two-handed and 60% shouldered, as VR shooters
do, and had a `vr_recoil` setting down to 0; both were removed (2026-10-10):
recoil is worked out by the client and nothing on the server checks it, so a
headset player must never get less of it than a monitor player.

Tried in a headset (2026-10-10): the guns fire with it and nothing broke;
how strong it should be is still open.

The model's jolt in the hand stays, at half its old size (it was all the
recoil there was).
`Server/test/vr_state_test.cpp` checks a rifle shot rises and settles without
dipping, a 30-shot SMG burst climbs past eight shots' worth, with a control
that the same shots a second apart do not build up, the sideways part
returns, and that no way of holding the gun gets less than a monitor's aimed
recoil, with one hand getting the monitor's unaimed recoil as the control.

### Steadier aim

Two things VR shooters do, both in `Client::UpdateVRAimAssist`, both only
turning the gun hand's forward (`SetVRToolForward`), so the gun drawn, the laser
and the shot stay one line.

**The stock** (Pavlov, H3VR, Onward), on the rifle only: the SMG and shotgun
are fired from the hands. With the rifle two-handed and the gun hand
within 22cm of a virtual shoulder (20cm below the eyes, 13cm to the gun hand's
side, 4cm back, in a frame that turns with the head but stays level, so nodding
does not swing the gun), the gun aims from that shoulder through the front
hand. It lets go past 30cm. The body sways far less than two wrists.

**Smoothing** is a one-euro filter (`VRAimFilter`), the usual answer to
tracking jitter: a low-pass whose cutoff rises with the aim's angular speed.
Still, it filters at 10Hz down to 0.5Hz as `vr_aimSmooth` goes 0 to 1; each
radian per second of turning adds 2Hz. So the tremor of a held aim is filtered
hard and a swing onto a target passes with little lag. Guns only: a lagging
spade would feel wrong. The filter moves once a frame; the second
hand-transform build per frame (for the scene) only puts its result back.

Two mistakes in 0.1.7, both worth knowing before touching this. The filter's
input was read off the gun hand's axes, which by then already held last
frame's smoothed aim: fed its own output, the filter saw no motion, and the
rifle froze pointing one way (grabbing it two-handed only moved where it froze).
The input is now worked out afresh each frame from the hands (the shoulder
through the front hand, hand to hand, or the hand's own `ownForward`), and
`VRAimFilter` says so where it is declared. And the speed estimate was smoothed
at 5Hz: tracking shake that flips direction every frame then read as motion
and opened the filter to the shake itself. It is 1Hz now, the one-euro
default, and `vr_state_test.cpp` checks all three: a turning hand is followed,
a shaking one is steadied, and 0 passes straight through.

### Solid tools

A tracked hand goes wherever the arm reaches, walls included. With it went the
gun, and with the gun the ray it shoots along, so a hand pushed through a wall
could shoot or dig from the far side. The flat game never had the problem: its
tools start at the eye.

`Client::ApplyVRToolCollision`, through the pure `VRToolPushback` (in
`VRState.h`, so the tests can give it a fake wall), runs in two steps. First
the hand is stopped on the line from the eyes to it, which are known to be
clear (`VRHeadClearance`): whatever wall is between them, the hand stays on
this side. Then the tool is followed along its forward from the stopped hand,
as far as it reaches (`VRToolReach`, grip to tip read off the models: rifle
0.85 blocks, SMG 0.6, shotgun 0.8, spade 0.85, block 0.2, grenade 0.1). If the
tip would be in a block, the hand is pulled back along the tool until the tip
is clear, which is how Boneworks and Pavlov show a gun pushed into a wall:
it slides back in the hands. Moving the hand rather than only the model
matters, because the hand's pose also places the tool's ray (`SetVRToolRay`)
and the laser, so what is drawn, what is aimed and what is shot stay one line.
A two-handed gun takes the barrel hand along with it. The other hand is only
stopped, as it holds nothing that reaches.

The hands are rebuilt twice a frame (input, then the scene), and the aim is
settled after the first rebuild (two-handed grip, stock, smoothing), so the
collision runs at the end of every rebuild and once more after the aim assist.
Only while alive in the body (`solidHands`): a dead or spectating view's hands
are not in the map. Players are left out on purpose: in a crowd of teammates a
gun that stops at people would be in the way all the time.

Tried in the tests only, not yet in a headset: the reach per tool is read off
the models, and a tool held very close to a wall can find the retreat taking
its grip behind the eyes. The margin is 0.03 blocks.

Tool collision is off by default for now (`vr_toolCollision`; it got in the way
of aiming behind cover), so a gun can again go through a wall. What it does
there is checked against the line from the eye instead (`VRLineOfSight`, a ray
through the map, where `VRHeadClearance` only tests the far end and so is
fooled by the clear side of a thin wall). A gun whose aim point the eye cannot
reach shoots the wall in between: from beyond it, the gun's ray found whoever
stood behind, and the shot and the laser swung to them, which a tester called
an ESP. Its laser is not drawn while the barrel is through. A scope whose lens
is through a wall shows black and lends nothing to the aim: its view was the
far side's. The spade and blocks, which act from the hand where nobody can see
it (no vr_presence), act from the eye when the line to the hand is blocked.

### The wrist menu

The wrist panel already sits where a hand can reach it, so it is also where
the controls you need now and then go. Bringing the tool hand's grip within
22cm of the panel (`VRWristReached`; it stays open until 40cm) opens it. The
panel grows 60% over 0.15s, upwards so it grows off the arm rather than into
it, and while it is open the show/hide test (`VRPlaceWristPanel`'s `inUse`)
is much looser, since poking at it turns the wrist about. The canvas stays
960x600; only its size in the room changes, so the same pixels are larger.

It is used the two ways VR menus usually are: a touch (the controller's aim
point coming within 2cm of the panel's face, re-armed at 5cm, so resting on a
button does not repeat it) or a point and a trigger pull
(`VRRayHitsWristPanel`, from the front only). A trigger pull that begins on
the panel belongs to it until it is let go, so sweeping off the panel with the
trigger held does not become a shot, and no shot fires while the pointer is on
the panel. The laser ends at the panel, and a red dot on the canvas shows where
it points.

The buttons are laid out in one place (`Client::VRWristMenuLayout`) that both
the drawing and the input read, so they cannot disagree. Chat opens the ordinary
chat field (`EnterGlobalChatWindow`, `EnterTeamChatWindow`), which in a headset
is in front with the pointing keyboard under it. Quick chat sends at most once
a second. The colours are the flat game's palette (`PaletteView`, 8 hues, dark
to light), so a colour picked here is highlighted in the flat palette too.

### Night and lamps

Fran6nd's Daytime and Weather and Flashlight extensions (zerospades PR #76)
were written for the OpenGL renderer; VR draws with Vulkan, so they are ported
to it here. The scene carries `sunlight` and `daylight` (both 1 by day, 0 at
midnight) and the Vulkan shaders read them where OpenGL's do: the sun's light,
its shadow and the radiosity it bounces scale by the sunlight; the sky's light,
the fog and the sky itself by the daylight. Each lit shader's spare push
constant slots carry them (the map's `_pad`/`_pad2`, water's `_pad0`); the
voxel models have one spare slot, so the two are packed into it as
`round(s*255)*256 + round(d*255)`. The ambient light's minimum-brightness floor
is scaled by the daylight too, or a night would never be dark.

Flashlights are spotlights with a soft cone edge, measured from the cone's axis
(Fran6nd's fix of a cut that lit one quadrant), and they do not shine through
walls: each lit pixel walks to the light block by block through
`VulkanMapOccupancy`, an R8 3D texture of the solid blocks kept in step with
the map, bound as set 1 of the dynamic-light passes. The headlamp model is
emissive, which the Vulkan voxel renderer did not support: an emissive voxel's
flag rides in the vertex's spare byte (location 5) and those voxels skip the
lighting. The lens glare goes into each eye at its own depth, like the labels.

In a headset your lamp is on your forehead and points where you look, as
everybody else sees it; the wrist menu's **Light** and a squeeze of the other
hand within 18cm of the headset switch it (that grip is then not the
scoreboard).

### The VR presence extension

Protocol extension 58, packet 122, sub packet in byte 1, following the Teamplay
extension's convention. Every client of this build advertises it, headset or
not, so a monitor player sees VR players too. A pose carries the head and both
hands as offsets from the sender's eye (1/512 block, int16) and unit
quaternions in world axes (int16), plus the body position and a few button
bits: 58 bytes on the wire, 20 to 30 times a second, unreliable and
unsequenced. `Sources/Client/VRNetProtocol.h` has the layout.

Other VR players are drawn with their tracked head, arms solved from shoulders
beside the head to each hand (two bones, elbows down and out), and the tool in
their tool hand. The torso leans from the hips to under the head: turned about
the hips and moved along itself until its top is at the neck the head hangs
from. Leaning, ducking and a head held high then keep the head on the
shoulders, while the legs stay where the body stands. Before this, the torso
stayed on the legs and the head went where it was tracked, which looked like a
chest sunk below a floating head. The tool is its ordinary third-person skin with the skin's
origin moved from the shoulder into the hand, so tracers, the muzzle flash and
casings come out of the gun they are actually holding. Each tool's grip point
in the skin's frame was read off its voxel model (`ClientPlayer::VRToolOriginMatrix`).

### Built so it would run on a Quest

Every VR feature is meant to run on a standalone headset as well: through
standard OpenXR, with only the controllers, in one process, within a mobile
GPU's budget. What only a PC can do (a spectator camera, supersampling) is an
optional extra on top. Three things keep that true before there is an APK:

- **One process.** The training course's server runs on a thread of the game
  (see Training above), because an Android app cannot start a second copy of
  itself.
- **No desktop OpenGL in the headset's builds.** `USE_OPENGL` (on by default,
  off on Android) leaves the OpenGL renderer, GLEW and the startup screen's
  OpenGL probe out entirely; the renderer setting then defaults to Vulkan, and
  a config that names `gl` gets Vulkan. The PC and Mac builds keep OpenGL as a
  flat renderer; it was never part of VR.
- **CI builds what the headset will.** `ci.yml` checks Linux arm64 with Vulkan
  and OpenXR and no OpenGL (AngelScript's ARM64 native calls, ENet, OpenAL and
  the voxel code on ARM), and builds the Quest APK: Android arm64 with the NDK
  and vcpkg's `arm64-android` triplet, which builds the game as `libmain.so`,
  the library SDL's Android activity loads, packaged by `android/build-apk.sh`
  (below). The Android
  build takes the system's Vulkan loader from the NDK (vcpkg's is not for
  Android) and compiles shaders with the build machine's `glslangValidator`.
  It builds and links locally (NDK r27c on Windows); getting there found four
  things that were not portable: AngelScript's ARM64 native-call code was left
  out of Android builds by its CMake (and its string add-on's locale restore
  had an `#endif` in the wrong place), MumbleLink used POSIX shared memory,
  which Android's libc does not have, VMA linked Vulkan 1.3 entry points the
  NDK's `libvulkan.so` does not export (it now loads them through the device),
  and the desktop Linux link line added X11, `rt` and `pthread`.

**Optional extensions are negotiated, not assumed.** `Sources/VR/XrCaps.cpp`
lists every optional OpenXR extension a feature might use (depth layers,
cylinder layers, refresh rates, foveation, space warp, passthrough, hand, eye
and body tracking, haptic envelopes, the Quest Pro and Quest 3 controller
profiles). The instance enables each one the runtime offers, and features
check `OpenXRSystem::GetCaps()`, never the platform. An extension whose
prerequisite is missing is dropped with it (foveation needs
`XR_FB_swapchain_update_state`; hand aim and hand mesh need hand tracking),
because a runtime may refuse the whole instance over one asked for alone. The
log lists each row as on, off or absent. `vr_disableExtensions` takes rows
away, which is how a PC runtime is made to lack what a Quest lacks.
`XR_KHR_android_create_instance` and the loader's Android start-up are the
only OpenXR code that differs by platform (`#ifdef __ANDROID__` in
`OpenXRSystem.cpp`). `Server/test/xr_caps_test.cpp` checks the table, each
case with a control.

**The Quest build** (`android/`). `android-build` (the NDK, vcpkg's
arm64-android) makes `libmain.so`, the game with SDL linked in, and
`android\build-apk.ps1` packages it without Gradle: SDL's Java activity (from
the SDL source vcpkg downloaded) and a small one of ours that loads OpenAL and
the game, compiled with a JDK 17 against the Java 8 API (SDL's lambdas need
`java.lang.invoke`, which `android.jar` lacks; d8 rewrites them) and dexed;
`aapt2` links the manifest (the Quest's VR intent category, head tracking, the
OpenXR loader's broker permissions and queries) with the paks as assets;
stripped `libmain.so` and `libopenal.so` go in beside it; zipalign; a local
debug key. `-Install` puts it on a headset over adb. 16.6MB.
`android/build-apk.sh` does the same on Linux, for CI.

The manifest has no version: both scripts give aapt2 the release's, from
`ZEROSPADES_VERSION_*` in CMakeLists.txt (or `-Version` / `--version`), with
versionCode major*10000 + minor*100 + patch (0.3.0 is 300), so it rises every
release; a headset installs an update only over a lower versionCode and the
same signing key. A release (`release.yml`) signs with the key every Quest build
so far has been signed with, from the repository's secrets
(`ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASS`, `ANDROID_KEY_ALIAS`); every
other CI build signs with a throwaway key, which a headset with a release on it
will refuse as an update.

On the headset the paks are copied out of the APK into the app's storage
(`files/Paks`) whenever the APK's `Resources/index.txt`, a name and SHA-1 a
pak, differs from the copy kept beside them, so the rest of the game reads
files as everywhere else. The first version copied them to `files/Resources`
and recopied one only when its size changed, and no update ever reached the
headset: SDL opens a relative path in the app's own storage before the APK's
assets, so "Resources/x.pak" was the old copy from then on, the size always
matched, and the water fix for the right eye shipped three times without
arriving. The copies moved, the old ones are deleted, and the fingerprint
catches a changed pak of the same size.

When something fails on a headset there is nothing to see: a Quest 2 sat on
its loading screen with the game's "Fatal Error" dialog open behind it, and
logcat held only that window's title. So on Android every log line also goes
to logcat (tag `ZeroSpades`), the settings and `SystemMessages.log` live in
the app's external storage (`Android/data/com.zerospades.vr/files/UserResources`,
readable with `adb pull` or SideQuest from any build), and a fatal error is
logged and quits to the headset's home instead of opening a dialog. A standalone headset is always one: VR on, Vulkan, no mirror
window, no startup window, and `vr_profile auto` picks the mobile pass set.
SDL's text input stays off, since it would bring up Android's keyboard; the VR
keyboard types instead.

Tried on Android's emulator (an x86_64 phone running the arm64 build through
its ARM translation, no OpenXR runtime): it loads, copies the paks, finds no
headset and falls back to the flat menu, through Vulkan with 4x MSAA, and lists
servers from the internet. Three bugs found on the way, all fixed: OpenAL's
lookup opened a driver library by name before asking the built-in OpenAL Soft,
and the name-less fallback made a string of the null `SDL_GetBasePath()` that
Android returns (a crash at start); the swapchain asked for the surface's
rotation, which on a portrait-first device drew the menus on their side; and
the Android keyboard came up over the menu.

On a Quest 3 (2026-10-11): the runtime (Oculus 209), almost every extension,
the Adreno 740, multiview with 4x MSAA, both controllers and the menu scene all
came up, and then the system killed the game for running out of memory a few
seconds in. On a PC the same VR menu held 2.9GB, which a PC hides in its video
memory and a Quest, whose memory is all one, does not. Two things were most of
it, both doing nothing: radiosity and ambient shadow kept whole-map 3D textures
and CPU copies of them (about 800MB a map) with radiosity off, and the water's
mirror kept two eye-sized 4x MSAA targets that only the mirror pass, off below
`r_water 2`, draws. With those a chunk and 1x1, the VR menu with its scene and
the mobile set was 1.7GB (device memory 1583MB to 816MB). Leaving the game
also killed it: SDL blocks the main loop while the activity is paused, so the
session never reached `xrEndSession`. The loop now runs on in the background,
sleeping while there is no session.

**128 bytes of push constants.** The first Quest 2 (Adreno 650) sat on its
loading screen; its log, once the log could be read (above), said the sprite
pipeline failed to build with `VK_ERROR_UNKNOWN`. Vulkan promises 128 bytes of
push constants and the Adreno 650 has no more, where a Quest 3 and every
desktop GPU have 256. The scene's blocks ran to 272: sprites and long sprites
192, soft sprites 152, the map 192 (lit) and its dynamic lights 224, models
188 or 268, their dynamic lights 272 and x-ray 192, and fog shadows 144. The
sprites were only first. Two of those were over 256 too, so even the desktop
builds had been out of spec: the validation layer reported
`VkPushConstantRange-size-00298` and `vkCmdPushConstants-offset-01796` on
every run, and the drivers happened to cope.

Upstream's renderer has since moved what did not fit into descriptor sets of
its own: the pass's view (`SceneView`, set 3) and the frame's lights (set 2),
with only per-draw data left in push constants, each block checked against
128 bytes where it is declared (`VulkanPushConstants.h`).

`SpirvCache` now refuses a shader whose push constants are over 128 bytes
(`CheckPushConstants`, which the post filters' loaders call too), on every GPU
and not only on the ones with no more, so the next one fails on the desk it
was written at. Its size reading agrees with `spirv-reflect` on all 98 shaders.

**The mobile pass set** (`vr_profile`, Main.cpp's `kMobileProfile`). A
standalone headset's GPU is tiled: every full-screen pass is a whole load and
store of each eye, so what is only polish has to go. `vr_profile mobile` turns
off bloom, FXAA (for 4x MSAA, which a tiler resolves cheaply), HDR and its
exposure pass, fog shadows, soft particles, outlines, the lens flare, colour
correction, depth of field, camera blur, SSAO and TAA, uses the simpler water,
halves the shadow map and the anisotropy, and draws both eyes in one pass. It
is applied the way a command-line override is, for this run only, so the
config keeps your own settings. `auto` (the default) is `mobile` on a
standalone headset (Android) and `pc` elsewhere; it only applies in a headset.
On this PC, in a Training game on the null headset, the eyes' GPU time went
from 1.31ms with the PC settings (bloom, colour correction, soft particles,
the best water, one eye at a time) to 0.75ms with the mobile set, 4x MSAA
included. Still to do for a tiler: resolving MSAA inside the render pass rather
than with `vkCmdResolveImage` after it, and a cheaper night (the map occupancy
ray walk for lamps).

**No window.** `vr_mirror 0` makes the Vulkan device with no surface and no
swapchain: a standalone headset has no window to show, so on PC this is the
Quest's path. The window is still created, hidden, because SDL's events come
through it. The renderer is not told: the device hands it three images of its
own as the "swapchain", its acquire signals the semaphore the renderer waits
on with an empty submit, and its present waits on the one the renderer
signals, so every frame's synchronisation is exactly as with a window. The VR
frame skips copying into them. If the headset session then fails to start
there is no monitor to fall back to, so the game says so and stops, rather
than run in a window nobody can see. Tested on SteamVR's null headset: the
session runs, and the validation layers report nothing the windowed run does
not.

Testing without a headset uses SteamVR's null driver: `requireHmd false`,
`forcedDriver null` and `driver_null.enable true` in `steamvr.vrsettings`, and
SteamVR started *before* the game (otherwise its compositor is not up and
`xrGetVulkanGraphicsDevice2KHR` fails with a runtime failure). The null
headset is 1512x1680 per eye at 90 Hz, with no controllers.

**Controllers only.** `vr_controllersOnly 1` drops every keyboard and mouse
event (and the polled Ctrl state) while a headset frame is active, so a screen
that still needs a key shows up on PC first. Gameplay, menus, chat and text
entry all have controller paths already (the pointer, the wrist menu, the
panel keyboard). The one screen that cannot work this way is key binding,
which captures a key press; VR's controls are fixed, so it has nothing to bind.

## Open

- The HUD is not shown on the monitor while 3D is being drawn (the mirror is a
  plain copy of the left eye).
- Screen tints (hit flash, underwater tint) are not applied in VR.
- Grip points per tool want tuning in the headset.
- With a headset in use but asleep, the monitor shows the 16:9 VR layout
  stretched over the window, and a window of another shape shows the 3D
  stretched too.
- The wrist panel's place over the wrist (8cm down the grip, 4cm off the back
  of the hand), its show/hide angles and the 15 degree aim tilt are first
  guesses, not yet tried in a headset.
- Not yet tried in a headset: the scope, physical reloading, your body, the
  comfort camera, holding the frame rate, the keyboard, the VR settings tab.
  The lens and magazine-well places are first guesses.
- The scope shows no water.
- Off-hand weapon actions: a shotgun pump (off hand back and forward along
  the barrel between shots) and a grenade pin (the off hand pulls it to start
  the cook) were built and unit-tested in 4a96027, both optional and never
  faster than the trigger, then taken out on 2026-10-11 until there is a better
  idea of how they should work in play. That commit has the stroke and pin
  detection (`VRPumpStep`, `VRPinStep`) and their tests.
- The CI and release workflows (`ci.yml`, `release.yml`) have not run on
  GitHub yet. The Android build is known to build and link locally and the
  APK to package with `build-apk.ps1`; `build-apk.sh` and the Linux arm64 job
  are untested.
- Multiview (`vr_multiview`) has not been seen in a headset.
- With Fog2 (`r_fogShadow 2` and radiosity) the per-eye path's right eye shows
  an unfogged void past a stepped edge in the far corner of the overview
  camera, where multiview draws fog over terrain. It does not happen without
  Fog2, and the cause is not found yet.
- Under HDR, auto exposure's two gain render passes have different dependency
  counts (`VkRenderPassBeginInfo-renderPass-00904`); predates multiview.
- The off-centre eyes are verified on a monitor, not yet in a headset. The
  water's reflections and refractions were not in view in that test.
- Validation warnings in VR that predate this work: a depth image copied into
  a colour one (`vkCmdCopyImage-srcImage-01551`), pipelines drawn in a render
  pass whose dependencies differ from the one they were made with
  (`vkCmdDrawIndexed-renderPass-02684`), and SPIR-V capabilities the device
  does not declare (`VkShaderModuleCreateInfo-pCode-08740/08742`).
