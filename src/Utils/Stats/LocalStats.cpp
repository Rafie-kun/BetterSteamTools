#include "LocalStats.h"

#include "OSTPlatform/include/DynamicLibrary.h"
#include "Utils/Logging/Log.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace fs = std::filesystem;

namespace LocalStats {
namespace {

    constexpr uint32_t kFileMagic   = 0x4C535441u; // 'LSTA'
    constexpr uint32_t kMaxEntries  = 4096;        // sanity cap per app
    constexpr uintmax_t kMaxFileBytes = 64u * 1024u;

    std::mutex g_mutex;
    // appId -> (stat_id -> stat_value). Populated lazily from disk.
    std::unordered_map<AppId_t, std::unordered_map<uint32_t, uint32_t>> g_journal;
    std::unordered_map<AppId_t, bool> g_loaded;

    fs::path StatsDir() {
        const fs::path steamExe = OSTPlatform::DynamicLibrary::GetMainExecutablePath();
        if (steamExe.empty()) return {};
        return steamExe.parent_path() / "opensteamtool" / "stats";
    }

    fs::path StatsFile(AppId_t appId) {
        return StatsDir() / (std::to_string(appId) + ".dat");
    }

    void LoadLocked(AppId_t appId) {
        if (g_loaded.count(appId)) return;
        g_loaded[appId] = true;

        std::error_code ec;
        const fs::path path = StatsFile(appId);
        const uintmax_t size = fs::file_size(path, ec);
        if (ec || size < 8 || size > kMaxFileBytes) return;

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return;
        uint32_t magic = 0, count = 0;
        ifs.read(reinterpret_cast<char*>(&magic), 4);
        ifs.read(reinterpret_cast<char*>(&count), 4);
        if (!ifs || magic != kFileMagic || count > kMaxEntries) return;

        auto& slot = g_journal[appId];
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t id = 0, value = 0;
            ifs.read(reinterpret_cast<char*>(&id), 4);
            ifs.read(reinterpret_cast<char*>(&value), 4);
            if (!ifs) break;
            slot[id] = value;
        }
        LOG_ACHIEVEMENT_DEBUG("LocalStats: loaded {} stat(s) for app {}", slot.size(), appId);
    }

    void SaveLocked(AppId_t appId) {
        const auto it = g_journal.find(appId);
        if (it == g_journal.end()) return;

        std::error_code ec;
        fs::create_directories(StatsDir(), ec);

        // Write temp + rename so a crash never leaves a torn journal.
        const fs::path dest = StatsFile(appId);
        const fs::path tmp  = fs::path(dest).concat(".tmp");
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                LOG_ACHIEVEMENT_WARN("LocalStats: cannot open {} for app {}", tmp.string(), appId);
                return;
            }
            const uint32_t count = static_cast<uint32_t>(it->second.size());
            ofs.write(reinterpret_cast<const char*>(&kFileMagic), 4);
            ofs.write(reinterpret_cast<const char*>(&count), 4);
            for (const auto& [id, value] : it->second) {
                ofs.write(reinterpret_cast<const char*>(&id), 4);
                ofs.write(reinterpret_cast<const char*>(&value), 4);
            }
            ofs.flush();
            if (!ofs) {
                LOG_ACHIEVEMENT_WARN("LocalStats: write failed for app {}", appId);
                fs::remove(tmp, ec);
                return;
            }
        }
        fs::rename(tmp, dest, ec);
        if (ec)
            LOG_ACHIEVEMENT_WARN("LocalStats: rename failed for app {} ({})", appId, ec.message());
    }

} // namespace

uint32_t RecordStore(AppId_t appId,
                     const std::vector<std::pair<uint32_t, uint32_t>>& pairs) {
    if (!appId || pairs.empty()) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked(appId);
    auto& slot = g_journal[appId];
    uint32_t merged = 0;
    for (const auto& [id, value] : pairs) {
        if (!slot.count(id) && slot.size() >= kMaxEntries) continue;
        if (slot[id] != value) {
            slot[id] = value;
            ++merged;
        }
    }
    if (merged) SaveLocked(appId);
    LOG_ACHIEVEMENT_DEBUG("LocalStats: merged {} stat(s) for app {} ({} total)",
                          merged, appId, slot.size());
    return merged;
}

void Clear(AppId_t appId) {
    if (!appId) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_journal.erase(appId);
    g_loaded[appId] = true; // do not resurrect from disk afterwards
    std::error_code ec;
    fs::remove(StatsFile(appId), ec);
    LOG_ACHIEVEMENT_INFO("LocalStats: cleared journal for app {}", appId);
}

std::vector<std::pair<uint32_t, uint32_t>> GetStats(AppId_t appId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked(appId);
    const auto it = g_journal.find(appId);
    if (it == g_journal.end()) return {};
    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(it->second.size());
    for (const auto& [id, value] : it->second)
        out.emplace_back(id, value);
    return out;
}

} // namespace LocalStats
