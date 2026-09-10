// edgeline: pure parsing of `xinput list --short` output.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extracted so the decision that matters can be tested. Creating a master
// pointer on X11 also creates a master keyboard, and both come with their own
// XTEST slave devices. Counting those XTEST slaves as "someone is using this
// master" meant the master could never be removed, so every use of the
// Own-pointer touch mode permanently left a second master keyboard behind, and
// a second master keyboard splits keyboard focus: typing goes to whatever the
// other pointer last focused.
#pragma once

#include <string>
#include <vector>

namespace xen::xinput {

// True when a master with this id still has a real slave attached, ignoring
// the XTEST devices X creates alongside every master.
bool masterHasRealSlaves(const std::vector<std::string>& lines, int masterId);

// Id of the master pointer whose name contains `name`, or -1.
int findMasterPointerId(const std::vector<std::string>& lines, const std::string& name);

std::vector<std::string> splitLines(const std::string& text);

} // namespace xen::xinput
