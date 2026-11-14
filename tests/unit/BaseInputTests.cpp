// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Reimpl/BaseInput.h"

TEST_CASE("Mixed_priority_sets_without_second_set")
{
	// UpdateActionState's check, which it makes before anything else
	BaseInput::VRActiveActionSet_t sets[2] = {};
	sets[0].ulActionSet = 0xabcd0001; // as GetActionSetHandle hands out
	sets[0].nPriority = 1;
	// The second entry names no set, and has another priority
	sets[1].ulActionSet = vr::k_ulInvalidActionSetHandle;
	sets[1].nPriority = 0;
	CHECK(BaseInput::CheckActiveSets(sets, sizeof(sets[0]), 2) == vr::VRInputError_InvalidParam);

	// One set alone, or two that both name one, pass the check
	CHECK(BaseInput::CheckActiveSets(sets, sizeof(sets[0]), 1) == vr::VRInputError_None);
	sets[1].ulActionSet = 0xabcd0002;
	CHECK(BaseInput::CheckActiveSets(sets, sizeof(sets[0]), 2) == vr::VRInputError_None);
}
