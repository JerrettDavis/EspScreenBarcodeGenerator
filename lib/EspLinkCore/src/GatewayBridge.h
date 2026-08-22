#pragma once

#include <cstdint>

#include "HopFrame.h"

namespace esplink {

// Builds the outbound hop-frame header GatewayRelay uses when relaying a reassembled message
// from one carrier to the other (docs/PROTOCOL_V2.md §10). Session/route/traffic-class fields
// pass through unchanged from whichever carrier the message arrived on -- only `profileId`
// (which carrier this frame is now going out on) and `linkMessageId` (a fresh counter local to
// the outbound leg, so concurrent relayed messages on that leg don't collide) are overwritten.
// `fragmentIndex`/`fragmentCount` are left at the source's values; `fragmentIntoHopFrames`
// overwrites both per-fragment when it re-fragments the payload for this header.
inline HopFrameHeader relayHeaderFor(const HopFrameHeader& source, CarrierProfileId destinationProfile,
                                     uint32_t freshLinkMessageId) {
    HopFrameHeader out = source;
    out.profileId = destinationProfile;
    out.linkMessageId = freshLinkMessageId;
    return out;
}

}  // namespace esplink
