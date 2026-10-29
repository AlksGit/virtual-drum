#pragma once

#include <string_view>

namespace vdrum
{

// Velocity layer named by a sample file stem, for one pad name (both already lower case, without
// spaces, '_' or '-'): 0 for "snare", N for "snarevN" (from "snare_v3.wav"), -1 if it is not
// a file of that pad.
inline int padLayerFromStem (std::string_view stem, std::string_view padName)
{
    if (stem == padName)
        return 0;
    if (stem.size() < padName.size() + 2 || stem.substr (0, padName.size()) != padName)
        return -1;

    const auto rest = stem.substr (padName.size());
    if (rest[0] != 'v' || rest.size() > 4)
        return -1;

    int layer = 0;
    for (const auto c : rest.substr (1))
    {
        if (c < '0' || c > '9')
            return -1;
        layer = layer * 10 + (c - '0');
    }
    return layer;
}

} // namespace vdrum
