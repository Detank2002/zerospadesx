// The key a server's extension list is remembered under: the typed address and
// the master list's form of the same server must come out the same.

#include <cstdint>
#include <cstdio>
#include <string>

#include <Client/ServerExtensionLog.h>

using namespace spades::client;

namespace {
	int failures = 0;

	void Check(bool ok, const char* what) {
		if (!ok) {
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	void Expect(const std::string& in, const std::string& want) {
		const std::string got = ServerExtensionKey(in);
		if (got != want) {
			std::printf("FAIL: %s -> %s (wanted %s)\n", in.c_str(), got.c_str(), want.c_str());
			++failures;
		}
	}
}

int main() {
	Expect("aos://16777343:32887", "127.0.0.1:32887");
	Expect("aos:///16777343:32887", "127.0.0.1:32887");
	Expect("127.0.0.1:32887", "127.0.0.1:32887");
	Expect("127.0.0.1", "127.0.0.1:32887");
	Expect("16777343:32887", "127.0.0.1:32887");
	Expect("1.2.3.4:32887:0.75", "1.2.3.4:32887");
	Expect("Play.Example.COM:1234", "play.example.com:1234");

	const std::uint32_t v = 1u | (2u << 8) | (3u << 16) | (4u << 24);
	const std::string master = "aos://" + std::to_string(v) + ":32887";
	Check(ServerExtensionKey(master) == "1.2.3.4:32887", "master-list form of 1.2.3.4");
	Check(ServerExtensionKey(master) == ServerExtensionKey("1.2.3.4:32887"),
	      "master-list form matches typed form");

	if (failures == 0) {
		std::printf("server_extension_key_test: all passed\n");
		return 0;
	}
	return 1;
}
