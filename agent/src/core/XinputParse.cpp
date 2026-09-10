// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/XinputParse.h"

#include <regex>
#include <sstream>

namespace xen::xinput {

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        out.push_back(line);
    return out;
}

int findMasterPointerId(const std::vector<std::string>& lines, const std::string& name)
{
    static const std::regex idre(R"(id=(\d+))");
    for (const std::string& line : lines) {
        if (line.find(name) == std::string::npos)
            continue;
        if (line.find("master pointer") == std::string::npos)
            continue;
        std::smatch m;
        if (std::regex_search(line, m, idre))
            return std::stoi(m[1].str());
    }
    return -1;
}

bool masterHasRealSlaves(const std::vector<std::string>& lines, int masterId)
{
    if (masterId < 0)
        return false;
    const std::string tag = "(" + std::to_string(masterId) + ")";
    for (const std::string& line : lines) {
        if (line.find("slave") == std::string::npos)
            continue;
        if (line.find(tag) == std::string::npos)
            continue;
        // X creates an XTEST pointer and keyboard for every master. They are
        // not a reason to keep one alive.
        if (line.find("XTEST") != std::string::npos)
            continue;
        return true;
    }
    return false;
}

} // namespace xen::xinput
