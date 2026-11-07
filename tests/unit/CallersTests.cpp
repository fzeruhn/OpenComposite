#include "doctest.h"

#include "callers_support.h"

#include <algorithm>

TEST_CASE("Callers exist for a known version")
{
	const std::vector<CallerFn>* callers = FindCallers("IVRSettings_003");
	REQUIRE(callers != nullptr);
	// IVRSettings_003.h (OpenVR 2.15.6) has 11 methods
	CHECK(callers->size() == 11);
}

TEST_CASE("No callers for an unknown version")
{
	CHECK(FindCallers("IVRNope_001") == nullptr);
}

TEST_CASE("Callers for every listed version with a header")
{
	std::vector<std::string> versions = GeneratedVersions();
	for (const char* v : { "IVRSystem_022", "IVRSystem_026", "IVRCompositor_029", "IVROverlay_028", "IVRInput_011",
	         "IVRTrackedCamera_006", "IVRResources_001", "IVRDebug_001" }) {
		CAPTURE(v);
		CHECK(std::find(versions.begin(), versions.end(), v) != versions.end());
	}
}
