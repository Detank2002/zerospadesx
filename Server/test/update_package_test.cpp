// The self-updater's checks: SHA-256, the signed SHA256SUMS and the paths a
// package may write. The signature below was made by OpenSSL (`openssl
// pkeyutl -sign -rawin`, as release.yml signs), with a key made for this test
// only, so this also checks that OpenSSL's Ed25519 and Monocypher's agree.

#include <cstdio>
#include <cstring>
#include <string>

#include <Core/Sha256.h>
#include <Gui/UpdatePackage.h>

using namespace spades;
using namespace spades::gui;

namespace {
	int failures = 0;

	void Check(bool ok, const char* what) {
		if (!ok) {
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	std::string HexOf(const std::string& s) {
		Sha256 h;
		h.Update(s.data(), s.size());
		return Sha256::ToHex(h.Finish());
	}

	std::string Bytes(const unsigned char* p, std::size_t n) {
		return std::string(reinterpret_cast<const char*>(p), n);
	}

	const std::string kSums =
	  "d3b0abc687a33ba766d8423999bba5d07bc4ff41cc9bb68fceebec186c2f3c05  "
	  "ZeroSpadesX-v0.3.0-windows-x64.zip\n"
	  "4fd49be41e9dfda87979ae7b45b16f9b29b3b003e35820fe24a44056eb5b264d  "
	  "ZeroSpadesX-v0.3.0-linux-x64.zip\n";

	const UpdatePublicKey kTestKey = {0x51, 0x1a, 0x35, 0xdb, 0xd0, 0xb6, 0xf4, 0x28,
	                                  0x11, 0x73, 0xcd, 0xbb, 0x37, 0x02, 0x0e, 0x6a,
	                                  0x65, 0xf6, 0xb1, 0xdf, 0x3e, 0x66, 0x14, 0x30,
	                                  0x70, 0xc0, 0xe3, 0xd2, 0xf3, 0x7c, 0x5b, 0x02};

	const unsigned char kSignature[64] = {
	  0x6c, 0x6c, 0x3b, 0x50, 0x41, 0x71, 0x41, 0x49, 0xac, 0x55, 0x22, 0x1b, 0x0e,
	  0xea, 0x45, 0x37, 0xa8, 0x7d, 0x50, 0xaa, 0xff, 0x76, 0x74, 0x34, 0x4c, 0x83,
	  0xb0, 0xa9, 0x9b, 0x1a, 0xba, 0xec, 0x7a, 0x8f, 0x65, 0xc2, 0xfc, 0xb8, 0x14,
	  0x42, 0xeb, 0xe3, 0x0b, 0x7a, 0x7b, 0x8e, 0xbe, 0x14, 0x42, 0x7e, 0xfe, 0x82,
	  0x2e, 0xbc, 0x21, 0x56, 0xb3, 0x8d, 0x38, 0xb4, 0x97, 0x63, 0xb9, 0x00};
} // namespace

int main() {
	// FIPS 180-4 examples, and one past a block boundary in uneven pieces
	Check(HexOf("") ==
	        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	      "sha256 of nothing");
	Check(HexOf("abc") ==
	        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	      "sha256 of abc");
	Check(HexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
	        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
	      "sha256 of two blocks");
	{
		const std::string a(1001, 'a');
		Sha256 h;
		for (int i = 0; i < 1000; ++i)
			h.Update(a.data(), i % 2 ? 999 : 1001); // a million 'a's, in odd pieces
		Check(Sha256::ToHex(h.Finish()) ==
		        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
		      "sha256 of a million a's");
	}

	// The signature, and anything changed under it
	const std::string sig = Bytes(kSignature, sizeof(kSignature));
	Check(VerifyUpdateSums(kSums, sig, kTestKey), "OpenSSL's signature verifies");
	{
		std::string tampered = kSums;
		tampered[0] = 'e';
		Check(!VerifyUpdateSums(tampered, sig, kTestKey), "a changed sum fails");
		Check(!VerifyUpdateSums(kSums + "\n", sig, kTestKey), "an added line fails");
		std::string badSig = sig;
		badSig[10] ^= 1;
		Check(!VerifyUpdateSums(kSums, badSig, kTestKey), "a changed signature fails");
		Check(!VerifyUpdateSums(kSums, sig.substr(0, 63), kTestKey), "a short signature fails");
		Check(!VerifyUpdateSums(kSums, sig, ReleaseSigningKey()),
		      "another key's signature fails");
	}

	// The sums
	Check(FindUpdateSum(kSums, "ZeroSpadesX-v0.3.0-linux-x64.zip").value_or("") ==
	        "4fd49be41e9dfda87979ae7b45b16f9b29b3b003e35820fe24a44056eb5b264d",
	      "finds a package's sum");
	Check(!FindUpdateSum(kSums, "ZeroSpadesX-v0.3.0-windows-x86.zip"), "no sum for a missing one");
	Check(!FindUpdateSum(kSums, "linux-x64.zip"), "no sum for part of a name");
	Check(FindUpdateSum("D3B0ABC687A33BA766D8423999BBA5D07BC4FF41CC9BB68FCEEBEC186C2F3C05 *a.zip\r\n",
	                    "a.zip")
	        .value_or("") == "d3b0abc687a33ba766d8423999bba5d07bc4ff41cc9bb68fceebec186c2f3c05",
	      "binary-mode line, upper case, CRLF");
	Check(!FindUpdateSum("zzb0abc687a33ba766d8423999bba5d07bc4ff41cc9bb68fceebec186c2f3c05  a.zip\n",
	                     "a.zip"),
	      "a malformed sum");
	Check(UpdateAssetName("v0.3.1", "windows-x64") == "ZeroSpadesX-v0.3.1-windows-x64.zip",
	      "the asset's name");

	// The paths
	Check(IsSafeUpdatePath("ZeroSpades.exe"), "a plain name");
	Check(IsSafeUpdatePath("Resources/pak002-Base.pak"), "a name in a folder");
	Check(!IsSafeUpdatePath("../evil.exe"), "a parent");
	Check(!IsSafeUpdatePath("Resources/../../evil"), "a parent inside");
	Check(!IsSafeUpdatePath("/etc/passwd"), "an absolute path");
	Check(!IsSafeUpdatePath("C:/Windows/evil.dll"), "a drive");
	Check(!IsSafeUpdatePath("Resources\\evil.pak"), "a backslash");
	Check(!IsSafeUpdatePath("Resources/"), "a folder");
	Check(!IsSafeUpdatePath("a//b"), "an empty part");
	Check(!IsSafeUpdatePath("./a"), "a dot");
	Check(!IsSafeUpdatePath(""), "nothing");

	if (failures == 0)
		std::printf("update_package_test: all passed\n");
	return failures == 0 ? 0 : 1;
}
