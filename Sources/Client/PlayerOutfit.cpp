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

			// The model set each outfit is drawn with, so art for one is a folder
			// under Models/Player/Custom: by its spec name, or the zombie role skins
			// of zerospades-userskins (Serius_Sim's; the plain Zombie is siecvi's).
			// One with no folder draws the usual parts.
			const std::string kGrunts[] = {"ZombieGrunt1", "ZombieGrunt2", "ZombieGrunt3"};

			const std::string& NamedFolder(int outfit) {
				static const std::unordered_map<int, std::string> folders = {
				  {PlayerOutfits::Scout, "Scout"},         {PlayerOutfits::Ghillie, "Ghillie"},
				  {PlayerOutfits::Bodyguard, "Bodyguard"}, {PlayerOutfits::Terrorist, "Terrorist"},
				  {PlayerOutfits::Convict, "Convict"},     {PlayerOutfits::Brawler, "Brawler"},
				  {PlayerOutfits::Miner, "Miner"},         {PlayerOutfits::Builder, "Builder"},
				  {PlayerOutfits::Scientist, "Scientist"}, {PlayerOutfits::Butcher, "Butcher"},
				  {PlayerOutfits::Royal, "Royal"},         {PlayerOutfits::Robot, "Robot"},
				  {PlayerOutfits::Vampire, "Vampire"},     {PlayerOutfits::Mummy, "Mummy"},
				  {PlayerOutfits::Demon, "Demon"},         {PlayerOutfits::God, "God"},
				  {PlayerOutfits::Mascot, "Mascot"},       {PlayerOutfits::Tiger, "Tiger"},
				  {PlayerOutfits::Penguin, "Penguin"},     {PlayerOutfits::Bear, "Bear"},
				  {PlayerOutfits::Unicorn, "Unicorn"},     {PlayerOutfits::TRex, "TRex"},
				  {PlayerOutfits::Banana, "Banana"},       {PlayerOutfits::BlockStack, "BlockStack"},
				  // Survive's brutes, each in its own colour
				  {PlayerOutfits::Spitter, "ZombieSpitter"},
				  {PlayerOutfits::Leaper, "ZombieLeaper"},
				  {PlayerOutfits::Caller, "ZombiePurple"},
				  {PlayerOutfits::Breaker, "ZombieYellow"},
				  {PlayerOutfits::Toppler, "Zombie"},
				  {PlayerOutfits::Turret, "Turret"},       {PlayerOutfits::Projector, "Projector"},
				  {PlayerOutfits::DishAntenna, "DishAntenna"},
				  {PlayerOutfits::MastAntenna, "MastAntenna"},
				};
				auto it = folders.find(outfit);
				return it != folders.end() ? it->second : kNone;
			}

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

		const std::string& PlayerOutfits::FolderOf(int outfit, int playerId) {
			// The plain zombie: one of the grunts, the same one for a player id
			if (outfit == Zombie)
				return kGrunts[static_cast<unsigned>(playerId) % 3];
			// An outfit with no art here (and the reserved numbers) is drawn as 0.
			return NamedFolder(outfit);
		}

		void PlayerOutfits::SetPlayerOutfit(int playerId, int outfit, int colourTeam) {
			if (outfit == kTeamOutfit)
				playerOutfits.erase(playerId);
			else
				playerOutfits[playerId] = outfit;
			if (colourTeam == 0 || colourTeam == 1)
				playerColours[playerId] = colourTeam;
			else
				playerColours.erase(playerId);
		}

		int PlayerOutfits::GetColourTeam(int playerId, int team) const {
			auto it = playerColours.find(playerId);
			return it != playerColours.end() ? it->second : team;
		}

		void PlayerOutfits::SetTeamOutfits(const std::unordered_map<int, int>& outfits) {
			teamOutfits = outfits;
		}

		void PlayerOutfits::PlayerSpawned(int playerId, int team) {
			spawnOutfits[playerId] = TeamOutfit(team);
		}

		void PlayerOutfits::PlayerLeft(int playerId) {
			playerOutfits.erase(playerId);
			playerColours.erase(playerId);
			spawnOutfits.erase(playerId);
		}

		void PlayerOutfits::MapStarted() {
			playerOutfits.clear();
			playerColours.clear();
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
					// Player, outfit and the colour worn; a server from before the
					// colour sends only the first two.
					const std::size_t size = r.GetNumRemainingBytes();
					if (size != 3 && size != 2) {
						SPLog("Dropped a Set Outfit of the wrong size");
						return;
					}
					int player = r.ReadByte();
					int outfit = r.ReadByte();
					int colour = size == 3 ? r.ReadByte() : PlayerOutfits::kOwnColour;
					outfits.SetPlayerOutfit(player, outfit, colour);
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
