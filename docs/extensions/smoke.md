# Smoke

Smoke the server puts in the world: a wide cloud or a rising column, in a
colour and an opacity, of a size, for a time.

| ------------: | ------------- |
| Extension ID: | `0x3F`        |
| Packet ID:    | `0x7F`        |
| Version:      | 1             |
| Type:         | `HAS_PACKETS` |

Written in the style of [aosprotocol](https://github.com/zerospades/aosprotocol)'s
extensions, for ZeroSpades X; not registered there (yet). `0x3F` is clear of
every id registered or proposed in aosprotocol as of 13 November 2026.

### Sub Packets:

| Sub ID | Name        | Direction        | Size |
|--------|-------------|------------------|------|
| 0      | Smoke       | Server -> Client | 25   |
| 1      | Clear Smoke | Server -> Client | 3    |

## What smoke is

Smoke is only drawn. It hides what is behind it on the clients that draw it,
and does nothing else: no damage, no block, nothing the server simulates. A
client without the extension sees nothing, so a server should use it where
that is fine (a marker, a cloud where something happened, a trail), or know
that only some of its players are hidden by it.

A client draws smoke whatever its particle setting, since turning particles
down must not see through it.

Smoke is an event in time, not part of the map: [Map Start](https://github.com/zerospades/aosprotocol/blob/main/protocol075.md#map-start-075)
clears it all. A player who joins while a smoke is still coming does not see
it unless the server sends it to them again (with what is left of its
duration).

## Sub ID 0: Smoke

| Field Name    | Field Type | Example        | Notes |
|---------------|------------|----------------|-------|
| Packet ID     | UByte      | `0x7F`         | Always `0x7F`. |
| Sub Packet ID | UByte      | `0`            | Always `0` for this sub-packet. |
| Smoke ID      | UByte      | `3`            | `0`-`254`: the server's name for it, for Clear Smoke; a new Smoke with an id in use replaces the old one. `255`: none, it cannot be cleared. |
| Style         | UByte      | `0`            | See [Style](#style). |
| X             | LE Float   | `256.5`        | Where it comes from, in world coordinates. |
| Y             | LE Float   | `256.5`        | |
| Z             | LE Float   | `60.0`         | As everywhere, Z grows downwards: the ground is a larger Z. |
| Radius        | UByte      | `5`            | How far it spreads, in blocks, `1`-`255`. A grenade's cloud is about `5`. |
| Red           | UByte      | `210`          | The colour. |
| Green         | UByte      | `210`          | |
| Blue          | UByte      | `210`          | |
| Opacity       | UByte      | `250`          | `0` clear to `255` opaque, for each puff; puffs over one another are thicker. |
| Duration      | LE UShort  | `100`          | How long it keeps coming, in tenths of a second (`100` = 10 s, up to 6553.5 s). |
| Fade          | UByte      | `50`           | How long it takes to go once it stops, in tenths of a second (`50` = 5 s). |
| Flags         | UByte      | `1`            | Bit 0: hiss, a sound where it comes from, for as long as it keeps coming. Other bits: `0`, ignored. |

### Style

| Value | Name   | Look |
|-------|--------|------|
| 0     | Cloud  | A wide, low cloud, spreading out over its radius: a smoke screen. |
| 1     | Column | A thin plume rising straight up from where it is: a marker. Radius sets how wide. |

A client draws a style it does not know as `0`.

## Sub ID 1: Clear Smoke

| Field Name    | Field Type | Example | Notes |
|---------------|------------|---------|-------|
| Packet ID     | UByte      | `0x7F`  | Always `0x7F`. |
| Sub Packet ID | UByte      | `1`     | Always `1` for this sub-packet. |
| Smoke ID      | UByte      | `3`     | The smoke to stop, or `255` for every one. |

The smoke stops coming; what is already out fades over its Fade. An id with
no smoke is ignored.

## Examples

A smoke screen, 10 s, grey, with the hiss:

    7F 00  FF  00  <x> <y> <z>  05  D2 D2 D2 FA  64 00  32  01

A green marker column over a supply drop, two minutes, cleared when taken:

    7F 00  03  01  <x> <y> <z>  02  30 E0 40 E6  B0 04  1E  00
    ...
    7F 01  03

## Later

A thrown smoke grenade, a player's own, belongs with the weapons a server
gives (aosprotocol's Custom Weapon draft, #22): the server would know the
player threw smoke and send Smoke where it lands. Nothing here changes what a
grenade does.

See [Extensions](https://github.com/zerospades/aosprotocol/blob/main/extensions/extension.md)
for how the extension is negotiated.
