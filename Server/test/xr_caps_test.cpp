// Checks the OpenXR extension table in Sources/VR/XrCaps.cpp. From the repository
// root:
//   g++ -std=c++17 -I Sources Server/test/xr_caps_test.cpp Sources/VR/XrCaps.cpp
//
// What a runtime lists is enabled and reported; what it does not list is neither
// (the control). vr_disableExtensions takes one away, as a Quest-like PC run does,
// and leaves the rest alone (control). An extension whose prerequisite is gone
// goes too, and stays when the prerequisite is there (control). Android's own
// extension is only taken on Android.
#include <VR/XrCaps.h>
#include <algorithm>
#include <cstdio>

using namespace spades::vr;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static bool In(const std::vector<std::string>& v, const char* s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

int main() {
	// Every row has a name, and no two rows share one.
	for (int i = 0; i < (int)XrCap::Count; ++i) {
		CHECK(std::string(XrCapExtension((XrCap)i)).rfind("XR_", 0) == 0);
		for (int j = 0; j < i; ++j)
			CHECK(std::string(XrCapExtension((XrCap)i)) != XrCapExtension((XrCap)j));
	}

	const std::vector<std::string> quest = {
	  "XR_KHR_vulkan_enable2", "XR_KHR_composition_layer_depth",
	  "XR_KHR_composition_layer_cylinder", "XR_FB_display_refresh_rate",
	  "XR_FB_swapchain_update_state", "XR_FB_foveation", "XR_FB_foveation_configuration",
	  "XR_FB_foveation_vulkan", "XR_FB_space_warp", "XR_EXT_hand_tracking",
	  "XR_FB_hand_tracking_aim", "XR_KHR_android_create_instance", "XR_SOMETHING_else"};

	{
		std::vector<std::string> enable = {"XR_KHR_vulkan_enable2"};
		XrCaps c = NegotiateXrCaps(quest, "", true, enable);
		CHECK(c[XrCap::SpaceWarp] && In(enable, "XR_FB_space_warp"));
		CHECK(c[XrCap::FoveationVulkan] && c[XrCap::HandTrackingAim]);
		CHECK(c[XrCap::AndroidCreateInstance]);
		// Control: not listed, not taken; unknown names are not ours to enable.
		CHECK(!c[XrCap::Passthrough] && !In(enable, "XR_FB_passthrough"));
		CHECK(!In(enable, "XR_SOMETHING_else"));
		// The required one is kept and not doubled.
		CHECK(std::count(enable.begin(), enable.end(), "XR_KHR_vulkan_enable2") == 1);
	}
	{
		// The same runtime on a PC build: Android's extension is not asked for.
		std::vector<std::string> enable;
		XrCaps c = NegotiateXrCaps(quest, "", false, enable);
		CHECK(!c[XrCap::AndroidCreateInstance] && !In(enable, "XR_KHR_android_create_instance"));
		CHECK(c[XrCap::SpaceWarp]); // control
	}
	{
		// Taken away by the setting, and only that one.
		std::vector<std::string> enable;
		XrCaps c = NegotiateXrCaps(quest, "XR_FB_space_warp, XR_KHR_composition_layer_depth",
		                           false, enable);
		CHECK(!c[XrCap::SpaceWarp] && !In(enable, "XR_FB_space_warp"));
		CHECK(!c[XrCap::CompositionLayerDepth]);
		CHECK(c[XrCap::CompositionLayerCylinder]); // control
	}
	{
		// A prerequisite gone takes its dependants with it, transitively.
		std::vector<std::string> enable;
		XrCaps c = NegotiateXrCaps(quest, "XR_FB_swapchain_update_state XR_EXT_hand_tracking",
		                           false, enable);
		CHECK(!c[XrCap::Foveation] && !c[XrCap::FoveationVulkan] &&
		      !c[XrCap::FoveationConfiguration]);
		CHECK(!In(enable, "XR_FB_foveation_vulkan"));
		CHECK(!c[XrCap::HandTrackingAim] && !In(enable, "XR_FB_hand_tracking_aim"));
		CHECK(c[XrCap::DisplayRefreshRate]); // control: unrelated rows stay
	}

	std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ok", failures,
	            failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
