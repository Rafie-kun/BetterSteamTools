#pragma once

// Local-only stat journal for unlocked games.
//
// Achievement unlocks travel as stat bits: the game ships (stat_id,
// stat_value) pairs in ClientStoreUserStats (820) / ClientStoreUserStats2
// (5466). With [stats] local_only, those stores never reach Valve — they are
// journaled here, persisted under <Steam>/opensteamtool/stats/<appid>.dat,
// and injected back into ClientGetUserStatsResponse (819) so the overlay and
// the library page show local progress. Nothing here ever leaves the machine
// except through CloudRedirect's own sync (which the user configures there).

#include <cstdint>
#include <utility>
#include <vector>

#include "Steam/Types.h"

namespace LocalStats {

    // Merge store pairs into the journal (and persist). Returns merged count.
    uint32_t RecordStore(AppId_t appId,
                         const std::vector<std::pair<uint32_t, uint32_t>>& pairs);

    // Drop all journaled stats for an app (explicit_reset from the game).
    void Clear(AppId_t appId);

    // Snapshot of journaled (stat_id, stat_value) pairs. Empty when unknown.
    std::vector<std::pair<uint32_t, uint32_t>> GetStats(AppId_t appId);

} // namespace LocalStats
