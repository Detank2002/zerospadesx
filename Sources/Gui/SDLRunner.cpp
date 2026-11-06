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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <cctype>
#include <cstring>
#include <memory>

#include "SDLRunner.h"

#include <Gui/Utils/Icon.h>
#ifdef USE_OPENGL
#include "SDLGLDevice.h"
#include <Draw/OpenGL/GLRenderer.h>
#endif
#ifdef USE_VULKAN
#include "SDLVulkanDevice.h"
#include <Draw/Vulkan/VulkanRenderer.h>
#endif
#include <Audio/ALDevice.h>
#include <Audio/NullDevice.h>
#include <Client/Client.h>
#include <Client/VRState.h>
#include "VRMenuPointer.h"
#include <Client/Fonts.h>
#if USE_OPENXR
#include <VR/OpenXRSystem.h>
#endif
#include <Core/ConcurrentDispatch.h>
#include <Core/Debug.h>
#include <Core/Disposable.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Math.h>
#include <Core/SamplingProfiler.h>
#include <Core/Settings.h>
#include <Draw/SW/SWPort.h>
#include <Draw/SW/SWRenderer.h>
#include <ZeroSpades.h>

SPADES_SETTING(r_videoWidth);
SPADES_SETTING(r_videoHeight);
DEFINE_SPADES_SETTING(r_fullscreen, "0");
DEFINE_SPADES_SETTING(r_vsync, "1");
DEFINE_SPADES_SETTING(r_allowSoftwareRendering, "0");
#if USE_OPENGL
DEFINE_SPADES_SETTING(r_renderer, "gl");
#else
DEFINE_SPADES_SETTING(r_renderer, "vulkan");
#endif
DEFINE_SPADES_SETTING(r_vulkan, "0");
DEFINE_SPADES_SETTING(s_audioDriver, "openal");
DEFINE_SPADES_SETTING(cl_fps, "0");
// Play in a headset through OpenXR. Needs the Vulkan renderer; `--vr` sets both.
DEFINE_SPADES_SETTING(vr_enable, "0");
// In a headset, ignore the keyboard and mouse altogether: play as a Quest does,
// so a screen that still needs a key shows up on PC first.
DEFINE_SPADES_SETTING(vr_controllersOnly, "0");
// 0: no desktop window in a headset, only the headset (as on a Quest). The
// window is kept hidden for SDL's events; nothing is drawn to it.
DEFINE_SPADES_SETTING(vr_mirror, "1");

static int lastMouseX = 0, lastMouseY = 0;

namespace spades {
	namespace gui {

		SDLRunner::SDLRunner() : m_hasSystemMenu(false) {}

		SDLRunner::~SDLRunner() {}

		client::IAudioDevice* SDLRunner::CreateAudioDevice() {
			if (EqualsIgnoringCase(s_audioDriver, "openal")) {
				return new audio::ALDevice();
			} else if (EqualsIgnoringCase(s_audioDriver, "null")) {
				return new audio::NullDevice();
			} else {
				SPLog("Unknown audio driver: %s, falling back to OpenAL", s_audioDriver.CString());
				s_audioDriver = "openal";
				return new audio::ALDevice();
			}
		}

		std::string SDLRunner::TranslateButton(Uint8 b) {
			SPADES_MARK_FUNCTION();
			switch (b) {
				case SDL_BUTTON_LEFT: return "LeftMouseButton";
				case SDL_BUTTON_RIGHT: return "RightMouseButton";
				case SDL_BUTTON_MIDDLE: return "MiddleMouseButton";
				case SDL_BUTTON_X1: return "MouseButton4";
				case SDL_BUTTON_X2: return "MouseButton5";
				default: return std::string();
			}
		}

		std::string SDLRunner::TranslateKey(const SDL_Keysym& k) {
			SPADES_MARK_FUNCTION();

			switch (k.sym) {
				case SDLK_ESCAPE: return "Escape";
				case SDLK_UP: return "Up";
				case SDLK_DOWN: return "Down";
				case SDLK_LEFT: return "Left";
				case SDLK_RIGHT: return "Right";
				case SDLK_PAGEUP: return "PageUp";
				case SDLK_PAGEDOWN: return "PageDown";
				case SDLK_SPACE: return " ";
				case SDLK_TAB: return "Tab";
				case SDLK_BACKSPACE: return "BackSpace";
				case SDLK_DELETE: return "Delete";
				case SDLK_RETURN: return "Enter";
				case SDLK_SLASH: return "/";
				case SDLK_KP_MINUS: return "-";
				case SDLK_KP_PLUS: return "+";
				case SDLK_KP_1: return "num1";
				case SDLK_KP_2: return "num2";
				case SDLK_KP_3: return "num3";
				case SDLK_KP_4: return "num4";
				case SDLK_KP_5: return "num5";
				case SDLK_KP_6: return "num6";
				case SDLK_KP_7: return "num7";
				case SDLK_KP_8: return "num8";
				case SDLK_KP_9: return "num9";
				case SDLK_KP_0: return "num0";
				default: return std::string(SDL_GetScancodeName(k.scancode));
			}
		}

		int SDLRunner::GetModState() { return SDL_GetModState(); }

		void SDLRunner::ProcessEvent(SDL_Event& event, View& view) {
			if ((bool)vr_controllersOnly && client::IsVRFrameActive()) {
				switch (event.type) {
					case SDL_MOUSEBUTTONDOWN:
					case SDL_MOUSEBUTTONUP:
					case SDL_MOUSEMOTION:
					case SDL_MOUSEWHEEL:
					case SDL_KEYDOWN:
					case SDL_KEYUP:
					case SDL_TEXTINPUT:
					case SDL_TEXTEDITING:
						// The controllers' pointer and keyboard reach the view on
						// their own (VRMenuPointer), not through SDL.
						return;
					default: break;
				}
			}
			switch (event.type) {
				case SDL_QUIT:
					view.Closing();
					// running = false;
					break;
				case SDL_MOUSEBUTTONDOWN:
					view.KeyEvent(TranslateButton(event.button.button), true);
					break;
				case SDL_MOUSEBUTTONUP:
					view.KeyEvent(TranslateButton(event.button.button), false);
					break;
				case SDL_MOUSEMOTION:
					if (m_active) {
						if (view.NeedsAbsoluteMouseCoordinate())
							view.MouseEvent(static_cast<float>(event.motion.x) * mouseScaleX,
							                static_cast<float>(event.motion.y) * mouseScaleY);
						else
							view.MouseEvent(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
					}
					break;
				case SDL_MOUSEWHEEL:
					view.WheelEvent(static_cast<float>(-event.wheel.x), static_cast<float>(-event.wheel.y));
					break;
				case SDL_KEYDOWN:
					if (!event.key.repeat) {
						const SDL_Keysym& keysym = event.key.keysym;
						// Toggle fullscreen mode
						if (keysym.mod & KMOD_ALT && keysym.sym == SDLK_RETURN) {
							SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);
							if (r_fullscreen) {
								if (!SDL_SetWindowFullscreen(window, 0))
									r_fullscreen = 0;
								else
									SPLog("Couldn't exit fullscreen mode: %s", SDL_GetError());
							} else {
								if (!SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN))
									r_fullscreen = 1;
								else
									SPLog("Couldn't enter fullscreen mode: %s", SDL_GetError());
							}
							return;
						}
						view.KeyEvent(TranslateKey(keysym), true);
					}
					break;
				case SDL_KEYUP: view.KeyEvent(TranslateKey(event.key.keysym), false); break;
				case SDL_TEXTINPUT: view.TextInputEvent(event.text.text); break;
				case SDL_TEXTEDITING:
					view.TextEditingEvent(event.edit.text, event.edit.start, event.edit.length);
					break;
				case SDL_WINDOWEVENT:
					if (event.window.type == SDL_WINDOWEVENT_FOCUS_GAINED) {
						SDL_SetRelativeMouseMode(SDL_TRUE);
						SDL_ShowCursor(SDL_DISABLE);
						m_active = true;
					} else if (event.window.type == SDL_WINDOWEVENT_FOCUS_LOST) {
						SDL_SetRelativeMouseMode(SDL_FALSE);
						SDL_ShowCursor(SDL_ENABLE);
						m_active = false;
					}
					break;
				default: break;
			}
		}

		void SDLRunner::RunClientLoop(SDL_Window* wnd, spades::client::IRenderer* renderer,
									  spades::client::IAudioDevice* audio) {
			{
				Handle<View> view(CreateView(renderer, audio), false);
				Uint32 ot = SDL_GetTicks();

				bool running = true;
				bool lastShift = false;
				bool lastCtrl = false;
				bool editing = false;
				bool lastGui = false;
				bool lastAlt = false;
				bool absoluteMouseCoord = true;

				SPLog("Starting Client Loop");

#if USE_OPENXR
				vr::OpenXRSystem* xr = vr::GetOpenXRSystem();
#endif
				VRMenuPointer vrPointer;
				vrPointer.SetAudio(audio);
				Handle<client::FontManager> vrFonts; // the keyboard's labels

				while (running) {
					SDL_Event event;
					SamplingProfiler::MarkFrame();

					DispatchQueue::GetThreadQueue()->ProcessQueue();

#if USE_OPENXR
					// xrWaitFrame paces the loop at the headset's rate, so it goes
					// ahead of the timing below; everything after it predicts for
					// the moment this frame is shown.
					if (xr) {
						xr->PollEvents();
						if (xr->WantsQuit()) {
							view->Closing();
							break;
						}
						xr->BeginFrame();
					}
#endif

					Uint32 dt = SDL_GetTicks() - ot;

					if ((float)cl_fps != 0 && !client::IsVRFrameActive()) {
						// Limit the frame rate
						Uint32 desiredDelay = static_cast<Uint32>(1000.0F / (float)cl_fps);
						desiredDelay = std::max<Uint32>(std::min<Uint32>(desiredDelay, 200), 1);
						if (dt < desiredDelay) {
							SDL_Delay(desiredDelay - dt);

							// Remeasure the time delta
							dt = SDL_GetTicks() - ot;
						}
					}

					ot += dt;

					// A headset frame must be drawn and ended whatever the tick
					// count says, or the runtime stalls on the unfinished frame.
					bool vrFrame = client::IsVRFrameActive();
					if (vrFrame && (int32_t)dt <= 0)
						dt = 1;

					if ((int32_t)dt > 0) {
						if (vrFrame) {
							// Menus take the controller as their mouse.
							vrPointer.Update(*view, client::GetVRSystem()->GetFrameState(),
							                 renderer->ScreenWidth(), renderer->ScreenHeight(),
							                 (float)dt / 1000.0F);
						}

						SamplingProfiler::MarkStage(1);
						view->RunFrame((float)dt / 1000.0F);

						// The keyboard, when the view takes typing.
						if (vrFrame) {
							if (!vrFonts)
								vrFonts = Handle<client::FontManager>::New(renderer);
							vrPointer.DrawKeyboard(*renderer, vrFonts->GetMediumFont());
						}

						// Not every screen draws a cursor of its own; in a headset the
						// pointer is the only way to see where the controller points.
						if (vrFrame && vrPointer.IsOnPanel()) {
							Vector2 at = vrPointer.GetPosition();
							renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.6F));
							renderer->DrawImage(nullptr, AABB2(at.x - 6.0F, at.y - 6.0F, 12.0F, 12.0F));
							renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
							renderer->DrawImage(nullptr, AABB2(at.x - 3.0F, at.y - 3.0F, 6.0F, 6.0F));
						}

						// Views draw; the runner is the only thing that presents. Present
						// before `RunFrameLate`, which is where post-frame work happens
						// (closing a subview, restoring renderer state) — that work must
						// never run between drawing the frame and swapping it.
						SamplingProfiler::MarkStage(2);
						renderer->FrameDone();
						renderer->Flip();

						SamplingProfiler::MarkStage(3);
						view->RunFrameLate((float)dt / 1000.0F);
					}

#if USE_OPENXR
					// Flip ends the headset frame; this only catches a frame no
					// renderer picked up, so the runtime is never left waiting.
					if (xr)
						xr->EndFrame(false, false);
#endif

					if (view->WantsToBeClosed()) {
						view->Closing();
						SPLog("Close requested by Client");
						break;
					}

					int modState = ((bool)vr_controllersOnly && client::IsVRFrameActive())
					                 ? 0
					                 : GetModState();
					if (modState & KMOD_CTRL) {
						if (!lastCtrl) {
							view->KeyEvent("Control", true);
							lastCtrl = true;
						}
					} else {
						if (lastCtrl) {
							view->KeyEvent("Control", false);
							lastCtrl = false;
						}
					}

					if (modState & KMOD_SHIFT) {
						if (!lastShift) {
							view->KeyEvent("Shift", true);
							lastShift = true;
						}
					} else {
						if (lastShift) {
							view->KeyEvent("Shift", false);
							lastShift = false;
						}
					}

					if (modState & KMOD_GUI) {
						if (!lastGui) {
							view->KeyEvent("Meta", true);
							lastGui = true;
						}
					} else {
						if (lastGui) {
							view->KeyEvent("Meta", false);
							lastGui = false;
						}
					}

					if (modState & KMOD_ALT) {
						if (!lastAlt) {
							view->KeyEvent("Alt", true);
							lastAlt = true;
						}
					} else {
						if (lastAlt) {
							view->KeyEvent("Alt", false);
							lastAlt = false;
						}
					}

					bool ed = view->AcceptsTextInput();
#if defined(__ANDROID__)
					// A standalone headset types on the VR keyboard
					// (VRMenuPointer); SDL's text input would bring up Android's
					// own keyboard over the game instead.
					ed = false;
#endif
					if (ed && !editing)
						SDL_StartTextInput();
					else if (!ed && editing)
						SDL_StopTextInput();
					editing = ed;

					if (editing) {
						AABB2 rt = view->GetTextInputRect();
						SDL_Rect srt;
						srt.x = (int)rt.GetMinX();
						srt.y = (int)rt.GetMinY();
						srt.w = (int)rt.GetWidth();
						srt.h = (int)rt.GetHeight();
						SDL_SetTextInputRect(&srt);
					}

					bool ab = view->NeedsAbsoluteMouseCoordinate();
					if (ab != absoluteMouseCoord) {
						if (ab) {
							SDL_SetRelativeMouseMode(SDL_FALSE);
							SDL_WarpMouseInWindow(wnd, lastMouseX, lastMouseY);
						} else {
							SDL_GetMouseState(&lastMouseX, &lastMouseY);

							// re-center if needed...
							if (lastMouseX == 0 && lastMouseY == 0) {
								int sw, sh;
								SDL_GetWindowSize(wnd, &sw, &sh);
								lastMouseX = sw / 2;
								lastMouseY = sh / 2;
							}

							SDL_SetRelativeMouseMode(SDL_TRUE);
						}

						absoluteMouseCoord = ab;
					}

#if USE_OPENXR
					// With a headset the 2D layer is laid out at its own size (see
					// VulkanRenderer::SetupVRUILayout), so the mouse on the window is
					// scaled onto it.
					if (vr::GetOpenXRSystem()) {
						int ww = 0, wh = 0;
						SDL_GetWindowSize(wnd, &ww, &wh);
						if (ww > 0 && wh > 0) {
							mouseScaleX = renderer->ScreenWidth() / static_cast<float>(ww);
							mouseScaleY = renderer->ScreenHeight() / static_cast<float>(wh);
						}
					}
#endif

					// Process events
					while (SDL_PollEvent(&event))
						ProcessEvent(event, *view);
				}

				SPLog("Leaving Client Loop");
			}
		}

		auto SDLRunner::GetRendererType() -> RendererType {
#ifdef USE_VULKAN
			if ((bool)r_vulkan)
				return RendererType::Vulkan;
#endif
#if USE_VULKAN && USE_OPENXR
			// VR draws with Vulkan whatever else was picked.
			if ((bool)vr_enable)
				return RendererType::Vulkan;
#endif
			if (EqualsIgnoringCase(r_renderer, "gl"))
#ifdef USE_OPENGL
				return RendererType::GL;
#elif defined(USE_VULKAN)
				return RendererType::Vulkan; // a config from a build that had it
#else
				return RendererType::SW;
#endif
			else if (EqualsIgnoringCase(r_renderer, "vulkan"))
				return RendererType::Vulkan;
			else if (EqualsIgnoringCase(r_renderer, "sw"))
				return RendererType::SW;
			else {
				SPLog("Unrecognized renderer '%s', falling back to software renderer.", r_renderer.CString());
				r_renderer = "sw";
				return RendererType::SW;
			}
		}

		class SDLSWPort : public draw::SWPort, public Disposable {
			SDL_Window* wnd;
			SDL_Surface* surface;
			bool adjusted;
			int actualW, actualH;

			Handle<Bitmap> framebuffer;

			void SetFramebufferBitmap() {
				if (adjusted) {
					framebuffer = Handle<Bitmap>::New(actualW, actualH);
				} else {
					framebuffer = Handle<Bitmap>::New(reinterpret_cast<uint32_t*>(surface->pixels),
													  surface->w, surface->h);
				}
			}

			void EnsureSurfaceIsValid() {
				if (!surface)
					SPRaise("The SDL surface associated with this SDLSWPart has already been"
							"destroyed.");
			}

		protected:
			~SDLSWPort() {
				if (surface && SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
			}

		public:
			SDLSWPort(SDL_Window* wnd) : wnd(wnd), surface(nullptr) {
				surface = SDL_GetWindowSurface(wnd);
				// FIXME: check pixel format
				if (SDL_MUSTLOCK(surface))
					SDL_LockSurface(surface);
				actualW = surface->w & ~7;
				actualH = surface->h & ~7;
				if (actualW != surface->w || actualH != surface->h) {
					SPLog("Surface size %dx%d doesn't match the software renderer's"
						  " requirements. Rounded to %dx%d using an intermediate surface.",
						  surface->w, surface->h, actualW, actualH);
					adjusted = true;
					memset(surface->pixels, 0, surface->w * surface->h * 4);
				} else {
					adjusted = false;
				}
				SetFramebufferBitmap();
			}

			void Dispose() override { surface = nullptr; }

			Bitmap& GetFramebuffer() override {
				EnsureSurfaceIsValid();

				return *framebuffer;
			}
			void Swap() override {
				EnsureSurfaceIsValid();

				if (adjusted) {
					int sy = (surface->h - actualH) >> 1;
					int sx = (surface->w - actualW) >> 1;
					uint32_t* outPixels = reinterpret_cast<uint32_t*>(surface->pixels);
					outPixels += sx + sy * (surface->pitch >> 2);

					uint32_t* inPixels = framebuffer->GetPixels();
					for (int y = 0; y < actualH; y++) {
						std::memcpy(outPixels, inPixels, actualW * 4);

						outPixels += surface->pitch >> 2;
						inPixels += actualW;
					}
				}

				if (SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
				SDL_UpdateWindowSurface(wnd);
				if (SDL_MUSTLOCK(surface)) {
					SDL_LockSurface(surface);
					if (!adjusted)
						SetFramebufferBitmap();
				}
			}
		};

		std::tuple<Handle<client::IRenderer>, Handle<Disposable>>
		SDLRunner::CreateRenderer(SDL_Window* wnd, RendererType type) {
			switch (type) {
#ifdef USE_OPENGL
				case RendererType::GL: {
					auto glDevice = Handle<SDLGLDevice>::New(wnd).Cast<draw::IGLDevice>();
					auto dummy = Handle<Disposable>::New(); // FIXME
					return std::make_tuple(
					  Handle<draw::GLRenderer>::New(std::move(glDevice)).Cast<client::IRenderer>(),
					  std::move(dummy));
				}
#endif
#ifdef USE_VULKAN
				case RendererType::Vulkan: {
#if USE_OPENXR
					const bool headless = vr::GetOpenXRSystem() != nullptr && !(bool)vr_mirror;
#else
					const bool headless = false;
#endif
					auto vulkanDevice = Handle<SDLVulkanDevice>::New(wnd, headless);
#if USE_OPENXR
					// The device was made through the runtime; the session binds the
					// headset to it. Without a session the game simply runs flat.
					if (vr::OpenXRSystem* xr = vr::GetOpenXRSystem()) {
						if (!xr->CreateSession(vulkanDevice->GetInstance(),
						                       vulkanDevice->GetPhysicalDevice(),
						                       vulkanDevice->GetDevice(),
						                       vulkanDevice->GetGraphicsQueueFamily(), 0)) {
							xr->DestroySession();
							// With no window there is no monitor to fall back to.
							if (headless)
								SPRaise("Could not start a headset session, and vr_mirror is 0 "
								        "so there is no window to play in. Set vr_mirror 1.");
							SPLog("[VR] Could not start a headset session; playing on the "
							      "monitor only");
						}
					}
#endif
					auto dummy = Handle<Disposable>::New(); // FIXME
					return std::make_tuple(
					  Handle<draw::VulkanRenderer>::New(std::move(vulkanDevice)).Cast<client::IRenderer>(),
					  std::move(dummy));
				}
#endif
				case RendererType::SW: {
					auto port = Handle<SDLSWPort>::New(wnd).Cast<draw::SWPort>();
					return std::make_tuple(
					  Handle<draw::SWRenderer>::New(port).Cast<client::IRenderer>(),
					  port.Cast<Disposable>());
				}
				default: SPRaise("Invalid renderer type");
			}
		}

		void SDLRunner::Run(int width, int height) {
			SPADES_MARK_FUNCTION();

			SDL_Init(SDL_INIT_VIDEO);
#if defined(__ANDROID__)
			SDL_StopTextInput(); // see the text input below: no Android keyboard
#endif

			try {
				std::string caption;
				{
					caption = PACKAGE_STRING;
#if !NDEBUG
					caption.append(" DEBUG build");
#endif
#ifndef GIT_COMMIT_HASH
#ifdef ZEROSPADES_COMPILER_STR
					caption.append(" " ZEROSPADES_COMPILER_STR); // add compiler to window title when git hash unavailable
#endif
#endif
				}

				auto rtype = GetRendererType();

#if USE_OPENXR
				// The runtime has to exist before the first Vulkan object: it picks
				// the GPU and adds the extensions the headset needs.
				std::unique_ptr<vr::OpenXRSystem> xrSystem;
				if (vr_enable) {
					if (rtype != RendererType::Vulkan) {
						SPLog("[VR] vr_enable needs the Vulkan renderer (r_renderer vulkan); "
						      "playing on the monitor only");
					} else {
						xrSystem.reset(vr::OpenXRSystem::TryCreate());
						if (xrSystem)
							vr::SetOpenXRSystem(xrSystem.get());
						else
							SPLog("[VR] No headset; playing on the monitor only");
					}
				}
#else
				if (vr_enable)
					SPLog("[VR] This build has no OpenXR support; playing on the monitor only");
#endif

				Uint32 sdlFlags = 0;
				switch (rtype) {
					case RendererType::GL:
						sdlFlags = SDL_WINDOW_OPENGL;
						SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
						SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
						SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
						if (!r_allowSoftwareRendering)
							SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
						break;
					case RendererType::Vulkan:
						sdlFlags = SDL_WINDOW_VULKAN;
						break;
					case RendererType::SW: sdlFlags = 0; break;
				}

#if USE_OPENXR
				// The window still carries SDL's events; it is just never shown.
				if (vr::GetOpenXRSystem() && !(bool)vr_mirror && rtype == RendererType::Vulkan)
					sdlFlags |= SDL_WINDOW_HIDDEN;
#endif

				if (!m_hasSystemMenu) {
					if (r_fullscreen)
						sdlFlags |= SDL_WINDOW_FULLSCREEN;
#ifdef __MACOSX__
					if (!r_fullscreen)
						sdlFlags |= SDL_WINDOW_BORDERLESS;
#endif
				}

				SDL_Window* window = SDL_CreateWindow(caption.c_str(),
					SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
					width, height, sdlFlags);

				if (!window) {
					std::string msg = SDL_GetError();
					SPRaise("Failed to create graphics window: %s", msg.c_str());
				}

#ifdef __APPLE__
#elif __unix
				SDL_Surface* icon = nullptr;
				SDL_RWops* icon_rw = nullptr;
				icon_rw = SDL_RWFromConstMem(g_appIconData, GetAppIconDataSize());
				if (icon_rw != nullptr) {
					icon = IMG_LoadPNG_RW(icon_rw);
					SDL_FreeRW(icon_rw);
				}
				if (icon == nullptr) {
					std::string msg = SDL_GetError();
					SPLog("Failed to load icon: %s", msg.c_str());
				} else {
					SDL_SetWindowIcon(window, icon);
					SDL_FreeSurface(icon);
				}
#endif

				SDL_SetRelativeMouseMode(SDL_FALSE);
				SDL_ShowCursor(SDL_DISABLE);
				m_active = true;

				{
					Handle<client::IRenderer> renderer;
					Handle<Disposable> windowReference;
					std::tie(renderer, windowReference) = CreateRenderer(window, rtype);

					Handle<client::IAudioDevice> audio(CreateAudioDevice(), false);

					if (rtype == RendererType::GL) {
						// Vsync prefers its adaptive kind, which shows a frame that misses
						// the display's refresh at once, tearing, instead of holding it
						// to the next one and halving the frame rate. Where the platform
						// has none (SDL's macOS backend), plain vsync.
						const int vsync = r_vsync;
						const bool adaptive = vsync == 1 && SDL_GL_SetSwapInterval(-1) == 0;
						if (!adaptive && SDL_GL_SetSwapInterval(vsync) != 0)
							SPLog("Couldn't set the swap interval to %d: %s", vsync,
							      SDL_GetError());
						else
							SPLog("Swap interval: %s", adaptive ? "adaptive vsync"
							                           : vsync ? "vsync"
							                                   : "immediate");
					}

					RunClientLoop(window, renderer.GetPointerOrNull(), audio.GetPointerOrNull());

#if USE_OPENXR
					// The session holds the Vulkan device; it has to go first.
					if (xrSystem)
						xrSystem->DestroySession();
#endif

					// `SDL_Window` and its associated resources will be inaccessible
					// past this point. Some referencing objects might be still alive due to
					// the indeterministic nature of AngelScript's tracing GC, so we explicitly
					// break such references right now.
					windowReference->Dispose();
				}
			} catch (...) {
#if USE_OPENXR
				vr::SetOpenXRSystem(nullptr);
#endif
				SDL_Quit();
				throw;
			}

#if USE_OPENXR
			vr::SetOpenXRSystem(nullptr);
#endif
			SDL_Quit();
		}
	} // namespace gui
} // namespace spades
