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

#include <algorithm> //std::sort
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

#include <Imports/SDL.h>
#include <curl/curl.h>
#include <zlib.h>

#include "Main.h"
#include "MainScreen.h"
#include "Runner.h"
#include "SplashWindow.h"
#include <Client/Client.h>
#include <Client/DemoRecorder.h>
#include <Client/Fonts.h>
#include <Client/GameMap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/CpuID.h>
#include <Core/Debug.h>
#include <Core/DirectoryFileSystem.h>
#include <Core/FileManager.h>
#include <Core/ServerAddress.h>
#include <Core/SamplingProfiler.h>
#include <Core/Settings.h>
#include <Core/StdStream.h>
#include <Core/Strings.h>
#include <Core/Thread.h>
#include <Core/ZipFileSystem.h>
#include <Gui/ConsoleScreen.h>
#include <Gui/ModOverlay.h>
#include <Gui/SelfUpdater.h>
#include <Gui/StartupScreen.h>
#include <Gui/UI/Settings/QualityPresets.h>
#include <ZeroSpades.h>

#include <Core/VoxelModel.h>

#include <ScriptBindings/ScriptManager.h>

#include <Core/Bitmap.h>
#include <Core/MemoryStream.h>

#if _MSC_VER >= 1900 // Visual Studio 2015 or higher
extern "C" {
FILE __iob_func[3] = {*stdin, *stdout, *stderr};
}
#endif

DEFINE_SPADES_SETTING(cl_showStartupWindow, "1");

// Absolute path of the running executable, refreshed every launch. Persisted
// so external tooling (e.g. the mod workbench) can locate the binary without
// guessing the platform's bundle layout.
DEFINE_SPADES_SETTING(core_executablePath, "");
// "auto" until a standalone headset's first start has written the headset
// Performance level into the config (Main.cpp), "applied" after.
DEFINE_SPADES_SETTING(vr_profile, "auto");
// A file: the whole run sampled into it, a report written there at exit
// (Core/SamplingProfiler.h). 64-bit Windows only.
DEFINE_SPADES_SETTING(core_profile, "");


#ifdef WIN32
// windows.h must be included before DbgHelp.h and shlobj.h.
#include <windows.h>

#include <DbgHelp.h>
#include <shlobj.h>

#define strncasecmp(x, y, z) _strnicmp(x, y, z)
#define strcasecmp(x, y) _stricmp(x, y)

DEFINE_SPADES_SETTING(core_win32BeginPeriod, "1");

namespace {
	class ThreadQuantumSetter {
	public:
		ThreadQuantumSetter() {
			if (core_win32BeginPeriod) {
				timeBeginPeriod(1);
				SPLog("Thread quantum was modified to 1ms by timeBeginPeriod");
				SPLog("(to disable this behavior, set core_win32BeginPeriod to 0)");
			} else {
				SPLog("Thread quantum is not modified");
				SPLog("(to enable this behavior, set core_win32BeginPeriod to 1)");
			}
		}
		~ThreadQuantumSetter() {
			if (core_win32BeginPeriod) {
				timeEndPeriod(1);
				SPLog("Thread quantum was restored");
			}
		}
	};

	// lm: without doing it this way, we will get a low-res icon or an ugly resampled icon in our
	// window.
	// we cannot use the fltk function on the console window, because it's not an Fl_Window...
	void setIcon(HWND hWnd) {
		HINSTANCE hInstance = GetModuleHandle(NULL);
		HICON hIcon = (HICON)LoadImageA(hInstance, "AppIcon", IMAGE_ICON,
			GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
		if (hIcon)
			SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

		hIcon = (HICON)LoadImageA(hInstance, "AppIcon", IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
		if (hIcon)
			SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
	}

	LONG WINAPI UnhandledExceptionProc(LPEXCEPTION_POINTERS lpEx) {
		typedef BOOL(WINAPI * PDUMPFN)(HANDLE hProcess, DWORD ProcessId, HANDLE hFile,
									   MINIDUMP_TYPE DumpType,
									   PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
									   PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
									   PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
		HMODULE hLib = LoadLibrary("DbgHelp.dll");
		PDUMPFN pMiniDumpWriteDump = (PDUMPFN)GetProcAddress(hLib, "MiniDumpWriteDump");

		static char buf[MAX_PATH + 120] = {0}; // this is our display buffer.
		if (pMiniDumpWriteDump) {
			static char fullBuf[MAX_PATH + 120] = {0};
			if (SUCCEEDED(
				  SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0,
								  buf))) { // max length = MAX_PATH (temp abuse this buffer space)
				strcat_s(buf, "\\");	   // ensure we end with a slash.
			} else {
				buf[0] = 0; // empty it, the file will now end up in the working directory :(
			}
			sprintf(fullBuf, "%sZeroSpadesCrash%d.dmp", buf, GetTickCount()); // some sort of randomization.
			HANDLE hFile = CreateFile(fullBuf, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
									  FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile != INVALID_HANDLE_VALUE) {
				MINIDUMP_EXCEPTION_INFORMATION mdei = {0};
				mdei.ThreadId = GetCurrentThreadId();
				mdei.ExceptionPointers = lpEx;
				mdei.ClientPointers = TRUE;
				MINIDUMP_TYPE mdt = MiniDumpNormal;
				BOOL rv = pMiniDumpWriteDump(GetCurrentProcess(),
					GetCurrentProcessId(), hFile, mdt, (lpEx != 0) ? &mdei : 0, 0, 0);
				CloseHandle(hFile);
				sprintf_s(buf,
						  "Something went horribly wrong, please send the file \n%s\nfor analysis.",
						  fullBuf);
			} else {
				sprintf_s(buf,
						  "Something went horribly wrong,\ni even failed to store information "
						  "about the problem... (0x%08x)",
						  lpEx ? lpEx->ExceptionRecord->ExceptionCode : 0xffffffff);
			}
		} else {
			sprintf_s(buf,
					  "Something went horribly wrong,\ni even failed to retrieve information "
					  "about the problem... (0x%08x)",
					  lpEx ? lpEx->ExceptionRecord->ExceptionCode : 0xffffffff);
		}
		MessageBoxA(NULL, buf, "Oops, we crashed...", MB_OK | MB_ICONERROR);
		ExitProcess(-1);
		// return EXCEPTION_EXECUTE_HANDLER;
	}
} // namespace
#else
namespace {
	class ThreadQuantumSetter {};
} // namespace
#endif

namespace spades {
	std::string g_pendingMapName;
	std::string g_pendingServerName;
	std::string g_pendingGameMode;
}

namespace {
	bool g_autoconnect = false;
	std::string g_autoconnectHostName;
	spades::ProtocolVersion g_autoconnectProtocolVersion = spades::ProtocolVersion::v075;

	bool g_replayDemo = false;
	std::string g_replayDemoPath;

	// Demo selection for --replay-demo, set with --demo and --player.
	std::string g_demoPath;             // empty = latest demo in Demos/
	std::string g_demoPlayer;           // empty = first player; else id or name

	// Menuless demo replay (--replay-demo). Plays a demo and auto-follows a player,
	// skipping the startup/setup/main screens entirely.
	bool g_replayDemoMenuless = false;

	bool g_printVersion = false;
	bool g_printHelp = false;

	std::string g_tryModPath;

	// Config variable overrides requested on the command line via --override-cvar
	// NAME=VALUE. Applied in memory after the preferences are loaded and reverted
	// to their previous values before the config is written back, so the on-disk
	// SPConfig.cfg is never modified by an override.
	std::vector<std::pair<std::string, std::string>> g_cvarOverrides;

	// Pre-override values of the variables in g_cvarOverrides, captured at apply
	// time and written back before the config is flushed.
	std::vector<std::pair<std::string, std::string>> g_cvarOverrideOriginals;

	void printHelp(char* binaryName) {
		printf("usage: %s [server_address] [v=protocol_version] [--replay demo.dem] [-h|--help] [-v|--version]\n",
		       binaryName);
		printf("  server_address       aos:// server address to connect to\n");
		printf("  v=0.75 or v=0.76     protocol version (default: 0.75)\n");
		printf("  --replay FILE        play back a demo recording\n");
		printf("  -r FILE              play back a demo recording (short form)\n");
		printf("  --try-mod PATH       try a mod (folder or .pak) on top of the base\n");
		printf("                       game; bypasses the startup setup and the\n");
		printf("                       enabled-mod set, and hides the Mods tab, so\n");
		printf("                       only this mod applies. A bare name resolves\n");
		printf("                       under the user Mods/ folder.\n");
		printf("  --override-cvar NAME=VALUE\n");
		printf("                       set a config variable for this run only; the\n");
		printf("                       on-disk config is left unchanged. May be given\n");
		printf("                       multiple times.\n");
		printf("  --replay-demo        play a demo and follow a player, skipping all\n");
		printf("                       menus. Use --demo and --player to customise it.\n");
		printf("  --demo FILE          demo to use (default: latest in Demos/); a bare\n");
		printf("                       name resolves under Demos/\n");
		printf("  --player ID|NAME     player to follow (default: first player)\n");
		printf("  --vr                 play in a VR headset through OpenXR (Vulkan\n");
		printf("                       renderer); see VR.md\n");
		printf("  -h, --help           show this help message\n");
		printf("  -v, --version        show version information\n");
		printf("\nAuto-recording can be enabled with the cg_demoAutoRecord setting.\n");
		printf("Recordings are kept in the Demos/ folder; only the last 10 are retained.\n");
	}

	std::regex const hostNameRegex{"aos://.*"};
	std::regex const v075Regex{"(?:v=)?0?\\.?75"};
	std::regex const v076Regex{"(?:v=)?0?\\.?76"};

	int handleCommandLineArgument(int argc, char** argv, int& i) {
		if (char* a = argv[i]) {
			if (std::regex_match(a, hostNameRegex)) {
				g_autoconnect = true;
				g_autoconnectHostName = a;
				return ++i;
			}
			if (std::regex_match(a, v075Regex)) {
				g_autoconnectProtocolVersion = spades::ProtocolVersion::v075;
				return ++i;
			}
			if (std::regex_match(a, v076Regex)) {
				g_autoconnectProtocolVersion = spades::ProtocolVersion::v076;
				return ++i;
			}
			if (!strcasecmp(a, "--version") || !strcasecmp(a, "-v")) {
				g_printVersion = true;
				return ++i;
			}
			if (!strcasecmp(a, "--help") || !strcasecmp(a, "-h")) {
				g_printHelp = true;
				return ++i;
			}
			if (!strcasecmp(a, "--replay") || !strcasecmp(a, "-r")) {
				if (i + 1 < argc) {
					g_replayDemo = true;
					g_replayDemoPath = argv[++i];
					return ++i;
				}
				return 0;
			}
			if (!strcasecmp(a, "--open-mods")) {
				spades::g_openModsTab = true;
				return ++i;
			}
			if (!strcasecmp(a, "--try-mod")) {
				if (i + 1 < argc) {
					spades::g_tryMod = true;
					g_tryModPath = argv[++i];
					return ++i;
				}
				return 0;
			}
			if (!strcasecmp(a, "--override-cvar")) {
				if (i + 1 < argc) {
					std::string spec = argv[++i];
					auto eq = spec.find('=');
					if (eq != std::string::npos) {
						std::string name = spec.substr(0, eq);
						std::string value = spec.substr(eq + 1);
						if (!name.empty())
							g_cvarOverrides.emplace_back(name, value);
						else
							SPLog("Ignoring --override-cvar with empty name: %s", spec.c_str());
					} else {
						SPLog("Ignoring malformed --override-cvar (expected NAME=VALUE): %s",
						      spec.c_str());
					}
					return ++i;
				}
				return 0;
			}
			if (!strcasecmp(a, "--replay-demo")) {
				g_replayDemoMenuless = true;
				return ++i;
			}
			if (!strcasecmp(a, "--vr")) {
				// VR renders through the Vulkan renderer only, so it is forced too.
				// Both go through the override list, so the saved config is untouched.
				g_cvarOverrides.emplace_back("vr_enable", "1");
				g_cvarOverrides.emplace_back("r_renderer", "vulkan");
				return ++i;
			}
			if (!strcasecmp(a, "--demo")) {
				if (i + 1 < argc) {
					g_demoPath = argv[++i];
					return ++i;
				}
				return 0;
			}
			if (!strcasecmp(a, "--player")) {
				if (i + 1 < argc) {
					g_demoPlayer = argv[++i];
					return ++i;
				}
				return 0;
			}
		}

		return 0;
	}

	bool pathExists(const std::string& path) {
		struct stat st;
		return ::stat(path.c_str(), &st) == 0;
	}

	bool isDirectory(const std::string& path) {
		struct stat st;
		if (::stat(path.c_str(), &st) != 0)
			return false;
		return (st.st_mode & S_IFDIR) != 0;
	}

	// Resolve a --try-mod argument to a path on disk. Accepts a folder or a
	// .pak/.zip file given directly (absolute or relative to the working dir),
	// or a bare name that lives under the user Mods/ folder. Returns "" if
	// nothing matches.
	std::string resolveTryMod(const std::string& arg) {
		if (pathExists(arg))
			return arg;

		const std::string& root = spades::g_userResourceDirectory;
		std::string candidates[] = {
		  root + "/Mods/" + arg,
		  root + "/Mods/" + arg + ".pak",
		};
		for (const std::string& c : candidates) {
			if (pathExists(c))
				return c;
		}
		return std::string();
	}

	// Resolve the demo to use for --replay-demo. An explicit --demo value is used
	// as-is (a bare name resolves under Demos/); otherwise the most recent
	// recording is chosen. Returns "" if none is found.
	std::string resolveCliDemoPath(const std::string& explicitPath) {
		if (!explicitPath.empty()) {
			if (explicitPath.find('/') == std::string::npos &&
			    explicitPath.find('\\') == std::string::npos)
				return "Demos/" + explicitPath;
			return explicitPath;
		}

		auto demos = spades::client::DemoRecorder::ListRecordings();
		if (demos.empty())
			return std::string();
		return demos.back();
	}
} // namespace

namespace spades {
	std::string g_userResourceDirectory;
	std::string g_executablePath;
	bool g_openModsTab = false;
	bool g_tryMod = false;

	void StartClient(const spades::ServerAddress& addr) {
		class ConcreteRunner : public spades::gui::Runner {
			spades::ServerAddress addr;

		protected:
			spades::gui::View* CreateView(spades::client::IRenderer* renderer,
										  spades::client::IAudioDevice* audio) override {
				auto fontManager = Handle<client::FontManager>::New(renderer);
				auto innerView = Handle<client::Client>::New(renderer, audio, addr, fontManager);
				return new spades::gui::ConsoleScreen(renderer, audio, fontManager,
													  std::move(innerView).Cast<gui::View>());
			}

		public:
			ConcreteRunner(const spades::ServerAddress& addr) : addr(addr) {}
		};
		ConcreteRunner runner(addr);
		runner.RunProtected();
	}

	void StartDemoReplay(const std::string& demoPath) {
		class DemoRunner : public spades::gui::Runner {
			std::string demoPath;

		protected:
			spades::gui::View* CreateView(spades::client::IRenderer* renderer,
			                              spades::client::IAudioDevice* audio) override {
				auto fontManager = Handle<client::FontManager>::New(renderer);
				auto innerView = Handle<client::Client>::New(
				    renderer, audio, ServerAddress(), fontManager, demoPath);
				return new spades::gui::ConsoleScreen(renderer, audio, fontManager,
				                                      std::move(innerView).Cast<gui::View>());
			}

		public:
			DemoRunner(const std::string& path) : demoPath(path) {}
		};
		DemoRunner runner(demoPath);
		runner.RunProtected();
	}

	void StartDemoReplayAutoFollow(const std::string& demoPath, const std::string& playerSpec) {
		class DemoRunner : public spades::gui::Runner {
			std::string demoPath;
			std::string playerSpec;

		protected:
			spades::gui::View* CreateView(spades::client::IRenderer* renderer,
			                              spades::client::IAudioDevice* audio) override {
				auto fontManager = Handle<client::FontManager>::New(renderer);
				auto innerView = Handle<client::Client>::New(
				    renderer, audio, ServerAddress(), fontManager, demoPath);
				innerView->EnableDemoReplayFollow(playerSpec);
				return new spades::gui::ConsoleScreen(renderer, audio, fontManager,
				                                      std::move(innerView).Cast<gui::View>());
			}

		public:
			DemoRunner(const std::string& demoPath, const std::string& playerSpec)
			    : demoPath(demoPath), playerSpec(playerSpec) {}
		};
		DemoRunner runner(demoPath, playerSpec);
		runner.RunProtected();
	}

	void StartMainScreen() {
		class ConcreteRunner : public spades::gui::Runner {
		protected:
			spades::gui::View* CreateView(spades::client::IRenderer* renderer,
										  spades::client::IAudioDevice* audio) override {
				auto fontManager = Handle<client::FontManager>::New(renderer);
				auto innerView = Handle<gui::MainScreen>::New(renderer, audio, fontManager);
				return new spades::gui::ConsoleScreen(renderer, audio, fontManager,
													  std::move(innerView).Cast<gui::View>());
			}

		public:
		};
		ConcreteRunner runner;
		runner.RunProtected();
	}
} // namespace spades

#ifndef _WIN32
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#else
#include <process.h>
#endif

namespace spades {
	namespace {
		/** Starts a new instance of this binary, with `extraArg` if not null. */
		void SpawnSelf(const char* extraArg) {
#ifdef _WIN32
			std::wstring exe;
			{
				auto* ws = (wchar_t*)SDL_iconv_string(
				  "UCS-2-INTERNAL", "UTF-8", g_executablePath.c_str(),
				  g_executablePath.size() + 1);
				if (ws) {
					exe.assign(ws);
					SDL_free(ws);
				}
			}
			std::wstring cmd = L"\"" + exe + L"\"";
			if (extraArg) {
				cmd += L" ";
				for (const char* c = extraArg; *c; ++c)
					cmd += static_cast<wchar_t>(*c); // an ASCII option
			}
			STARTUPINFOW si{};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi{};
			if (CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE,
			                   0, nullptr, nullptr, &si, &pi)) {
				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
			}
#elif defined(__APPLE__)
			// Prefer launching the .app bundle via `open -n` so the new process
			// becomes a proper foreground application; fall back to executing
			// the binary directly if we can't find the bundle.
			std::string base = g_executablePath;
			std::string appPath;
			auto pos = base.find(".app/");
			if (pos != std::string::npos)
				appPath = base.substr(0, pos + 4);

			std::vector<const char*> args;
			if (!appPath.empty()) {
				args.push_back("open");
				args.push_back("-n");
				args.push_back(appPath.c_str());
				if (extraArg) {
					args.push_back("--args");
					args.push_back(extraArg);
				}
				args.push_back(nullptr);
				pid_t pid = 0;
				posix_spawnp(&pid, args[0], nullptr, nullptr,
				             const_cast<char**>(args.data()), environ);
			} else {
				args.push_back(g_executablePath.c_str());
				if (extraArg)
					args.push_back(extraArg);
				args.push_back(nullptr);
				pid_t pid = 0;
				posix_spawnp(&pid, args[0], nullptr, nullptr,
				             const_cast<char**>(args.data()), environ);
			}
#else
			const char* argv[] = {g_executablePath.c_str(), extraArg, nullptr};
			pid_t pid = 0;
			posix_spawnp(&pid, argv[0], nullptr, nullptr,
			             const_cast<char**>(argv), environ);
#endif
		}

		/**
		 * Releases every file handle this process holds before a replacement
		 * starts. On Windows a file open for writing without shared access
		 * can't be reopened by another process, so if the old process still held
		 * SystemMessages.log the fresh instance would fail to start logging. By
		 * closing first, the new process always finds the files free — no race,
		 * no startup delay. The game's paks are closed too, so an update can
		 * replace them.
		 */
		void CloseForRelaunch() {
			Settings::GetInstance()->Flush();
			StopLog();
			FileManager::Close();
		}
	} // namespace

	void RelaunchForMods() {
		// Spawn a new instance of the same binary with --open-mods, then exit.
		CloseForRelaunch();
		SpawnSelf("--open-mods");

		// Skip atexit handlers — SDL/GL teardown can stall here, and the
		// fresh process is already on its way.
		_exit(0);
	}

	void RelaunchAfterUpdate(const std::function<void()>& install) {
		CloseForRelaunch();
		install();
		SpawnSelf(nullptr);
		_exit(0);
	}
} // namespace spades

static uLong computeCrc32ForStream(spades::IStream* s) {
	uLong crc = crc32(0L, Z_NULL, 0);

	char buf[16384];
	size_t sz;

	while ((sz = s->Read(buf, 16384)) != 0) {
		crc = crc32(crc, reinterpret_cast<const Bytef*>(buf), static_cast<uInt>(sz));
	}

	return crc;
}

#ifdef WIN32
static std::string Utf8FromWString(const wchar_t* ws) {
	auto* s = (char*)SDL_iconv_string("UTF-8", "UCS-2-INTERNAL", (char*)(ws), wcslen(ws) * 2 + 2);
	if (!s)
		return "";
	std::string ss(s);
	SDL_free(s);
	return ss;
}
#endif

int main(int argc, char** argv) {
#ifdef WIN32
	SetUnhandledExceptionFilter(UnhandledExceptionProc);
#endif

	if (argc > 0 && argv[0])
		spades::g_executablePath = argv[0];

	for (int i = 1; i < argc;) {
		int ret = handleCommandLineArgument(argc, argv, i);
		if (!ret) {
			// ignore unknown arg
			i++;
		}
	}
#if defined(__ANDROID__)
	// A standalone headset is always one: VR on, through Vulkan, no window to
	// mirror into and no startup window to click through. Overrides, so a
	// config carried over from a PC is not rewritten.
	g_cvarOverrides.emplace_back("vr_enable", "1");
	g_cvarOverrides.emplace_back("r_renderer", "vulkan");
	g_cvarOverrides.emplace_back("vr_mirror", "0");
	g_cvarOverrides.emplace_back("cl_showStartupWindow", "0");
	// SDL stops the main loop while the activity is paused (the headset's
	// menu, the home screen), and OpenXR needs that loop: the runtime moves
	// the session to STOPPING and waits for xrEndSession, and a session never
	// ended gets the app killed, which is what leaving the game on the Quest
	// did. The loop runs on; OpenXR's own session states say when to draw.
	SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
	SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0");
#endif

	if (g_printVersion) {
		printf("%s\n", PACKAGE_STRING);
		return 0;
	}

	if (g_printHelp) {
		printHelp(argv[0]);
		return 0;
	}

	std::unique_ptr<spades::SplashWindow> splashWindow;

	try {
		// start recording backtrace
		spades::reflection::Backtrace::StartBacktrace();
		SPADES_MARK_FUNCTION();

		// show splash window
		// NOTE: splash window uses image loader, which assumes backtrace is already initialized.
		splashWindow.reset(new spades::SplashWindow());
		auto showSplashWindowTime = SDL_GetTicks();
		auto pumpEvents = [&splashWindow] { splashWindow->PumpEvents(); };

		// initialize threads
		spades::Thread::InitThreadSystem();
		spades::DispatchQueue::GetThreadQueue()->MarkSDLVideoThread();

		// Before any worker thread makes a request: the server list, the mods
		// index and the update check can start at the same time, and libcurl's
		// implicit global init is not thread-safe on older versions.
		curl_global_init(CURL_GLOBAL_DEFAULT);

		SPLog("Package: " PACKAGE_STRING);

		{
			SDL_version linked;
			SDL_GetVersion(&linked);
			SPLog("SDL Version: %d.%d.%d %s", linked.major,
				linked.minor, linked.patch, SDL_GetRevision());
		}

// setup user-specific default resource directories
#ifdef WIN32
		static wchar_t buf[4096];
		GetModuleFileNameW(NULL, buf, 4096);
		std::wstring appdir = buf;
		appdir = appdir.substr(0, appdir.find_last_of(L'\\') + 1);

		// Switch to "portable" mode if "UserResources" exists
		std::wstring userAppDir = appdir + L"UserResources";

		DWORD userAppDirAttrib = GetFileAttributesW(userAppDir.c_str());
		if (userAppDirAttrib != INVALID_FILE_ATTRIBUTES &&
			(userAppDirAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
			SPLog("UserResources found - switching to 'portable' mode");

			spades::g_userResourceDirectory = Utf8FromWString(userAppDir.c_str());

			spades::FileManager::AddFileSystem(
			  new spades::DirectoryFileSystem(spades::g_userResourceDirectory, true));
		} else {
			if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, buf))) {
				std::wstring datadir = buf;
				datadir += L"\\OpenSpades\\Resources";

				spades::g_userResourceDirectory = Utf8FromWString(datadir.c_str());

				spades::FileManager::AddFileSystem(
				  new spades::DirectoryFileSystem(spades::g_userResourceDirectory, true));
			} else {
				SPLog("SHGetFolderPathW failed.");
			}
		}

		spades::FileManager::AddFileSystem(
		  new spades::DirectoryFileSystem(Utf8FromWString((appdir + L"Resources").c_str()), false));

		// fltk has a console window on windows (can disable while building, maybe use a builtin
		// console for a later release?)
		HWND hCon = GetConsoleWindow();
		if (NULL != hCon)
			setIcon(hCon);

#elif defined(__APPLE__)
		std::string home = getenv("HOME");
		spades::FileManager::AddFileSystem(new spades::DirectoryFileSystem("./Resources", false));

		// OS X application is made of Bundle, which contains its own Resources directory.
		{
			char* baseDir = SDL_GetBasePath();
			if (baseDir) {
				spades::FileManager::AddFileSystem(new spades::DirectoryFileSystem(baseDir, false));
				SDL_free(baseDir);
			}
		}

		spades::g_userResourceDirectory =
		  home + "/Library/Application Support/OpenSpades/Resources";

		spades::FileManager::AddFileSystem(
		  new spades::DirectoryFileSystem(spades::g_userResourceDirectory, true));
#elif defined(__ANDROID__)
		// A standalone headset. The game's paks travel in the APK as assets,
		// which only SDL reads (SDL_RWFromFile on a relative path), so they are
		// copied into the app's own storage, where the rest of the game reads
		// them as files like anywhere else. Resources/index.txt, written by the
		// APK build, lists them with a fingerprint each; when it differs from the
		// copy kept beside the paks, every pak is copied again.
		//
		// Not into files/Resources. SDL opens a relative path in the app's own
		// storage first and falls back to the APK only when nothing is there, so
		// paks copied to files/Resources were what "Resources/x.pak" opened from
		// then on: every new APK's paks went unread, and a fix to a shader never
		// reached the headset. Those copies are removed.
		{
			const char* base = SDL_AndroidGetInternalStoragePath();
			const std::string root = base ? base : ".";
			const std::string paks = root + "/Paks";
			mkdir(paks.c_str(), 0755);
			auto readAll = [](SDL_RWops* io) {
				std::string text;
				if (!io)
					return text;
				char buf[4096];
				size_t n;
				while ((n = SDL_RWread(io, buf, 1, sizeof(buf))) > 0)
					text.append(buf, n);
				SDL_RWclose(io);
				return text;
			};
			// The names, from each line's first word.
			auto names = [](const std::string& index) {
				std::vector<std::string> out;
				size_t at = 0;
				while (at < index.size()) {
					size_t end = index.find('\n', at);
					if (end == std::string::npos)
						end = index.size();
					std::string line = index.substr(at, end - at);
					at = end + 1;
					const size_t sp = line.find_first_of(" \t\r");
					if (sp != std::string::npos)
						line.resize(sp);
					if (!line.empty())
						out.push_back(line);
				}
				return out;
			};
			// The old copies first, or SDL would go on opening them.
			const std::string legacy = root + "/Resources";
			const std::string apkIndex = readAll(SDL_RWFromFile("Resources/index.txt", "rb"));
			for (const auto& name : names(apkIndex))
				remove((legacy + "/" + name).c_str());
			remove((legacy + "/index.txt").c_str());
			rmdir(legacy.c_str());
			if (apkIndex.empty())
				SPLog("[Android] No Resources/index.txt in the APK's assets");

			const std::string keptIndex = readAll(SDL_RWFromFile((paks + "/index.txt").c_str(), "rb"));
			int copied = 0;
			if (keptIndex != apkIndex) {
				for (const auto& name : names(apkIndex)) {
					SDL_RWops* in = SDL_RWFromFile(("Resources/" + name).c_str(), "rb");
					if (!in) {
						SPLog("[Android] Asset Resources/%s is missing", name.c_str());
						continue;
					}
					if (SDL_RWops* out = SDL_RWFromFile((paks + "/" + name).c_str(), "wb")) {
						char buf[1 << 16];
						size_t n;
						while ((n = SDL_RWread(in, buf, 1, sizeof(buf))) > 0)
							SDL_RWwrite(out, buf, 1, n);
						SDL_RWclose(out);
						copied++;
					}
					SDL_RWclose(in);
				}
				if (SDL_RWops* out = SDL_RWFromFile((paks + "/index.txt").c_str(), "wb")) {
					SDL_RWwrite(out, apkIndex.data(), 1, apkIndex.size());
					SDL_RWclose(out);
				}
			}
			SPLog("[Android] Paks in %s: %s", paks.c_str(),
			      copied ? (std::to_string(copied) + " copied from the APK").c_str()
			             : "up to date with the APK");
			spades::FileManager::AddFileSystem(new spades::DirectoryFileSystem(paks, false));
			// Settings, logs and maps in the app's external storage
			// (Android/data/com.zerospades.vr/files): there they can be read with
			// adb pull or SideQuest's file browser from any build, where the app's
			// own storage is closed to everything but a debuggable one. A headset
			// that fails at start shows only an endless loading screen, so the log
			// is the only account of it. The app's own storage if there is none.
			std::string user = root;
			if (const char* ext = SDL_AndroidGetExternalStoragePath())
				user = ext;
			spades::g_userResourceDirectory = user + "/UserResources";
			mkdir(spades::g_userResourceDirectory.c_str(), 0755);
			spades::FileManager::AddFileSystem(
			  new spades::DirectoryFileSystem(spades::g_userResourceDirectory, true));
		}
#else
		std::string home = getenv("HOME");

		spades::FileManager::AddFileSystem(new spades::DirectoryFileSystem("./Resources", false));

		spades::FileManager::AddFileSystem(new spades::DirectoryFileSystem(
		  CMAKE_INSTALL_PREFIX "/" ZEROSPADES_INSTALL_RESOURCES, false));

		std::string xdg_data_home = home + "/.local/share";

		if (getenv("XDG_DATA_HOME") == NULL) {
			SPLog("XDG_DATA_HOME not defined. Assuming that XDG_DATA_HOME is ~/.local/share");
		} else {
			std::string xdg_data_home = getenv("XDG_DATA_HOME");
			SPLog("XDG_DATA_HOME is %s", xdg_data_home.c_str());
		}

		struct stat info;

		if (stat((xdg_data_home + "/openspades").c_str(), &info) != 0) {
			if (stat((home + "/.openspades").c_str(), &info) != 0) {
			} else if (info.st_mode & S_IFDIR) {
				SPLog("OpenSpades directory in XDG_DATA_HOME not found, though old directory "
				      "exists. Trying to resolve compatibility problem.");

				if (rename((home + "/.openspades").c_str(),
				           (xdg_data_home + "/openspades").c_str()) != 0) {
					SPLog("Failed to move old directory to new.");
				} else {
					SPLog("Successfully moved old directory.");

					if (mkdir((home + "/.openspades").c_str(),
					          S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0) {
						SDL_RWops* io = SDL_RWFromFile(
						  (home + "/.openspades/CONTENT_MOVED_TO_NEW_DIR").c_str(), "wb");
						if (io != NULL) {
							std::string text = ("Content of this directory moved to " +
							                    xdg_data_home + "/openspades");
							io->write(io, text.c_str(), text.length(), 1);
							io->close(io);
						}
					}
				}
			}
		}

		spades::g_userResourceDirectory = xdg_data_home + "/openspades/Resources";

		spades::FileManager::AddFileSystem(
		  new spades::DirectoryFileSystem(spades::g_userResourceDirectory, true));

#endif

		// start log output to SystemMessages.log
		try {
			spades::StartLog();
		} catch (const std::exception& ex) {
			SDL_InitSubSystem(SDL_INIT_VIDEO);
			auto msg = spades::Format(
			  "Failed to start recording log because of the following error:\n{0}\n\n"
			  "ZeroSpades will continue to run, but any critical events are not logged.",
			  ex.what());
			if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "ZeroSpades Log System Failure",
			                             msg.c_str(), splashWindow->GetWindow())) {
				// showing dialog failed.
			}
		}
		SPLog("Log Started.");

		// load preferences.
		spades::Settings::GetInstance()->Load();

		// Apply --override-cvar values in memory. The previous value of each
		// overridden variable is captured here and restored just before the
		// config is flushed at shutdown, so an override never reaches the disk.
		//
		// Settings::Save() writes out every variable known to the instance, so
		// referencing a brand-new name would add a stray line to the config.
		// Only override variables that already exist (every real cvar is
		// registered by static initializers before main runs); unknown names are
		// skipped with a warning so the on-disk config is guaranteed untouched.
		// A standalone headset's first start takes the headset Performance level
		// (QualityPresets.cpp), written into the config once; after that the VR
		// tab's Headset Quality and every single setting are the player's.
#if defined(__ANDROID__)
		{
			spades::Settings::ItemHandle profile("vr_profile", nullptr);
			if (static_cast<std::string>(profile) == "auto") {
				spades::gui::ApplyQualityPreset(spades::gui::GetHeadsetQualityPresets().front());
				profile = "applied";
				// Now: a headset app is often closed without a clean exit
				spades::Settings::GetInstance()->Flush();
				SPLog("[VR] First start: the headset Performance level");
			}
		}
#endif

		if (!g_cvarOverrides.empty()) {
			auto knownNames = spades::Settings::GetInstance()->GetAllItemNames();
			std::set<std::string> known(knownNames.begin(), knownNames.end());
			for (const auto& ov : g_cvarOverrides) {
				if (known.find(ov.first) == known.end()) {
					SPLog("Ignoring --override-cvar for unknown config variable: %s",
					      ov.first.c_str());
					continue;
				}
				spades::Settings::ItemHandle item(ov.first, nullptr);
				g_cvarOverrideOriginals.emplace_back(ov.first, (std::string)item);
				item = ov.second;
				SPLog("Overriding config variable for this run: %s = %s",
				      ov.first.c_str(), ov.second.c_str());
			}
		}

		if (!static_cast<std::string>(core_profile).empty())
			spades::SamplingProfiler::Start(core_profile);

		// record the absolute executable path (overwrites any stale value from
		// a previous launch, then gets flushed back to the config at shutdown).
		// Windows: query the loaded module directly; POSIX: resolve argv[0]
		// (symlinks and relative paths included) with realpath.
		{
			std::string exePath = spades::g_executablePath;
#ifdef _WIN32
			std::vector<wchar_t> wbuf(32768);
			DWORD n = GetModuleFileNameW(NULL, wbuf.data(), (DWORD)wbuf.size());
			if (n > 0 && n < wbuf.size())
				exePath = Utf8FromWString(wbuf.data());
#else
			if (char* resolved = realpath(spades::g_executablePath.c_str(), nullptr)) {
				exePath = resolved;
				free(resolved);
			}
#endif
			core_executablePath = exePath;
		}
		// What the last self-update moved aside, or why it failed
		spades::gui::SelfUpdater::CleanUpAfterUpdate();
		pumpEvents();

		// dump CPU info (for debugging?)
		{
			spades::CpuID cpuid;
			SPLog("---- CPU Information ----");
			SPLog("Vendor ID: %s", cpuid.GetVendorId().c_str());
			SPLog("Brand ID: %s", cpuid.GetBrand().c_str());
			SPLog("Supports MMX: %s", cpuid.Supports(spades::CpuFeature::MMX) ? "YES" : "NO");
			SPLog("Supports SSE: %s", cpuid.Supports(spades::CpuFeature::SSE) ? "YES" : "NO");
			SPLog("Supports SSE2: %s", cpuid.Supports(spades::CpuFeature::SSE2) ? "YES" : "NO");
			SPLog("Supports SSE3: %s", cpuid.Supports(spades::CpuFeature::SSE3) ? "YES" : "NO");
			SPLog("Supports SSSE3: %s", cpuid.Supports(spades::CpuFeature::SSSE3) ? "YES" : "NO");
			SPLog("Supports FMA: %s", cpuid.Supports(spades::CpuFeature::FMA) ? "YES" : "NO");
			SPLog("Supports AVX: %s", cpuid.Supports(spades::CpuFeature::AVX) ? "YES" : "NO");
			SPLog("Supports AVX2: %s", cpuid.Supports(spades::CpuFeature::AVX2) ? "YES" : "NO");
			SPLog("Supports AVX512F: %s", cpuid.Supports(spades::CpuFeature::AVX512F) ? "YES" : "NO");
			SPLog("Supports AVX512CD: %s", cpuid.Supports(spades::CpuFeature::AVX512CD) ? "YES" : "NO");
			SPLog("Supports AVX512ER: %s", cpuid.Supports(spades::CpuFeature::AVX512ER) ? "YES" : "NO");
			SPLog("Supports AVX512PF: %s", cpuid.Supports(spades::CpuFeature::AVX512PF) ? "YES" : "NO");
			SPLog("Supports SMT: %s", cpuid.Supports(spades::CpuFeature::SimultaneousMT) ? "YES" : "NO");

			auto cpuInfo = cpuid.GetMiscInfo();
			if (cpuInfo != "(none)")
				SPLog("Misc: %s", cpuInfo.c_str());

			SPLog("-------------------------");
		}

// register resource directory specified by Makefile (or something)
#if defined(RESDIR_DEFINED)
		spades::FileManager::AddFileSystem(new spades::DirectoryFileSystem(RESDIR, false));
#endif

		// search current file system for .pak files
		{
			std::vector<spades::IFileSystem*> fss;
			std::vector<spades::IFileSystem*> fssImportant;

			std::vector<std::string> files = spades::FileManager::EnumFiles("");

			struct Comparator {
				static int GetPakId(const std::string& str) {
					if (str.size() >= 4 && str[0] == 'p' && str[1] == 'a' && str[2] == 'k' &&
						(str[3] >= '0' && str[3] <= '9')) {
						return atoi(str.c_str() + 3);
					} else {
						return 32767;
					}
				}
				static bool Compare(const std::string& a, const std::string& b) {
					int pa = GetPakId(a);
					int pb = GetPakId(b);
					if (pa == pb) {
						return a < b;
					} else {
						return pa < pb;
					}
				}
			};

			std::sort(files.begin(), files.end(), Comparator::Compare);

			for (size_t i = 0; i < files.size(); i++) {
				std::string name = files[i];

				// check extension
				if (name.size() < 4 || (name.rfind(".pak") != name.size() - 4 &&
										name.rfind(".zip") != name.size() - 4)) {
					SPLog("Ignored loose file: %s", name.c_str());
					continue;
				}

				if (spades::FileManager::FileExists(name.c_str())) {
					auto stream = spades::FileManager::OpenForReading(name.c_str());
					uLong crc = computeCrc32ForStream(stream.get());

					stream->SetPosition(0);

					spades::ZipFileSystem* fs = new spades::ZipFileSystem(stream.release(), true);
					if (name[0] == '_' && false) { // last resort for #198
						SPLog("Pak registered: %s: %08lx (marked as 'important')", name.c_str(),
							  static_cast<unsigned long>(crc));
						fssImportant.push_back(fs);
					} else {
						SPLog("Pak registered: %s: %08lx", name.c_str(),
							  static_cast<unsigned long>(crc));
						fss.push_back(fs);
					}
				}
			}
			for (size_t i = fss.size(); i > 0; i--)
				spades::FileManager::AppendFileSystem(fss[i - 1]);
			for (size_t i = 0; i < fssImportant.size(); i++)
				spades::FileManager::PrependFileSystem(fssImportant[i]);
		}

		// Mount enabled mods as an overlay on top of the base paks (see ModOverlay).
		// The shipped install paks are never modified.
		spades::gui::ModOverlay::Mount();

		// Mount the --try-mod target on top of everything. The target is an
		// unpacked mod folder or a single .pak/.zip — both expose the same file
		// tree, so the engine sees no difference between the two. No packing
		// step, so editing files in the folder and relaunching is the whole loop.
		if (spades::g_tryMod) {
			std::string path = resolveTryMod(g_tryModPath);
			if (path.empty()) {
				SPLog("Mod to try not found: %s", g_tryModPath.c_str());
			} else if (isDirectory(path)) {
				spades::FileManager::PrependFileSystem(
				  new spades::DirectoryFileSystem(path, false));
				SPLog("Mod folder mounted: %s", path.c_str());
			} else if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
				spades::FileManager::PrependFileSystem(
				  new spades::ZipFileSystem(new spades::StdStream(f, true), true));
				SPLog("Mod pak mounted: %s", path.c_str());
			} else {
				SPLog("Mod to try failed to open: %s", path.c_str());
			}
		}
		pumpEvents();

		// initialize localization system
		SPLog("Initializing localization system");
		spades::LoadCurrentLocale();
		_Tr("Main", "Localization System Loaded");
		pumpEvents();

		// parse args

		// initialize AngelScript
		SPLog("Initializing script engine");
		spades::ScriptManager::GetInstance();
		pumpEvents();

		ThreadQuantumSetter quantumSetter;
		(void)quantumSetter; // suppress "unused variable" warning

		SDL_InitSubSystem(SDL_INIT_VIDEO);

		// we want to show splash window at least for some time...
		pumpEvents();
		auto ticks = SDL_GetTicks();
		if (ticks < showSplashWindowTime + 1500)
			SDL_Delay(showSplashWindowTime + 1500 - ticks);
		pumpEvents();

		// everything is now ready!
		if (g_replayDemoMenuless) {
			splashWindow.reset();

			std::string demoPath = resolveCliDemoPath(g_demoPath);
			if (demoPath.empty()) {
				SPLog("No demos found in Demos/ for --replay-demo");
			} else {
				SPLog("Starting menuless demo replay: '%s'", demoPath.c_str());
				spades::StartDemoReplayAutoFollow(demoPath, g_demoPlayer);
			}
		} else if (g_replayDemo) {
			splashWindow.reset();

			SPLog("Starting demo replay: %s", g_replayDemoPath.c_str());
			spades::StartDemoReplay(g_replayDemoPath);
		} else if (!g_autoconnect) {
			if (spades::g_openModsTab || spades::g_tryMod ||
			    !((int)cl_showStartupWindow != 0 || splashWindow->IsStartupScreenRequested())) {
				splashWindow.reset();

				SPLog("Starting main screen");
				spades::StartMainScreen();
			} else {
				splashWindow.reset();

				SPLog("Starting startup window");
				::spades::gui::StartupScreen::Run();
			}
		} else {
			splashWindow.reset();

			spades::ServerAddress host(g_autoconnectHostName, g_autoconnectProtocolVersion);
			spades::StartClient(host);
		}

		spades::SamplingProfiler::Stop();

		// Revert any --override-cvar values to what they were before this run so
		// the flush below writes the unchanged configuration back to disk.
		for (const auto& orig : g_cvarOverrideOriginals) {
			spades::Settings::ItemHandle item(orig.first, nullptr);
			item = orig.second;
		}

		spades::Settings::GetInstance()->Flush();

		spades::FileManager::Close();
	} catch (const spades::ExitRequestException&) {
		// user changed his/her mind.
	} catch (const std::exception& ex) {
		try {
			splashWindow.reset(nullptr);
		} catch (...) {
		}

		std::string msg = ex.what();
		msg = _Tr("Main",
		          "A serious error caused ZeroSpades to stop working:\n\n{0}\n\nSee "
		          "SystemMessages.log for more details.",
		          msg);

		SPLog("[!] Terminating due to the fatal error: %s", ex.what());

#if defined(__ANDROID__)
		// A standalone headset never shows a flat dialog: the game would sit behind
		// its loading screen forever (a Quest 2 did). Logged (to logcat too),
		// then out, back to the headset's home.
		exit(1);
#else
		SDL_InitSubSystem(SDL_INIT_VIDEO);
		if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
			_Tr("Main", "ZeroSpades Fatal Error").c_str(), msg.c_str(), nullptr)) {
			fprintf(stderr, "[Fatal] %s\n", ex.what());
		}
#endif
	}

#if defined(__ANDROID__)
	// Android keeps the process after main() returns, and the next launch runs
	// main() again in it, with every static of this run still set: the second
	// launch crashed in StartLog, the third (a fresh process) worked. End the
	// process instead, so every launch starts clean.
	spades::StopLog();
	_exit(0);
#endif

	return 0;
}