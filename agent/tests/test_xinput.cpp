// SPDX-License-Identifier: GPL-3.0-or-later
// This exists because of a real fault. The Own-pointer touch mode creates a
// master pointer, which on X11 also creates a master keyboard. The cleanup that
// should remove the pair asked "does this master still have slaves?" and every
// master has its own XTEST slaves, so the answer was always yes and the pair was
// never removed. A second master keyboard splits keyboard focus, so typing went
// to whatever the stray pointer had last focused instead of the main screen.
#include "core/XinputParse.h"

#include "check.h"

#include <cstdio>

using namespace xen::xinput;

namespace {

// Captured from the machine while the fault was present.
const char* kOrphanedMaster = R"(
⎡ Virtual core pointer                    	id=2	[master pointer  (3)]
⎜   ↳ Virtual core XTEST pointer              	id=4	[slave  pointer  (2)]
⎜   ↳ Logitech USB Receiver Mouse             	id=10	[slave  pointer  (2)]
⎣ Virtual core keyboard                   	id=3	[master keyboard (2)]
    ↳ Virtual core XTEST keyboard             	id=5	[slave  keyboard (3)]
⎡ xeneon-edge pointer                     	id=23	[master pointer  (24)]
⎜   ↳ xeneon-edge XTEST pointer               	id=25	[slave  pointer  (23)]
⎣ xeneon-edge keyboard                    	id=24	[master keyboard (23)]
    ↳ xeneon-edge XTEST keyboard              	id=26	[slave  keyboard (24)]
∼ wch.cn TouchScreen                      	id=22	[floating slave]
)";

// The same tree while Own-pointer mode is genuinely in use.
const char* kMasterInUse = R"(
⎡ Virtual core pointer                    	id=2	[master pointer  (3)]
⎣ Virtual core keyboard                   	id=3	[master keyboard (2)]
⎡ xeneon-edge pointer                     	id=23	[master pointer  (24)]
⎜   ↳ xeneon-edge XTEST pointer               	id=25	[slave  pointer  (23)]
⎜   ↳ wch.cn TouchScreen                      	id=22	[slave  pointer  (23)]
⎣ xeneon-edge keyboard                    	id=24	[master keyboard (23)]
    ↳ xeneon-edge XTEST keyboard              	id=26	[slave  keyboard (24)]
)";

const char* kNoEdgeMaster = R"(
⎡ Virtual core pointer                    	id=2	[master pointer  (3)]
⎜   ↳ wch.cn TouchScreen                      	id=22	[slave  pointer  (2)]
⎣ Virtual core keyboard                   	id=3	[master keyboard (2)]
)";

} // namespace

int main()
{
    // The fault itself: a master whose only slaves are its own XTEST devices
    // is not in use, and must be removable.
    {
        const auto lines = splitLines(kOrphanedMaster);
        const int id = findMasterPointerId(lines, "xeneon-edge");
        CHECK(id == 23);
        CHECK(!masterHasRealSlaves(lines, id));
    }

    // A master with the digitizer attached is genuinely in use and must stay.
    {
        const auto lines = splitLines(kMasterInUse);
        const int id = findMasterPointerId(lines, "xeneon-edge");
        CHECK(id == 23);
        CHECK(masterHasRealSlaves(lines, id));
    }

    // No such master: nothing to find and nothing to remove.
    {
        const auto lines = splitLines(kNoEdgeMaster);
        CHECK(findMasterPointerId(lines, "xeneon-edge") == -1);
        CHECK(!masterHasRealSlaves(lines, -1));
    }

    // The core master must never be mistaken for ours, whatever it holds.
    {
        const auto lines = splitLines(kOrphanedMaster);
        CHECK(masterHasRealSlaves(lines, 2));    // the mouse is a real slave
        CHECK(findMasterPointerId(lines, "Virtual core") == 2);
    }

    // A keyboard master id must not be confused with the pointer master id.
    // Removing the pointer removes the pair, so the pointer id is the one to find.
    {
        const auto lines = splitLines(kOrphanedMaster);
        CHECK(findMasterPointerId(lines, "xeneon-edge") != 24);
    }

    // Ids that merely share digits must not match: "(2)" is not "(23)".
    {
        const auto lines = splitLines(kOrphanedMaster);
        CHECK(!masterHasRealSlaves(lines, 25));
    }

    CHECK(splitLines("").empty());
    CHECK(splitLines("one\ntwo\n").size() == 2);

    return xen::test::report("test_xinput");
}
