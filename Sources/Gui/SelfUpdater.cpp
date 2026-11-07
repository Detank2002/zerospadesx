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

#include "SelfUpdater.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cstring>
#include <memory>
#include <set>
#include <thread>

#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include <curl/curl.h>
#include <unzip/unzip.h>

#include "CurlTls.h"
#include "Main.h"
#include "UpdatePackage.h"
#include <Core/Debug.h>
#include <Core/Sha256.h>
#include <Core/Strings.h>
#include <Core/Thread.h>
#include <ZeroSpades.h>

namespace spades {
	namespace gui {
		namespace {
			// A release's files are only ever downloaded from here.
			constexpr const char* kDownloadUrl =
			  "https://github.com/Detank2002/zerospadesx/releases/download/";

			// In the game's folder: the download and the unpacked package...
			constexpr const char* kStaging = ".zsupdate";
			// ...the files an install put there, so the next one removes what a
			// newer package no longer has...
			constexpr const char* kManifest = "ZeroSpadesX-files.txt";
			// ...why the last install failed, for the next start to report...
			constexpr const char* kFailureNote = ".zsupdate-failed.txt";
			// ...and what a file moved aside is named, until the next start.
			constexpr const char* kAside = ".zsold";

			constexpr std::int64_t kMaxPackageBytes = std::int64_t(512) << 20;
			constexpr std::size_t kMaxSmallFileBytes = 1 << 20;

			// -- Files, with UTF-8 paths (Windows' narrow calls would read them in
			// the ANSI code page) --

#ifdef _WIN32
			std::wstring Widen(const std::string& s) {
				if (s.empty())
					return std::wstring();
				const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
				std::wstring w(static_cast<std::size_t>(n), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
				return w;
			}

			std::string Narrow(const std::wstring& w) {
				if (w.empty())
					return std::string();
				const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
				                                  nullptr, nullptr);
				std::string s(static_cast<std::size_t>(n), '\0');
				WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr,
				                    nullptr);
				return s;
			}
#endif

			std::FILE* OpenFile(const std::string& path, const char* mode) {
#ifdef _WIN32
				return _wfopen(Widen(path).c_str(), Widen(mode).c_str());
#else
				return std::fopen(path.c_str(), mode);
#endif
			}

			bool Exists(const std::string& path) {
#ifdef _WIN32
				return GetFileAttributesW(Widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
				struct stat st;
				return ::lstat(path.c_str(), &st) == 0;
#endif
			}

			bool IsDir(const std::string& path) {
#ifdef _WIN32
				const DWORD a = GetFileAttributesW(Widen(path).c_str());
				return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
				struct stat st;
				return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
			}

			/** Moves `from` to `to`, over what `to` was if anything. */
			bool Move(const std::string& from, const std::string& to) {
#ifdef _WIN32
				return MoveFileExW(Widen(from).c_str(), Widen(to).c_str(),
				                   MOVEFILE_REPLACE_EXISTING) != 0;
#else
				return std::rename(from.c_str(), to.c_str()) == 0;
#endif
			}

			bool RemoveFile(const std::string& path) {
#ifdef _WIN32
				return DeleteFileW(Widen(path).c_str()) != 0 || !Exists(path);
#else
				return ::unlink(path.c_str()) == 0 || !Exists(path);
#endif
			}

			bool MakeDir(const std::string& path) {
#ifdef _WIN32
				return CreateDirectoryW(Widen(path).c_str(), nullptr) != 0 || IsDir(path);
#else
				return ::mkdir(path.c_str(), 0755) == 0 || IsDir(path);
#endif
			}

			/** Makes every folder of `path`'s parent under `root`. */
			bool MakeParents(const std::string& root, const std::string& relative) {
				std::size_t at = 0;
				while ((at = relative.find('/', at)) != std::string::npos) {
					if (!MakeDir(root + "/" + relative.substr(0, at)))
						return false;
					++at;
				}
				return true;
			}

			std::vector<std::string> ListDir(const std::string& path) {
				std::vector<std::string> out;
#ifdef _WIN32
				WIN32_FIND_DATAW fd;
				HANDLE h = FindFirstFileW(Widen(path + "/*").c_str(), &fd);
				if (h == INVALID_HANDLE_VALUE)
					return out;
				do {
					std::string name = Narrow(fd.cFileName);
					if (name != "." && name != "..")
						out.push_back(name);
				} while (FindNextFileW(h, &fd));
				FindClose(h);
#else
				if (DIR* d = ::opendir(path.c_str())) {
					while (auto* e = ::readdir(d)) {
						if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
							out.emplace_back(e->d_name);
					}
					::closedir(d);
				}
#endif
				return out;
			}

			void RemoveTree(const std::string& path) {
				if (IsDir(path)) {
					for (const std::string& entry : ListDir(path))
						RemoveTree(path + "/" + entry);
#ifdef _WIN32
					RemoveDirectoryW(Widen(path).c_str());
#else
					::rmdir(path.c_str());
#endif
					return;
				}
				RemoveFile(path);
			}

			std::string ReadSmallFile(const std::string& path) {
				std::string text;
				if (std::FILE* f = OpenFile(path, "rb")) {
					char buf[4096];
					std::size_t n;
					while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 &&
					       text.size() < kMaxSmallFileBytes)
						text.append(buf, n);
					std::fclose(f);
				}
				return text;
			}

			bool WriteSmallFile(const std::string& path, const std::string& text) {
				std::FILE* f = OpenFile(path, "wb");
				if (!f)
					return false;
				bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
				return std::fclose(f) == 0 && ok;
			}

			std::vector<std::string> Lines(const std::string& text) {
				std::vector<std::string> out;
				std::size_t start = 0;
				while (start < text.size()) {
					std::size_t nl = text.find('\n', start);
					std::string line =
					  text.substr(start, (nl == std::string::npos ? text.size() : nl) - start);
					start = nl == std::string::npos ? text.size() : nl + 1;
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					if (!line.empty() && line[0] != '#')
						out.push_back(line);
				}
				return out;
			}

			/** The running executable's own path, with `/` separators. */
			std::string ExecutablePath() {
#ifdef _WIN32
				std::vector<wchar_t> buf(32768);
				const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
				if (n == 0 || n >= buf.size())
					return std::string();
				std::string path = Narrow(std::wstring(buf.data(), n));
				std::replace(path.begin(), path.end(), '\\', '/');
				return path;
#elif defined(__linux__)
				std::vector<char> buf(4096);
				const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
				if (n <= 0)
					return std::string();
				return std::string(buf.data(), static_cast<std::size_t>(n));
#else
				return std::string();
#endif
			}

			/** The folder the running executable is in. */
			std::string GameFolder() {
				const std::string exe = ExecutablePath();
				const std::size_t slash = exe.find_last_of('/');
				return slash == std::string::npos ? std::string() : exe.substr(0, slash);
			}

			std::string BaseName(const std::string& path) {
				const std::size_t slash = path.find_last_of('/');
				return slash == std::string::npos ? path : path.substr(slash + 1);
			}

			/** A release tag this updater accepts: `v` and three numbers. */
			bool IsReleaseTag(const std::string& tag) {
				int parts = 0;
				std::size_t i = 1;
				if (tag.size() < 6 || tag[0] != 'v')
					return false;
				while (parts < 3) {
					std::size_t digits = 0;
					while (i < tag.size() && std::isdigit(static_cast<unsigned char>(tag[i]))) {
						++i;
						++digits;
					}
					if (digits == 0 || digits > 5)
						return false;
					if (++parts < 3) {
						if (i >= tag.size() || tag[i] != '.')
							return false;
						++i;
					}
				}
				return i == tag.size();
			}

			// -- Downloads --

			struct CURLEasyDeleter {
				void operator()(CURL* p) const { curl_easy_cleanup(p); }
			};

			struct Sink {
				std::FILE* file = nullptr; // or, when null, into `text`
				std::string text;
				std::int64_t limit = 0;
				std::int64_t received = 0;
				std::function<void(std::int64_t, std::int64_t)> progress;
			};

			std::size_t SinkWrite(void* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
				Sink& sink = *static_cast<Sink*>(userdata);
				const std::size_t bytes = size * nmemb;
				sink.received += static_cast<std::int64_t>(bytes);
				if (sink.received > sink.limit)
					return 0; // stops the transfer
				if (sink.file)
					return std::fwrite(ptr, 1, bytes, sink.file);
				sink.text.append(static_cast<const char*>(ptr), bytes);
				return bytes;
			}

			int SinkProgress(void* clientp, curl_off_t total, curl_off_t now, curl_off_t,
			                 curl_off_t) {
				Sink& sink = *static_cast<Sink*>(clientp);
				if (sink.progress)
					sink.progress(static_cast<std::int64_t>(now), static_cast<std::int64_t>(total));
				return 0;
			}

			/** GET `url` into `sink`. Returns "" on success, otherwise what went wrong. */
			bool InTestRun();
			std::string GetTestFile(const std::string& url, Sink& sink);

			std::string HttpGet(const std::string& url, Sink& sink) {
				if (InTestRun())
					return GetTestFile(url, sink);
				std::string err;
				for (int attempt = 1; attempt <= 3; ++attempt) {
					if (sink.file) {
						std::fflush(sink.file);
						std::rewind(sink.file);
					}
					sink.text.clear();
					sink.received = 0;

					std::unique_ptr<CURL, CURLEasyDeleter> h{curl_easy_init()};
					if (!h)
						return "curl_easy_init failed";
					curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
					SetupCurlTls(h.get());
					curl_easy_setopt(h.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
					curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
					// A release's file redirects to GitHub's storage: over https only.
#if LIBCURL_VERSION_NUM >= 0x075500
					curl_easy_setopt(h.get(), CURLOPT_PROTOCOLS_STR, "https");
					curl_easy_setopt(h.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
					curl_easy_setopt(h.get(), CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
					curl_easy_setopt(h.get(), CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
#endif
					curl_easy_setopt(h.get(), CURLOPT_FAILONERROR, 1L);
					curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
					curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, SinkWrite);
					curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &sink);
					curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
					curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_TIME, 30L);
					curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_LIMIT, 15L);
					curl_easy_setopt(h.get(), CURLOPT_NOPROGRESS, 0L);
					curl_easy_setopt(h.get(), CURLOPT_XFERINFOFUNCTION, SinkProgress);
					curl_easy_setopt(h.get(), CURLOPT_XFERINFODATA, &sink);
					const CURLcode rc = curl_easy_perform(h.get());
					if (rc == CURLE_OK)
						return std::string();
					long code = 0;
					curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &code);
					if (sink.received > sink.limit)
						return "the file is larger than expected";
					err = code >= 400 ? Format("HTTP {0}", code) : curl_easy_strerror(rc);
					const bool transient =
					  code >= 500 || rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST ||
					  rc == CURLE_OPERATION_TIMEDOUT || rc == CURLE_GOT_NOTHING ||
					  rc == CURLE_RECV_ERROR || rc == CURLE_SEND_ERROR || rc == CURLE_PARTIAL_FILE;
					if (!transient)
						break;
					std::this_thread::sleep_for(std::chrono::seconds(attempt));
				}
				return err;
			}

			/**
			 * For testing an install without a release: ZS_UPDATE_TEST_DIR names a
			 * folder holding what a release would (SHA256SUMS, its signature and
			 * the package), read instead of GitHub, and ZS_UPDATE_TEST_TAG the
			 * release to install from it, at once. The signature is checked as
			 * ever. Both are read once and cleared, so the game the install starts
			 * does not install it again.
			 */
			struct TestRun {
				std::string dir, tag;
			};

			const TestRun& GetTestRun() {
				static const TestRun run = [] {
					TestRun r;
					if (const char* dir = std::getenv("ZS_UPDATE_TEST_DIR"))
						r.dir = dir;
					if (const char* tag = std::getenv("ZS_UPDATE_TEST_TAG"))
						r.tag = tag;
#ifdef _WIN32
					_putenv("ZS_UPDATE_TEST_DIR=");
					_putenv("ZS_UPDATE_TEST_TAG=");
#else
					::unsetenv("ZS_UPDATE_TEST_DIR");
					::unsetenv("ZS_UPDATE_TEST_TAG");
#endif
					if (!r.dir.empty())
						SPLog("Update: testing with the release in %s", r.dir.c_str());
					return r;
				}();
				return run;
			}

			bool InTestRun() { return !GetTestRun().dir.empty(); }

			/** As `HttpGet`, from the test folder: `url`'s last part names the file. */
			std::string GetTestFile(const std::string& url, Sink& sink) {
				const std::string path = GetTestRun().dir + "/" + url.substr(url.find_last_of('/') + 1);
				std::FILE* f = OpenFile(path, "rb");
				if (!f)
					return "HTTP 404";
				char buf[65536];
				std::size_t n;
				bool ok = true;
				while (ok && (n = std::fread(buf, 1, sizeof(buf), f)) > 0)
					ok = SinkWrite(buf, 1, n, &sink) == n;
				std::fclose(f);
				return ok ? std::string() : "the file is larger than expected";
			}

			std::string Megabytes(std::int64_t bytes) {
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(bytes) / 1048576.0);
				return buf;
			}

			// -- Unpacking --

#ifdef _WIN32
			voidpf ZCALLBACK OpenZipWide(voidpf, const char* filename, int mode) {
				if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ ||
				    !filename)
					return nullptr;
				return OpenFile(filename, "rb");
			}
#endif

			unzFile OpenZip(const std::string& path) {
				zlib_filefunc_def def;
				fill_fopen_filefunc(&def);
#ifdef _WIN32
				def.zopen_file = OpenZipWide;
#endif
				return unzOpen2(path.c_str(), &def);
			}
		} // namespace

		// -- The download --

		class SelfUpdater::Download final : public Thread {
			SelfUpdater& owner;
			const std::string tag;
			const std::string folder;

			std::string Fetch(const std::string& name, std::string& out, std::size_t limit) {
				Sink sink;
				sink.limit = static_cast<std::int64_t>(limit);
				std::string err = HttpGet(kDownloadUrl + tag + "/" + name, sink);
				out = std::move(sink.text);
				return err;
			}

			/** Returns "" once the package is unpacked into `files`, otherwise why not. */
			std::string Prepare(std::vector<std::string>& files) {
				const std::string package = SelfUpdatePackage();
				const std::string staging = folder + "/" + kStaging;

				// The release's list of sums, and its signature by the release key.
				owner.SetStatus(_Tr("SelfUpdater", "Checking ZeroSpades X {0}...", tag), -1.0F);
				std::string sums, signature;
				std::string err = Fetch("SHA256SUMS", sums, kMaxSmallFileBytes);
				if (err.empty())
					err = Fetch("SHA256SUMS.sig", signature, 1024);
				if (!err.empty())
					return _Tr("SelfUpdater", "Cannot get the release's checksums: {0}", err);
				if (!VerifyUpdateSums(sums, signature, ReleaseSigningKey()))
					return _Tr("SelfUpdater",
					           "The release's checksums are not signed by the ZeroSpades X "
					           "release key.");

				const std::string asset = UpdateAssetName(tag, package);
				const std::optional<std::string> sum = FindUpdateSum(sums, asset);
				if (!sum)
					return _Tr("SelfUpdater", "The release has no {0} package.", package);

				RemoveTree(staging);
				if (!MakeDir(staging) || !MakeDir(staging + "/files"))
					return _Tr("SelfUpdater", "Cannot write to the game's folder.");

				// The package, checked against the signed sum before anything reads it.
				const std::string zipPath = staging + "/package.zip";
				{
					std::FILE* f = OpenFile(zipPath, "w+b");
					if (!f)
						return _Tr("SelfUpdater", "Cannot write to the game's folder.");
					const std::string downloading =
					  _Tr("SelfUpdater", "Downloading ZeroSpades X {0}", tag);
					Sink sink;
					sink.file = f;
					sink.limit = kMaxPackageBytes;
					sink.progress = [&](std::int64_t now, std::int64_t total) {
						if (total > 0)
							owner.SetStatus(downloading + ": " + Megabytes(now) + " / " +
							                  Megabytes(total) + " MB",
							                static_cast<float>(static_cast<double>(now) /
							                                   static_cast<double>(total)));
					};
					owner.SetStatus(downloading + "...", -1.0F);
					err = HttpGet(kDownloadUrl + tag + "/" + asset, sink);
					const bool closed = std::fclose(f) == 0;
					if (err.empty() && !closed)
						err = "cannot write the download";
					if (!err.empty())
						return _Tr("SelfUpdater", "Cannot download {0}: {1}", asset, err);
				}

				owner.SetStatus(_Tr("SelfUpdater", "Checking the download..."), -1.0F);
				{
					std::FILE* f = OpenFile(zipPath, "rb");
					if (!f)
						return _Tr("SelfUpdater", "Cannot read the download.");
					Sha256 h;
					std::vector<unsigned char> buf(1 << 16);
					std::size_t rd;
					while ((rd = std::fread(buf.data(), 1, buf.size(), f)) > 0)
						h.Update(buf.data(), rd);
					std::fclose(f);
					if (Sha256::ToHex(h.Finish()) != *sum)
						return _Tr("SelfUpdater",
						           "The download does not match the release's checksum.");
				}

				// Unpacked aside, so a damaged package leaves the game as it is.
				std::unique_ptr<void, int (*)(unzFile)> zip{OpenZip(zipPath), unzClose};
				if (!zip)
					return _Tr("SelfUpdater", "The download is not a zip.");
				std::vector<char> buf(1 << 16);
				std::int64_t unpacked = 0;
				std::set<std::string> seen;
				for (int rc = unzGoToFirstFile(zip.get()); rc == UNZ_OK;
				     rc = unzGoToNextFile(zip.get())) {
					char nameBuf[512];
					unz_file_info info;
					if (unzGetCurrentFileInfo(zip.get(), &info, nameBuf, sizeof(nameBuf), nullptr,
					                          0, nullptr, 0) != UNZ_OK)
						return _Tr("SelfUpdater", "The package is damaged.");
					const std::string name = nameBuf;
					if (!name.empty() && name.back() == '/')
						continue; // a folder
					std::string lower = name;
					for (char& c : lower)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (!IsSafeUpdatePath(name) || lower == "zerospadesx-files.txt" ||
					    lower.compare(0, 4, ".zsu") == 0 ||
					    lower.find(kAside) != std::string::npos || !seen.insert(lower).second)
						return _Tr("SelfUpdater", "The package holds a file it should not: {0}",
						           name);

					owner.SetStatus(_Tr("SelfUpdater", "Unpacking {0}", name), -1.0F);
					if (!MakeParents(staging + "/files", name) ||
					    unzOpenCurrentFile(zip.get()) != UNZ_OK)
						return _Tr("SelfUpdater", "Cannot unpack {0}.", name);
					const std::string out = staging + "/files/" + name;
					std::FILE* f = OpenFile(out, "wb");
					if (!f) {
						unzCloseCurrentFile(zip.get());
						return _Tr("SelfUpdater", "Cannot unpack {0}.", name);
					}
					int read;
					bool written = true;
					while ((read = unzReadCurrentFile(zip.get(), buf.data(),
					                                  static_cast<unsigned>(buf.size()))) > 0) {
						unpacked += read;
						if (unpacked > kMaxPackageBytes)
							break;
						written = written && std::fwrite(buf.data(), 1, static_cast<std::size_t>(read),
						                                 f) == static_cast<std::size_t>(read);
					}
					written = std::fclose(f) == 0 && written;
					// UNZ_CRCERROR here: the data does not match the zip's own checksum.
					const bool intact = unzCloseCurrentFile(zip.get()) == UNZ_OK && read == 0;
					if (unpacked > kMaxPackageBytes || !intact || !written)
						return _Tr("SelfUpdater", "Cannot unpack {0}.", name);
#ifndef _WIN32
					// The package's own permissions, as unzip would keep them: the
					// executable stays executable. Made on Unix, the mode is in the
					// high half of the external attributes.
					mode_t mode = 0644;
					if ((info.version >> 8) == 3 && ((info.external_fa >> 16) & 0777))
						mode = static_cast<mode_t>((info.external_fa >> 16) & 0755);
					::chmod(out.c_str(), mode | 0600);
#endif
					files.push_back(name);
				}
				zip.reset();
				RemoveFile(zipPath);

				// A package without this program would leave nothing to start.
				const std::string exe = BaseName(ExecutablePath());
				if (std::find(files.begin(), files.end(), exe) == files.end())
					return _Tr("SelfUpdater", "The package has no {0}.", exe);
				return std::string();
			}

		public:
			Download(SelfUpdater& owner, std::string tag)
			    : owner(owner), tag(std::move(tag)), folder(GameFolder()) {}

			void Run() override {
				std::vector<std::string> files;
				std::string err;
				try {
					err = Prepare(files);
				} catch (const std::exception& ex) {
					err = ex.what();
				}
				if (!err.empty()) {
					RemoveTree(folder + "/" + kStaging);
					owner.Fail(err);
					return;
				}
				SPLog("Update: ZeroSpades X %s is unpacked and ready to install.", tag.c_str());
				owner.Finish(std::move(files));
			}
		};

		// -- SelfUpdater --

		SelfUpdater& SelfUpdater::Get() {
			// Never destroyed: the download may still run when the process exits.
			static SelfUpdater* instance = new SelfUpdater();
			return *instance;
		}

		bool SelfUpdater::IsSupported() {
			static const bool supported = [] {
				if (SelfUpdatePackage().empty())
					return false;
				const std::string folder = GameFolder();
				if (folder.empty())
					return false;
#ifndef _WIN32
				// The game reads ./Resources, so it must run from its own folder
				// for the paks an update installs to be the ones it reads.
				char cwd[4096];
				if (!::getcwd(cwd, sizeof(cwd)))
					return false;
				char* real = ::realpath(cwd, nullptr);
				const bool here = real && folder == real;
				std::free(real);
				if (!here) {
					SPLog("Update: not running from the game's folder (%s); only the release "
					      "page is offered.",
					      folder.c_str());
					return false;
				}
#endif
				// Writable, e.g. not unpacked somewhere only an administrator can write
				const std::string probe = folder + "/.zsupdate-probe";
				std::FILE* f = OpenFile(probe, "wb");
				if (!f) {
					SPLog("Update: the game's folder (%s) is read-only; only the release page "
					      "is offered.",
					      folder.c_str());
					return false;
				}
				std::fclose(f);
				RemoveFile(probe);
				return true;
			}();
			return supported;
		}

		void SelfUpdater::RunTestIfAsked() {
			const std::string& tag = GetTestRun().tag;
			if (tag.empty())
				return;
			SelfUpdater& self = Get();
			switch (self.GetProgress().state) {
				case State::Idle: self.Start(tag); break;
				case State::Ready: self.InstallAndRestart(); break;
				default: break;
			}
		}

		void SelfUpdater::CleanUpAfterUpdate() {
			GetTestRun(); // read, and cleared, before anything starts another process
			if (SelfUpdatePackage().empty())
				return;
			const std::string folder = GameFolder();
			if (folder.empty())
				return;

			const std::string note = folder + "/" + kFailureNote;
			const std::string failure = ReadSmallFile(note);
			if (!failure.empty()) {
				SPLog("[!] Update: the last install failed and was undone: %s", failure.c_str());
				RemoveFile(note);
				SelfUpdater& self = Get();
				std::lock_guard<std::mutex> lk(self.mutex);
				self.progress.state = State::Failed;
				self.progress.error = failure;
			}
			RemoveTree(folder + "/" + kStaging);

			// What the last install moved aside, in the folders its files are in.
			// The exiting old version can hold its .exe a moment longer, so this
			// goes on trying for a while, out of the way.
			std::set<std::string> dirs{""};
			for (const std::string& file : Lines(ReadSmallFile(folder + "/" + kManifest))) {
				const std::size_t slash = file.find_last_of('/');
				if (slash != std::string::npos && IsSafeUpdatePath(file))
					dirs.insert(file.substr(0, slash));
			}
			std::vector<std::string> aside;
			for (const std::string& dir : dirs) {
				const std::string path = dir.empty() ? folder : folder + "/" + dir;
				for (const std::string& name : ListDir(path)) {
					if (name.find(kAside) != std::string::npos)
						aside.push_back(path + "/" + name);
				}
			}
			if (aside.empty())
				return;
			std::thread([aside]() mutable {
				for (int attempt = 0; attempt < 30 && !aside.empty(); ++attempt) {
					aside.erase(std::remove_if(aside.begin(), aside.end(), RemoveFile), aside.end());
					if (!aside.empty())
						std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}).detach();
		}

		void SelfUpdater::Start(const std::string& tag) {
			{
				std::lock_guard<std::mutex> lk(mutex);
				if (progress.state == State::Working || progress.state == State::Ready)
					return;
				progress = Progress{};
				progress.tag = tag;
				if (!IsSupported() || !IsReleaseTag(tag)) {
					progress.state = State::Failed;
					progress.error = _Tr("SelfUpdater", "This copy cannot update itself.");
					return;
				}
				progress.state = State::Working;
				progress.status = _Tr("SelfUpdater", "Checking ZeroSpades X {0}...", tag);
			}
			SPLog("Update: downloading ZeroSpades X %s (%s).", tag.c_str(),
			      SelfUpdatePackage().c_str());
			auto* download = new Download(*this, tag);
			download->Start();
			download->MarkForAutoDeletion();
		}

		SelfUpdater::Progress SelfUpdater::GetProgress() {
			std::lock_guard<std::mutex> lk(mutex);
			return progress;
		}

		void SelfUpdater::SetStatus(const std::string& status, float fraction) {
			std::lock_guard<std::mutex> lk(mutex);
			progress.status = status;
			progress.fraction = fraction;
		}

		void SelfUpdater::Fail(const std::string& error) {
			SPLog("[!] Update failed: %s", error.c_str());
			std::lock_guard<std::mutex> lk(mutex);
			progress.state = State::Failed;
			progress.error = error;
		}

		void SelfUpdater::Finish(std::vector<std::string>&& unpacked) {
			std::lock_guard<std::mutex> lk(mutex);
			files = std::move(unpacked);
			progress.state = State::Ready;
			progress.status.clear();
			progress.fraction = 1.0F;
		}

		void SelfUpdater::InstallAndRestart() {
			std::vector<std::string> newFiles;
			std::string tag;
			{
				std::lock_guard<std::mutex> lk(mutex);
				if (progress.state != State::Ready)
					return;
				newFiles = files;
				tag = progress.tag;
			}
			const std::string folder = GameFolder();
			const std::string exe = ExecutablePath();
			if (folder.empty() || exe.empty()) {
				Fail(_Tr("SelfUpdater", "Cannot find the game's folder."));
				return;
			}
			SPLog("Update: installing ZeroSpades X %s and restarting.", tag.c_str());

			// Started again from where the new version now is.
			g_executablePath = exe;

			RelaunchAfterUpdate([folder, newFiles, tag] {
				// From here on, nothing can be logged: the log is closed.
				const std::string staging = folder + "/" + kStaging + "/files/";
				struct Step {
					std::string target, aside, staged; // staged empty: only moved aside
				};
				std::vector<Step> done;
				std::string failure;

				// A file moved aside gets the first free name of `<name>.zsold<n>`
				// (an earlier one can still be held by a process exiting).
				auto moveAside = [&](const std::string& target, Step& step) {
					for (int n = 0; n < 10; ++n) {
						const std::string aside =
						  target + kAside + (n == 0 ? std::string() : std::to_string(n));
						if (Exists(aside) && !RemoveFile(aside))
							continue;
						if (!Move(target, aside))
							return false;
						step.aside = aside;
						return true;
					}
					return false;
				};

				for (const std::string& file : newFiles) {
					Step step;
					step.target = folder + "/" + file;
					if (!MakeParents(folder, file)) {
						failure = "cannot make the folder for " + file;
						break;
					}
					if (Exists(step.target) && !moveAside(step.target, step)) {
						failure = "cannot move " + file + " aside (is it open elsewhere?)";
						break;
					}
					if (!Move(staging + file, step.target)) {
						if (!step.aside.empty())
							Move(step.aside, step.target);
						failure = "cannot put the new " + file + " in place";
						break;
					}
					step.staged = staging + file;
					done.push_back(step);
				}

				if (failure.empty()) {
					// What the last install put there and this package no longer has
					const std::set<std::string> kept(newFiles.begin(), newFiles.end());
					for (const std::string& old :
					     Lines(ReadSmallFile(folder + "/" + kManifest))) {
						if (kept.count(old) || !IsSafeUpdatePath(old) ||
						    !Exists(folder + "/" + old))
							continue;
						Step step;
						step.target = folder + "/" + old;
						if (moveAside(step.target, step))
							done.push_back(step);
					}
					std::string manifest =
					  "# The files ZeroSpades X " + tag +
					  " installed here. Its next update removes the ones it no longer has.\n";
					for (const std::string& file : newFiles)
						manifest += file + "\n";
					WriteSmallFile(folder + "/" + kManifest, manifest);
					return;
				}

				// Undone, newest first: the old version starts again, and reports it.
				for (auto it = done.rbegin(); it != done.rend(); ++it) {
					if (!it->staged.empty())
						Move(it->target, it->staged);
					if (!it->aside.empty())
						Move(it->aside, it->target);
				}
				WriteSmallFile(folder + "/" + kFailureNote, "ZeroSpades X " + tag + ": " + failure);
			});
		}
	} // namespace gui
} // namespace spades
