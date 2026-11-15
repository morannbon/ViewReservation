#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ViewReservationUi {

struct PanelViewportRowIdentity {
    std::uint64_t ChannelKey = 0;
    std::uint32_t EventId = 0;
    bool IsFuture = false;
};

// Both keyboard selection and the viewport's top-row anchor use this same
// stable identity mapping when the EPG changes or rows expand/collapse.
template<class IdentityAt>
int FindPanelViewportRow(std::size_t count, const PanelViewportRowIdentity &previousIdentity,
    int previousIndex, IdentityAt identityAt) {
    if (previousIndex < 0 || previousIdentity.ChannelKey == 0) return -1;
    int nearest = -1;
    std::int64_t distance = (std::numeric_limits<std::int64_t>::max)();
    for (std::size_t i = 0; i < count && i <= static_cast<std::size_t>((std::numeric_limits<int>::max)()); ++i) {
        const auto identity = identityAt(i);
        if (identity.ChannelKey != previousIdentity.ChannelKey ||
            identity.IsFuture != previousIdentity.IsFuture) continue;
        if (previousIdentity.EventId == 0 || identity.EventId == previousIdentity.EventId)
            return static_cast<int>(i);
        const std::int64_t candidateDistance = static_cast<std::int64_t>(i) - previousIndex;
        const std::int64_t absolute = candidateDistance < 0 ? -candidateDistance : candidateDistance;
        if (absolute < distance) {
            distance = absolute;
            nearest = static_cast<int>(i);
        }
    }
    return nearest;
}

} // namespace ViewReservationUi
