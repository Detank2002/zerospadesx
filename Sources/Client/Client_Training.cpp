/*
 Copyright (c) 2026 ZeroSpades developers.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

// Training's side of the client: the tutorial's checklist and time, and the aim
// scenarios' run as the HUD shows it, with the best scores kept on disk. The
// course's tips themselves are in Client_Draw.cpp (TrainingTip).

#include <cstdio>
#include <sstream>

#include "Client.h"
#include "World.h"

#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/TrainingHost.h>
#include <Training/TrainingCourse.h>
#include <Training/TrainingModes.h>

SPADES_SETTING(cg_keyJump);

namespace spades {
	namespace client {
		namespace {
			constexpr const char* kBestsFile = "TrainingBests.txt";

			std::string Clock(float seconds) {
				const int s = static_cast<int>(seconds + 0.999F);
				char buf[16];
				std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
				return buf;
			}

			std::string Percent(float f) {
				char buf[16];
				std::snprintf(buf, sizeof(buf), "%.0f%%", f * 100.0F);
				return buf;
			}

			std::string WeaponName(int w) {
				return w == 1 ? _Tr("Client", "SMG") : w == 2 ? _Tr("Client", "Shotgun")
				                                         : _Tr("Client", "Rifle");
			}
		} // namespace

		std::string Client::AimScenarioName(int scenario) {
			switch (static_cast<training::Scenario>(scenario)) {
				case training::Scenario::Gridshot: return _Tr("Client", "Gridshot");
				case training::Scenario::Flick: return _Tr("Client", "Flick");
				case training::Scenario::Tracking: return _Tr("Client", "Tracking");
				case training::Scenario::StrafeDuel: return _Tr("Client", "Strafe duel");
				case training::Scenario::LongRange: return _Tr("Client", "Long range");
				default: return "?";
			}
		}

		// -- best scores: "aim <scenario> <weapon> <score>" and
		//    "tutorial <seconds> <vr>" lines --

		std::map<std::string, float> Client::ReadTrainingBests() {
			std::map<std::string, float> bests;
			try {
				if (!FileManager::FileExists(kBestsFile))
					return bests;
				std::istringstream in(FileManager::ReadAllBytes(kBestsFile));
				std::string kind;
				while (in >> kind) {
					if (kind == "aim") {
						int sc, w;
						float score;
						if (in >> sc >> w >> score)
							bests["aim " + std::to_string(sc) + " " + std::to_string(w)] = score;
					} else if (kind == "tutorial") {
						float sec;
						int vr;
						if (in >> sec >> vr)
							bests["tutorial " + std::to_string(vr)] = sec;
					} else {
						std::string rest;
						std::getline(in, rest);
					}
				}
			} catch (const std::exception& ex) {
				SPLog("[!] Cannot read %s: %s", kBestsFile, ex.what());
			}
			return bests;
		}

		void Client::WriteTrainingBests(const std::map<std::string, float>& bests) {
			try {
				std::string text = "# Training's best scores, written by ZeroSpades X\n";
				for (const auto& b : bests) {
					char line[96];
					std::snprintf(line, sizeof(line), "%s %g\n", b.first.c_str(), b.second);
					text += line;
				}
				auto s = FileManager::OpenForWriting(kBestsFile);
				s->Write(text);
			} catch (const std::exception& ex) {
				SPLog("[!] Cannot write %s: %s", kBestsFile, ex.what());
			}
		}

		// -- the tutorial --

		void Client::UpdateTraining(float dt) {
			gui::TrainingHost& host = gui::TrainingHost::Get();
			if (!world || !host.IsActive()) {
				trainingSteps = 0;
				trainingVRSteps = 0;
				trainingTime = 0.0F;
				trainingStarted = trainingFinished = false;
				return;
			}
			stmp::optional<Player&> p = world->GetLocalPlayer();

			if (host.IsAim()) {
				const training::AimStatus st = host.GetAimStatus();
				if (st.run != aimRunSeen) {
					aimRunSeen = st.run;
					aimResultSaved = false;
					aimNewBest = false;
				}
				// A run over: its score against the best of this scenario and gun.
				if (st.phase == training::AimPhase::Finished && !aimResultSaved) {
					aimResultSaved = true;
					auto bests = ReadTrainingBests();
					const std::string key = "aim " + std::to_string(static_cast<int>(st.scenario)) +
					                        " " + std::to_string(st.weapon);
					auto it = bests.find(key);
					aimPreviousBest = it == bests.end() ? -1.0F : it->second;
					SPLog("Training: %s run %d over: score %d, %d kills, %d of %d shots hit",
					      AimScenarioName(static_cast<int>(st.scenario)).c_str(), st.run, st.score(),
					      st.kills, st.hits, st.shots);
					if (st.score() > 0 && st.score() > aimPreviousBest) {
						aimNewBest = true;
						bests[key] = static_cast<float>(st.score());
						WriteTrainingBests(bests);
					}
				}
				return;
			}

			// The course: its steps from where you are, done for good once done.
			if (!p || !p->IsAlive() || !IsOnTrainingCourse())
				return;
			const int kills = world->GetPlayerScore(p->GetId());
			for (int s = 0; s < training::StepCount; s++)
				if (training::step_reached(s, p->GetPosition().x, kills))
					trainingSteps |= 1 << s;
			if (!trainingStarted) {
				trainingStarted = true;
				trainingTime = 0.0F;
			}
			const bool vr = IsVRFrameActive();
			const int all = (1 << training::StepCount) - 1;
			const int allVR = (1 << training::VRStepCount) - 1;
			const bool done = trainingSteps == all && (!vr || trainingVRSteps == allVR);
			if (!trainingFinished && done) {
				trainingFinished = true;
				auto bests = ReadTrainingBests();
				const std::string key = std::string("tutorial ") + (vr ? "1" : "0");
				auto it = bests.find(key);
				trainingPreviousBest = it == bests.end() ? -1.0F : it->second;
				trainingNewBest = trainingPreviousBest < 0.0F || trainingTime < trainingPreviousBest;
				if (trainingNewBest) {
					bests[key] = trainingTime;
					WriteTrainingBests(bests);
				}
			}
			if (!trainingFinished)
				trainingTime += dt;
		}

		void Client::MarkTrainingVRStep(int step) {
			if (gui::TrainingHost::Get().IsActive() && !gui::TrainingHost::Get().IsAim())
				trainingVRSteps |= 1 << step;
		}

		std::string Client::TrainingChecklist(bool vr) {
			const int kills = [&] {
				stmp::optional<Player&> p = world ? world->GetLocalPlayer() : stmp::optional<Player&>();
				return p ? world->GetPlayerScore(p->GetId()) : 0;
			}();
			struct Item {
				bool done;
				std::string text;
			};
			std::vector<Item> items;
			items.push_back({(trainingSteps & (1 << training::StepWalk)) != 0,
			                 _Tr("Client", "Walk to the range")});
			items.push_back({(trainingSteps & (1 << training::StepShoot)) != 0,
			                 _Tr("Client", "Shoot {0} targets ({1}/{0})", training::kRangeKills,
			                     std::min(kills, training::kRangeKills))});
			items.push_back({(trainingSteps & (1 << training::StepDig)) != 0,
			                 _Tr("Client", "Dig through the wall")});
			items.push_back({(trainingSteps & (1 << training::StepClimb)) != 0,
			                 _Tr("Client", "Build up the ledge")});
			items.push_back({(trainingSteps & (1 << training::StepBlast)) != 0,
			                 _Tr("Client", "Blow the tunnel open")});
			items.push_back({(trainingSteps & (1 << training::StepFinish)) != 0,
			                 _Tr("Client", "Reach the finish")});
			if (vr) {
				items.push_back({(trainingVRSteps & (1 << training::VRStepWrist)) != 0,
				                 _Tr("Client", "Look at your wrist (health, ammo, map)")});
				items.push_back({(trainingVRSteps & (1 << training::VRStepHolster)) != 0,
				                 _Tr("Client", "Take a tool from a holster")});
				items.push_back({(trainingVRSteps & (1 << training::VRStepTwoHands)) != 0,
				                 _Tr("Client", "Hold the gun in both hands")});
				items.push_back({(trainingVRSteps & (1 << training::VRStepReload)) != 0,
				                 _Tr("Client", "Reload")});
			}
			int done = 0;
			for (const auto& it : items)
				done += it.done ? 1 : 0;
			std::string out = _Tr("Client", "Steps {0}/{1}, time {2}", done, (int)items.size(),
			                      Clock(trainingTime));
			if (trainingFinished) {
				out += "  " + (trainingNewBest ? _Tr("Client", "(new best!)")
				                               : _Tr("Client", "(best {0})", Clock(trainingPreviousBest)));
			}
			for (const auto& it : items)
				out += std::string("\n") + (it.done ? "[x] " : "[  ] ") + it.text;
			return out;
		}

		// -- the aim scenarios, for the tip box --

		bool Client::AimTip(bool vr, std::string& title, std::string& tip) {
			gui::TrainingHost& host = gui::TrainingHost::Get();
			if (!world || !host.IsAim())
				return false;
			const training::AimStatus st = host.GetAimStatus();
			const std::string name =
			  AimScenarioName(static_cast<int>(st.scenario)) + " (" + WeaponName(st.weapon) + ")";
			const std::string jump =
			  vr ? (VRToolHand() == VRHandRight ? std::string("A") : std::string("X"))
			     : static_cast<std::string>(cg_keyJump);
			const bool tracking = training::scored_on_tracking(st.scenario);
			switch (st.phase) {
				case training::AimPhase::Waiting:
					title = name;
					tip = _Tr("Client", "Getting ready...");
					break;
				case training::AimPhase::Countdown: {
					title = _Tr("Client", "{0}: starting in {1}", name,
					            static_cast<int>(st.countdown + 0.999F));
					switch (st.scenario) {
						case training::Scenario::Gridshot:
							tip = _Tr("Client", "Three targets on the stands. Shoot one and another "
							                    "takes its place. Kills and accuracy count.");
							break;
						case training::Scenario::Flick:
							tip = _Tr("Client", "One target at a time, somewhere around you; an "
							                    "orange ping shows where the next one comes. "
							                    "Flick to it and shoot.");
							break;
						case training::Scenario::Tracking:
							tip = _Tr("Client", "A target strafes and jumps in front of you. Keep "
							                    "your aim on it: the time on target is the score.");
							break;
						case training::Scenario::StrafeDuel:
							tip = _Tr("Client", "Targets that strafe and stop like players, with "
							                    "full health. Headshots end them fastest.");
							break;
						default:
							tip = _Tr("Client", "Walkers far across the arena. Lead them, and "
							                    "crouch to steady your aim.");
							break;
					}
					break;
				}
				case training::AimPhase::Running:
					title = name + "  " + Clock(st.time_left);
					tip = tracking ? _Tr("Client", "On target {0}   Hits {1}   Accuracy {2}",
					                     Percent(st.tracking()), st.hits, Percent(st.accuracy()))
					               : _Tr("Client", "Kills {0}   Accuracy {1}   Score {2}", st.kills,
					                     Percent(st.accuracy()), st.score());
					break;
				case training::AimPhase::Finished: {
					title = _Tr("Client", "{0}: done", name);
					std::string score = tracking ? Percent(st.tracking()) : std::to_string(st.score());
					tip = _Tr("Client", "Score {0}", score);
					if (aimNewBest)
						tip += "  " + _Tr("Client", "(new best!)");
					else if (aimPreviousBest >= 0.0F)
						tip += "  " + _Tr("Client", "(best {0})",
						                  tracking ? Percent(aimPreviousBest / 10000.0F)
						                           : std::to_string(static_cast<int>(aimPreviousBest)));
					tip += "\n" + _Tr("Client", "Kills {0}   Accuracy {1} ({2} of {3} shots)", st.kills,
					                  Percent(st.accuracy()), st.hits, st.shots);
					if (st.hits > 0)
						tip += "\n" + _Tr("Client", "Headshots {0} ({1} of hits)", st.headshots,
						                  Percent(static_cast<float>(st.headshots) /
						                          static_cast<float>(st.hits)));
					if (st.ttk_count > 0) {
						char ttk[16];
						std::snprintf(ttk, sizeof(ttk), "%.2f", st.average_ttk());
						tip += "\n" + _Tr("Client", "Average time to kill {0}s", std::string(ttk));
					}
					if (tracking)
						tip += "\n" + _Tr("Client", "Time on target {0}", Percent(st.tracking()));
					tip += "\n\n" + (vr ? _Tr("Client", "{0} goes again. The wrist menu's Menu "
					                                    "leaves.",
					                          jump)
					                    : _Tr("Client", "{0} goes again; Esc leaves.", jump));
					break;
				}
			}
			return true;
		}

		bool Client::AimRestartKey(bool jumpPressed) {
			if (!jumpPressed || !world)
				return false;
			gui::TrainingHost& host = gui::TrainingHost::Get();
			if (!host.IsAim())
				return false;
			if (host.GetAimStatus().phase != training::AimPhase::Finished)
				return false;
			host.RestartAim();
			return true;
		}
	} // namespace client
} // namespace spades
