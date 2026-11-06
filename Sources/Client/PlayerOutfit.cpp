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

#include "PlayerOutfit.h"

#include "NetProtocol.h"
#include <Core/Debug.h>
#include <Core/FileManager.h>

namespace spades {
	namespace client {

		namespace {
			const std::string kNone;

			// The model set each outfit is drawn with, by its spec name, so art for
			// one is a folder of that name; Undead is our zombie. One with no folder
			// draws the usual parts.
			const std::string kFolders[PlayerOutfits::NumOutfits] = {
			  "",        "Zombie",  "Scout",     "Royal",   "Vampire", "Miner",
			  "Ghillie", "Brawler", "Scientist", "Butcher", "Convict", "Builder",
			  "Robot",   "God",     "Demon",     "Bodyguard", "Terrorist"};

			// A part a set may leave out because another of its parts serves for it:
			// the right leg is the left one, as the classic game drew both.
			const char* StandInFor(const std::string& part) {
				if (part == "LegRight")
					return "Leg";
				if (part == "LegRightCrouch")
					return "LegCrouch";
				if (part == "ArmRight")
					return "Arm";
				if (part == "UpperArmRight")
					return "UpperArm";
				return nullptr;
			}
		} // namespace

		const std::string& PlayerOutfits::FolderOf(int outfit) {
			// 17-254 are reserved and drawn as 0.
			if (outfit <= 0 || outfit >= NumOutfits)
				return kNone;
			return kFolders[outfit];
		}

		void PlayerOutfits::SetPlayerOutfit(int playerId, int outfit) {
			if (outfit == kTeamOutfit)
				playerOutfits.erase(playerId);
			else
				playerOutfits[playerId] = outfit;
		}

		void PlayerOutfits::SetTeamOutfits(const std::unordered_map<int, int>& outfits) {
			teamOutfits = outfits;
		}

		void PlayerOutfits::PlayerSpawned(int playerId, int team) {
			spawnOutfits[playerId] = TeamOutfit(team);
		}

		void PlayerOutfits::PlayerLeft(int playerId) {
			playerOutfits.erase(playerId);
			spawnOutfits.erase(playerId);
		}

		void PlayerOutfits::MapStarted() {
			playerOutfits.clear();
			spawnOutfits.clear();
		}

		int PlayerOutfits::TeamOutfit(int team) const {
			auto it = teamOutfits.find(team < 0 ? kSpectatorTeam : team);
			return it != teamOutfits.end() ? it->second : Soldier;
		}

		int PlayerOutfits::GetOutfit(int playerId, int team) const {
			auto own = playerOutfits.find(playerId);
			if (own != playerOutfits.end())
				return own->second;
			auto atSpawn = spawnOutfits.find(playerId);
			if (atSpawn != spawnOutfits.end())
				return atSpawn->second;
			// Introduced without a spawn seen here (already in the game when we
			// joined): their team's as it is now.
			return TeamOutfit(team);
		}

		bool PlayerOutfits::Exists(const std::string& path) const {
			auto it = exists.find(path);
			if (it != exists.end())
				return it->second;
			bool there = FileManager::FileExists(path.c_str());
			exists[path] = there;
			return there;
		}

		void ApplyPlayerOutfitPacket(NetPacketReader& r, PlayerOutfits& outfits) {
			if (r.GetNumRemainingBytes() < 1) {
				SPLog("Dropped a truncated Player Outfit packet");
				return;
			}
			int sub = r.ReadByte();
			switch (sub) {
				case PlayerOutfits::kSubSetOutfit: {
					if (r.GetNumRemainingBytes() != 2) {
						SPLog("Dropped a Set Outfit of the wrong size");
						return;
					}
					int player = r.ReadByte();
					int outfit = r.ReadByte();
					outfits.SetPlayerOutfit(player, outfit);
				} break;
				case PlayerOutfits::kSubTeamOutfits: {
					if (r.GetNumRemainingBytes() % 2 != 0) {
						SPLog("Dropped a Team Outfits of the wrong size");
						return;
					}
					std::unordered_map<int, int> teams;
					while (r.GetNumRemainingBytes() >= 2) {
						int team = r.ReadByte();
						int outfit = r.ReadByte();
						// 255 is no team's outfit: worn as 0.
						teams[team] = outfit == PlayerOutfits::kTeamOutfit ? 0 : outfit;
					}
					outfits.SetTeamOutfits(teams);
				} break;
				default:
					// Sub packets are how the extension grows: a newer one is skipped.
					SPLog("Ignoring an unknown Player Outfit sub packet %d", sub);
					break;
			}
		}

		std::string PlayerOutfits::ResolvePart(const std::string& name, const std::string& part,
		                                       const std::string& fallback) const {
			if (name.empty())
				return fallback;
			const std::string folder = "Models/Player/Custom/" + name + "/";
			std::string own = folder + part + ".kv6";
			if (Exists(own))
				return own;
			if (const char* other = StandInFor(part)) {
				std::string stand = folder + other + ".kv6";
				if (Exists(stand))
					return stand;
			}
			return fallback;
		}
	} // namespace client
} // namespace spades
