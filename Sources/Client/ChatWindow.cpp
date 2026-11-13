/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <cctype>

#include "ChatWindow.h"
#include "Client.h"
#include "IFont.h"
#include "IRenderer.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Math.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_chatHeight, "30");
DEFINE_SPADES_SETTING(cg_killfeedHeight, "26");

DEFINE_SPADES_SETTING(cg_chatFadeTime, "12");
DEFINE_SPADES_SETTING(cg_killfeedFadeTime, "6");

SPADES_SETTING(cg_stats);
SPADES_SETTING(cg_statsSmallFont);

namespace spades {
	namespace client {

		ChatWindow::ChatWindow(Client* c, IFont* fnt, bool killfeed)
		    : client(c), renderer(c->GetRenderer()), font(fnt), baseFont(fnt), killfeed(killfeed) {
			firstY = 0.0F;
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/a-Rifle.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/b-SMG.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/c-Shotgun.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/d-Headshot.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/e-Melee.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/f-Grenade.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/g-Falling.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/h-Teamchange.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/i-Classchange.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/j-Airborne.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/k-Noscope.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/l-Domination.png").GetPointerOrNull());
			killImages.push_back(renderer.RegisterImage("Gfx/Killfeed/m-Revenge.png").GetPointerOrNull());
			SPAssert(killImages.size() == kKillImageCount);
		}

		ChatWindow::~ChatWindow() {}

		float ChatWindow::GetWidth() { return renderer.ScreenWidth() * 0.5F; }

		float ChatWindow::LayoutHeight() {
			return layoutHeight > 0.0F ? layoutHeight : renderer.ScreenHeight();
		}

		float ChatWindow::GetHeight() {
			float prop = killfeed ? (float)cg_killfeedHeight : (float)cg_chatHeight;
			return LayoutHeight() * prop * 0.01F;
		}

		float ChatWindow::GetLineHeight() { return 20.0F * textScale; }

		void ChatWindow::SetTextScale(IFont* scaledFont, float scale) {
			font = scaledFont ? scaledFont : baseFont;
			textScale = scaledFont ? scale : 1.0F;
		}

		std::string ChatWindow::KillImage(int kt, int weapon) {
			std::string tmp = "xx";
			tmp[0] = MsgImage;
			switch (kt) {
				case KillTypeWeapon:
					switch (weapon) {
						case RIFLE_WEAPON:
						case SMG_WEAPON:
						case SHOTGUN_WEAPON:
							tmp[1] = 'a' + weapon;
							break;
						default: return "";
					}
					break;
				case KillTypeHeadshot:
				case KillTypeMelee:
				case KillTypeGrenade:
				case KillTypeFall:
				case KillTypeTeamChange:
				case KillTypeClassChange:
				case 7:  // airborne kill
				case 8:  // noscope kill
				case 9:  // domination kill
				case 10: // revenge kill
					tmp[1] = 'a' + 2 + kt;
					break;
				default: return "";
			}
			return tmp;
		}
		IImage* ChatWindow::GetKillImage(char index) {
			int real = index - 'a';
			if (real >= 0 && real < kKillImageCount)
				return killImages[real];
			return NULL;
		}

		void ChatWindow::AddMessage(const std::string& msg, float fadeTime) {
			SPADES_MARK_FUNCTION();

			float lh = GetLineHeight();
			float fadeOut = killfeed ? 0.5F : 1.0F;

			if (fadeTime < 0.0F)
				fadeTime = killfeed ? cg_killfeedFadeTime : cg_chatFadeTime;

			entries.push_front(ChatEntry(msg, lh, fadeTime + fadeOut));

			firstY -= lh;
		}

		std::string ChatWindow::ColoredMessage(const std::string& msg, char c) {
			SPADES_MARK_FUNCTION_DEBUG();
			std::string s;
			s += c;
			s += msg;
			s += MsgColorRestore;
			return s;
		}

		std::string ChatWindow::StripFormatting(const std::string& msg) {
			std::string s;
			s.reserve(msg.size());
			for (size_t i = 0; i < msg.size(); i++) {
				if (msg[i] >= 1 && msg[i] <= MsgColorMax) {
					// An image takes the byte after it as its kind, when that names
					// one; otherwise the byte is text, as Draw reads it.
					if (msg[i] == MsgImage && i + 1 < msg.size() && msg[i + 1] >= 'a' &&
						msg[i + 1] < 'a' + kKillImageCount)
						i++;
					continue;
				}
				s += msg[i];
			}
			return s;
		}

		std::string ChatWindow::TeamColorMessage(const std::string& msg, int team) {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (team) {
				case 0: return ColoredMessage(msg, MsgColorTeam1);
				case 1: return ColoredMessage(msg, MsgColorTeam2);
				case 2: return ColoredMessage(msg, MsgColorTeam3);
				default: return msg;
			}
		}

		Vector4 ChatWindow::GetColor(char c) {
			World* w = client ? client->GetWorld() : NULL;
			switch (c) {
				case MsgColorTeam1:
					return w ? ConvertColorRGBA(w->GetTeamColor(0)) : MakeVector4(0, 1, 0, 1);
				case MsgColorTeam2:
					return w ? ConvertColorRGBA(w->GetTeamColor(1)) : MakeVector4(0, 0, 1, 1);
				case MsgColorTeam3:
					return w ? ConvertColorRGBA(w->GetTeamColor(2)) : MakeVector4(1, 1, 0, 1);
				case MsgColorRed: return MakeVector4(1, 0.25, 0.25, 1);
				case MsgColorGreen: return MakeVector4(0.5, 1, 0.5, 1);
				case MsgColorGray: return MakeVector4(0.8F, 0.8F, 0.8F, 1);
				case MsgColorYellow: return MakeVector4(1, 1, 0.5, 1);
				default: return MakeVector4(1, 1, 1, 1);
			}
		}

		void ChatWindow::Update(float dt) {
			if (firstY < 0.0F) {
				firstY += dt * std::max(100.0F, -firstY);
				if (firstY > 0.0F)
					firstY = 0.0F;
			}

			float height = GetHeight();
			float y = firstY;

			for (auto it = entries.begin(); it != entries.end();) {
				ChatEntry& ent = *it;

				// Stacking direction doesn't matter here: the chat is the mirror
				// image of the killfeed, so the same conditions apply to both.
				if (y + ent.height > height) {
					// fade out, then remove
					ent.fade -= dt * 4.0F;
					if (ent.fade < 0.0F) {
						it = entries.erase(it);
						continue;
					}
				} else if (y + ent.height > 0.0F) {
					// fade in
					ent.fade = std::min(ent.fade + dt * 4.0F, 1.0F);
				}

				ent.timeFade -= dt;
				if (ent.timeFade < 0.0F) {
					// Collapse the expired line so the others slide into its place,
					// then remove it once it has no height left.
					ent.height = std::max(ent.height - dt * 100.0F, 0.0F);
					if (ent.height <= 0.0F) {
						it = entries.erase(it);
						continue;
					}
				}

				y += ent.height;
				++it;
			}
		}

		void ChatWindow::Draw() {
			SPADES_MARK_FUNCTION();

			float sh = renderer.ScreenHeight();
			layoutHeight = sh;

			float winH = GetHeight();
			float winX = 8.0F;
			float winY = killfeed ? 8.0F : (sh - bottomMargin) - winH;
			float lh = GetLineHeight();
			float y = killfeed ? firstY : winH - firstY;

			if (killfeed) {
				bool demoMode = client->IsDemoMode();
				const int statsMode = cg_stats;
				if ((statsMode == 2 || (statsMode >= 3 && client->IsScoreboardVisible())) && !demoMode)
					winY += cg_statsSmallFont ? 10.0F : 20.0F;
			}

			Vector4 shadowColor = { 0, 0, 0, 0.8F };
			Vector4 brightShadowColor = { 1, 1, 1, 0.8F };

			std::string ch = "aaaaaa"; // let's not make a new object for each character.
			// note: UTF-8's longest character is 6 bytes

			// Lines wrap, at a space where there is one, where they would run off
			// what they are drawn on: the screen's edge on a monitor, the wrist
			// panel's in a headset, which a long line used to run off. Each entry's
			// height follows the lines it took, so the entries above it make room.
			const float maxLineW = std::max(64.0F, renderer.ScreenWidth() - 8.0F - winX);

			std::list<ChatEntry>::iterator it;
			for (it = entries.begin(); it != entries.end(); ++it) {
				ChatEntry& ent = *it;

				// top of this entry: the chat stacks upwards, the killfeed downwards.
				float entryY = killfeed ? y : y - ent.height;

				const auto& msg = ent.msg;
				Vector4 color = GetColor(MsgColorRestore);

				float curPosX = winX;
				float tx = 0.0F, ty = entryY;
				float fade = ent.fade;
				// The width of the word starting at `from`: up to a space, a new
				// line or a colour change (an image counts as part of the word).
				auto wordWidth = [&](size_t from) {
					float w = 0.0F;
					for (size_t j = from; j < msg.size(); j++) {
						const char c = msg[j];
						if (c == ' ' || c == '\r' || c == '\n')
							break;
						if (c >= 1 && c <= MsgColorMax) {
							if (c != MsgImage)
								break;
							IImage* img = j + 1 < msg.size() ? GetKillImage(msg[j + 1]) : nullptr;
							if (img)
								w += (int)roundf(img->GetWidth() * textScale);
							j++;
							continue;
						}
						size_t ln = 0;
						GetCodePointFromUTF8String(msg, j, &ln);
						w += (int)roundf(font->Measure(msg.substr(j, ln)).x);
						j += ln - 1;
					}
					return w;
				};
				int lines = 1;
				bool atWordStart = true;
				// A new line for a word that does not fit, unless it is the line's
				// first: a word wider than the whole line breaks where it reaches
				// the edge instead.
				auto wrapFor = [&](float w) {
					if (tx > 0.0F && tx + w > maxLineW) {
						tx = 0.0F;
						ty += lh;
						lines++;
						return true;
					}
					return false;
				};

				float fadeOut = killfeed ? 0.5F : 1.0F;
				if (ent.timeFade < fadeOut)
					fade *= (ent.timeFade / fadeOut);

				if (fade < 0.01F)
					goto endDrawLine; // Skip rendering invisible messages

				brightShadowColor.w = shadowColor.w = (killfeed ? 0.4F : 0.8F) * fade;
				color.w = fade;

				// animation for killfeed
				if (killfeed) {
					float easeOutFactor = 1.0F - powf(1.0F - fade, 3.0F);
					curPosX -= floorf(300.0F * (1.0F - easeOutFactor));
				}

				for (size_t i = 0; i < msg.size(); i++) {
					if ((msg[i] == '\r' || msg[i] == '\n') && !killfeed) {
						// new line
						tx = 0.0F;
						ty += lh;
						lines++;
						atWordStart = true;
					} else if (msg[i] >= 1 && msg[i] <= MsgColorMax) {
						if (msg[i] == MsgImage) {
							IImage* img = NULL;
							if (i + 1 < msg.size() && (img = GetKillImage(msg[i + 1]))) {
								if (atWordStart)
									wrapFor(wordWidth(i));
								atWordStart = false;
								Vector4 colorP = color;
								colorP.x *= colorP.w;
								colorP.y *= colorP.w;
								colorP.z *= colorP.w;
								renderer.SetColorAlphaPremultiplied(colorP);
								const Vector2 at = MakeVector2(tx + curPosX + 1, ty + winY).Floor();
								renderer.DrawImage(img, AABB2(at.x, at.y, img->GetWidth() * textScale,
								                              img->GetHeight() * textScale));
								tx += (int)roundf(img->GetWidth() * textScale);
								++i;
							}
						} else {
							color = GetColor(msg[i]);
							color.w = fade;
						}
					} else {
						if (msg[i] == ' ') {
							atWordStart = true;
						} else if (atWordStart) {
							wrapFor(wordWidth(i));
							atWordStart = false;
						}
						size_t ln = 0;
						GetCodePointFromUTF8String(msg, i, &ln);
						ch.resize(ln);
						for (size_t k = 0; k < ln; k++)
							ch[k] = msg[i + k];
						i += ln - 1;
						// A word longer than a whole line breaks at the edge.
						wrapFor(roundf(font->Measure(ch).x));
						// A space that wrapped is not drawn at the start of the line.
						if (ch == " " && tx == 0.0F && ty > y)
							continue;

						float luminosity = color.x + color.y + color.z;
						Vector4 shadow = (luminosity > 0.9F) ? shadowColor : brightShadowColor;
						Vector2 pos = MakeVector2(tx + curPosX, ty + winY).Floor();

						if (killfeed) {
							font->DrawShadow(ch, pos + MakeVector2(1, 1), 1.0F, shadow, shadow);
							font->Draw(ch, pos, 1.0F, color);
						} else {
							font->DrawShadow(ch, pos, 1.0F, color, shadow);
						}

						tx += (int)roundf(font->Measure(ch).x);
					}
				}

				ent.height = lh * (float)lines;

			endDrawLine:
				y = killfeed ? y + ent.height : entryY;
			}
		}
	} // namespace client
} // namespace spades
