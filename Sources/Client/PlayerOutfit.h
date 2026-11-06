/*
 Copyright (c) 2026 ZeroSpades X developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace spades {
	namespace client {
		class NetPacketReader;

		/**
		 * Client-side state of the *Player Outfit* protocol extension (extension id
		 * 0x35, packet id 0x75), aosprotocol's extensions/player-outfit.md.
		 *
		 * The server dresses a team, or one player, in an outfit (a number, 0 the
		 * usual soldier), and this client draws that player's body from the outfit's
		 * model set instead of the usual one. The sets ship with the client, one
		 * folder each under Models/Player/Custom/<name>/; the server only ever sends a
		 * number, so it cannot make a client load a file it does not already have. A
		 * part a set leaves out is drawn as usual, so a set can be a whole body or
		 * just a head, and an outfit with no set at all is drawn as 0, as the spec
		 * asks.
		 *
		 *     0  Set Outfit    S->C  player(1) outfit(1)   255: wear the team's
		 *     1  Team Outfits  S->C  (team(1) outfit(1))*  a team not listed wears 0
		 *
		 * A player's own outfit lasts until Player Left or Map Start. One wearing
		 * their team's takes it when they spawn or are introduced, so a new Team
		 * Outfits shows only at their next spawn. An unknown sub packet is skipped.
		 */
		class PlayerOutfits {
		public:
			static constexpr std::uint8_t kExtensionId = 0x35;
			static constexpr std::uint8_t kPacketId = 64 + kExtensionId;
			static constexpr std::uint8_t kSubSetOutfit = 0;
			static constexpr std::uint8_t kSubTeamOutfits = 1;
			static constexpr int kTeamOutfit = 255;
			static constexpr int kSpectatorTeam = 255;

			enum Outfit {
				Soldier = 0, Undead, Scout, Royal, Vampire, Miner, Ghillie, Brawler,
				Scientist, Butcher, Convict, Builder, Robot, God, Demon, Bodyguard,
				Terrorist, NumOutfits
			};

			/** The model set an outfit is drawn with, or empty for the usual parts. */
			static const std::string& FolderOf(int outfit);

			void SetPlayerOutfit(int playerId, int outfit);
			/** Replaces every team's outfit; a team not in `outfits` wears 0. */
			void SetTeamOutfits(const std::unordered_map<int, int>& outfits);
			/** A player wearing their team's outfit takes it now. */
			void PlayerSpawned(int playerId, int team);
			/** A player's outfit goes with them; the slot may be someone else's next. */
			void PlayerLeft(int playerId);
			/** Map Start: every player id back to its team's outfit. */
			void MapStarted();

			/** The outfit this player is drawn in. */
			int GetOutfit(int playerId, int team) const;
			/** The model set this player is drawn with, or empty for the usual one. */
			const std::string& GetModelName(int playerId, int team) const {
				return FolderOf(GetOutfit(playerId, team));
			}

			/**
			 * The file to draw `part` ("Head", "LegRight"...) from for a player drawn
			 * with set `name`: the set's own file, or one it uses in its place (a set's
			 * Leg serves for its LegRight), or `fallback`, the usual one.
			 */
			std::string ResolvePart(const std::string& name, const std::string& part,
			                        const std::string& fallback) const;

			const std::unordered_map<int, int>& GetTeamOutfits() const { return teamOutfits; }
			const std::unordered_map<int, int>& GetPlayerOutfits() const { return playerOutfits; }

		private:
			std::unordered_map<int, int> teamOutfits;
			// Set Outfit, other than 255.
			std::unordered_map<int, int> playerOutfits;
			// The team outfit each player took at their last spawn.
			std::unordered_map<int, int> spawnOutfits;
			// Whether a set's file is there, asked once per file.
			mutable std::unordered_map<std::string, bool> exists;

			int TeamOutfit(int team) const;
			bool Exists(const std::string& path) const;
		};

		/**
		 * A Player Outfit packet (after its id) applied to `outfits`, for a game and
		 * for a demo alike. A malformed one is dropped; an unknown sub packet skipped.
		 */
		void ApplyPlayerOutfitPacket(NetPacketReader& r, PlayerOutfits& outfits);
	} // namespace client
} // namespace spades
