#include "kernox/modules/experiment/workload.hpp"

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>

namespace kernox::experiment {

std::string_view to_string(WorkloadScenarioType type) noexcept {
    switch (type) {
        case WorkloadScenarioType::Normal:
            return "Normal";
        case WorkloadScenarioType::HighMemoryPressure:
            return "HighMemoryPressure";
        case WorkloadScenarioType::BackgroundHeavy:
            return "BackgroundHeavy";
        case WorkloadScenarioType::ForegroundProtection:
            return "ForegroundProtection";
        case WorkloadScenarioType::Mixed:
            return "Mixed";
        case WorkloadScenarioType::Custom:
            return "Custom";
    }
    return "Unknown";
}

std::optional<WorkloadScenarioType> parse_scenario_type(std::string_view str) noexcept {
    std::string lower;
    lower.reserve(str.size());
    for (char ch : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (lower == "normal") {
        return WorkloadScenarioType::Normal;
    }
    if (lower == "high-pressure" || lower == "highmemorypressure" || lower == "high_pressure" ||
        lower == "high_memory_pressure") {
        return WorkloadScenarioType::HighMemoryPressure;
    }
    if (lower == "background-heavy" || lower == "backgroundheavy" || lower == "background_heavy") {
        return WorkloadScenarioType::BackgroundHeavy;
    }
    if (lower == "foreground-protection" || lower == "foregroundprotection" ||
        lower == "foreground_protection") {
        return WorkloadScenarioType::ForegroundProtection;
    }
    if (lower == "mixed") {
        return WorkloadScenarioType::Mixed;
    }
    if (lower == "custom") {
        return WorkloadScenarioType::Custom;
    }
    return std::nullopt;
}

std::string WorkloadDefinition::compute_fingerprint() const {
    // 64-bit FNV-1a hash over all deterministic configuration parameters
    uint64_t hash = 14695981039346656037ULL;
    auto hash_bytes = [&hash](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    };

    hash_bytes(&seed, sizeof(seed));
    auto sc_val = static_cast<uint32_t>(scenario_type);
    hash_bytes(&sc_val, sizeof(sc_val));
    hash_bytes(&total_memory_bytes, sizeof(total_memory_bytes));
    size_t count = process_specs.size();
    hash_bytes(&count, sizeof(count));

    for (const auto& p : process_specs) {
        hash_bytes(p.name.data(), p.name.size());
        hash_bytes(&p.cpu_priority, sizeof(p.cpu_priority));
        auto imp = static_cast<uint32_t>(p.importance);
        hash_bytes(&imp, sizeof(imp));
        hash_bytes(&p.requested_memory_bytes, sizeof(p.requested_memory_bytes));
        hash_bytes(&p.initial_memory_bytes, sizeof(p.initial_memory_bytes));
        hash_bytes(&p.cpu_burst_ticks, sizeof(p.cpu_burst_ticks));
        hash_bytes(&p.arrival_tick, sizeof(p.arrival_tick));
        for (const auto& ev : p.dynamic_events) {
            hash_bytes(&ev.tick, sizeof(ev.tick));
            hash_bytes(&ev.delta_bytes, sizeof(ev.delta_bytes));
        }
    }

    std::ostringstream oss;
    oss << "WKL-" << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

core::Result<void> WorkloadGeneratorConfig::validate() const {
    if (total_memory_bytes == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Total simulated memory capacity must be greater than zero bytes"
        );
    }
    if (process_count == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Workload process count must be greater than zero"
        );
    }
    return core::Result<void>::ok();
}

core::Result<WorkloadDefinition> WorkloadGenerator::generate(const WorkloadGeneratorConfig& config) {
    auto val_res = config.validate();
    if (val_res.is_err()) {
        return core::Result<WorkloadDefinition>::err(val_res.error());
    }

    WorkloadDefinition def;
    def.scenario_name = std::string(to_string(config.scenario));
    def.scenario_type = config.scenario;
    def.seed = config.seed;
    def.total_memory_bytes = config.total_memory_bytes;
    def.process_specs.reserve(config.process_count);

    std::mt19937_64 rng(config.seed);

    static const std::vector<std::string> kForegroundApps = {
        "com.android.camera", "com.google.maps", "org.videolan.vlc",
        "com.android.dialer", "com.twitter.android", "org.mozilla.firefox"
    };

    static const std::vector<std::string> kVisibleApps = {
        "com.android.systemui", "com.google.android.inputmethod",
        "com.android.settings", "com.android.calculator2", "com.android.clock"
    };

    static const std::vector<std::string> kServiceApps = {
        "com.spotify.music", "com.google.android.gms", "com.android.bluetooth",
        "com.whatsapp.backup", "com.google.android.sync", "com.podcast.player"
    };

    static const std::vector<std::string> kBackgroundApps = {
        "com.instagram.android", "com.dropbox.sync", "com.amazon.mShop",
        "com.reddit.frontpage", "com.linkedin.android", "com.pinterest.app"
    };

    static const std::vector<std::string> kCachedApps = {
        "com.weather.forecast", "com.news.feed", "com.game.arcade",
        "com.recipe.book", "com.sports.scores", "com.trivia.quiz"
    };

    for (size_t i = 0; i < config.process_count; ++i) {
        ProcessWorkloadSpec spec;
        uint64_t arrival = 0;
        uint64_t burst = 50;
        uint32_t prio = 20;
        intel::ProcessImportance importance = intel::ProcessImportance::Background;
        uint64_t req_mem = 64ULL * 1024ULL * 1024ULL;
        uint64_t init_mem = 48ULL * 1024ULL * 1024ULL;
        std::string app_name;

        switch (config.scenario) {
            case WorkloadScenarioType::Normal: {
                // Mix: ~15% FG, ~15% Vis, ~25% Svc, ~25% BG, ~20% Cached
                double r = std::generate_canonical<double, 53>(rng);
                if (r < 0.15) {
                    importance = intel::ProcessImportance::Foreground;
                    app_name = kForegroundApps[i % kForegroundApps.size()];
                    prio = static_cast<uint32_t>(rng() % 15); // High CPU urgency 0-14
                    init_mem = (50ULL + (rng() % 30)) * 1024ULL * 1024ULL;
                } else if (r < 0.30) {
                    importance = intel::ProcessImportance::Visible;
                    app_name = kVisibleApps[i % kVisibleApps.size()];
                    prio = static_cast<uint32_t>(15 + (rng() % 15));
                    init_mem = (40ULL + (rng() % 25)) * 1024ULL * 1024ULL;
                } else if (r < 0.55) {
                    importance = intel::ProcessImportance::Service;
                    app_name = kServiceApps[i % kServiceApps.size()];
                    prio = static_cast<uint32_t>(30 + (rng() % 20));
                    init_mem = (45ULL + (rng() % 35)) * 1024ULL * 1024ULL;
                } else if (r < 0.80) {
                    importance = intel::ProcessImportance::Background;
                    app_name = kBackgroundApps[i % kBackgroundApps.size()];
                    prio = static_cast<uint32_t>(50 + (rng() % 25));
                    init_mem = (35ULL + (rng() % 30)) * 1024ULL * 1024ULL;
                } else {
                    importance = intel::ProcessImportance::Cached;
                    app_name = kCachedApps[i % kCachedApps.size()];
                    prio = static_cast<uint32_t>(75 + (rng() % 24));
                    init_mem = (25ULL + (rng() % 25)) * 1024ULL * 1024ULL;
                }
                arrival = (i % 3) * 5ULL;
                burst = 40ULL + (rng() % 60);
                req_mem = init_mem + (20ULL * 1024ULL * 1024ULL);
                break;
            }

            case WorkloadScenarioType::HighMemoryPressure: {
                // Heavy memory allocations driving system into Critical pressure (>90%)
                double r = std::generate_canonical<double, 53>(rng);
                if (r < 0.20) {
                    importance = intel::ProcessImportance::Foreground;
                    app_name = kForegroundApps[i % kForegroundApps.size()];
                    prio = static_cast<uint32_t>(rng() % 20);
                } else if (r < 0.40) {
                    importance = intel::ProcessImportance::Service;
                    app_name = kServiceApps[i % kServiceApps.size()];
                    prio = static_cast<uint32_t>(20 + (rng() % 25));
                } else if (r < 0.70) {
                    importance = intel::ProcessImportance::Background;
                    app_name = kBackgroundApps[i % kBackgroundApps.size()];
                    prio = static_cast<uint32_t>(45 + (rng() % 30));
                } else {
                    importance = intel::ProcessImportance::Cached;
                    app_name = kCachedApps[i % kCachedApps.size()];
                    prio = static_cast<uint32_t>(75 + (rng() % 24));
                }

                // Generous initial memory footprints (90 MiB to 180 MiB)
                init_mem = (90ULL + (rng() % 90)) * 1024ULL * 1024ULL;
                req_mem = init_mem + (60ULL * 1024ULL * 1024ULL);
                arrival = (i / 2) * 5ULL;
                burst = 30ULL + (rng() % 50);

                // Add aggressive dynamic expansion demands
                spec.dynamic_events.push_back(MemoryDemandEvent{
                    .tick = arrival + 10ULL,
                    .delta_bytes = static_cast<int64_t>((30ULL + (rng() % 40)) * 1024ULL * 1024ULL),
                    .reason = "High memory demand burst 1"
                });
                spec.dynamic_events.push_back(MemoryDemandEvent{
                    .tick = arrival + 20ULL,
                    .delta_bytes = static_cast<int64_t>((20ULL + (rng() % 30)) * 1024ULL * 1024ULL),
                    .reason = "High memory demand burst 2"
                });
                break;
            }

            case WorkloadScenarioType::BackgroundHeavy: {
                // Heavily skewed toward Cached (~40%) and Background (~40%)
                double r = std::generate_canonical<double, 53>(rng);
                if (i == 0) {
                    importance = intel::ProcessImportance::Foreground;
                    app_name = "com.android.launcher3";
                    prio = 5;
                    init_mem = 80ULL * 1024ULL * 1024ULL;
                } else if (i == 1) {
                    importance = intel::ProcessImportance::Visible;
                    app_name = "com.android.systemui";
                    prio = 10;
                    init_mem = 60ULL * 1024ULL * 1024ULL;
                } else if (r < 0.15) {
                    importance = intel::ProcessImportance::Service;
                    app_name = kServiceApps[i % kServiceApps.size()];
                    prio = static_cast<uint32_t>(30 + (rng() % 20));
                    init_mem = (50ULL + (rng() % 30)) * 1024ULL * 1024ULL;
                } else if (r < 0.55) {
                    importance = intel::ProcessImportance::Background;
                    app_name = kBackgroundApps[i % kBackgroundApps.size()];
                    prio = static_cast<uint32_t>(50 + (rng() % 25));
                    init_mem = (40ULL + (rng() % 40)) * 1024ULL * 1024ULL;
                } else {
                    importance = intel::ProcessImportance::Cached;
                    app_name = kCachedApps[i % kCachedApps.size()];
                    prio = static_cast<uint32_t>(75 + (rng() % 24));
                    init_mem = (30ULL + (rng() % 35)) * 1024ULL * 1024ULL;
                }
                arrival = (i % 4) * 2ULL;
                burst = 20ULL + (rng() % 40);
                req_mem = init_mem + (15ULL * 1024ULL * 1024ULL);
                break;
            }

            case WorkloadScenarioType::ForegroundProtection: {
                // Significant Foreground presence (30%) + Visible (20%) under memory shortfall
                double r = std::generate_canonical<double, 53>(rng);
                if (r < 0.30) {
                    importance = intel::ProcessImportance::Foreground;
                    app_name = kForegroundApps[i % kForegroundApps.size()];
                    prio = static_cast<uint32_t>(rng() % 15);
                    init_mem = (100ULL + (rng() % 50)) * 1024ULL * 1024ULL;
                } else if (r < 0.50) {
                    importance = intel::ProcessImportance::Visible;
                    app_name = kVisibleApps[i % kVisibleApps.size()];
                    prio = static_cast<uint32_t>(15 + (rng() % 20));
                    init_mem = (70ULL + (rng() % 40)) * 1024ULL * 1024ULL;
                } else if (r < 0.70) {
                    importance = intel::ProcessImportance::Service;
                    app_name = kServiceApps[i % kServiceApps.size()];
                    prio = static_cast<uint32_t>(35 + (rng() % 20));
                    init_mem = (60ULL + (rng() % 30)) * 1024ULL * 1024ULL;
                } else if (r < 0.90) {
                    importance = intel::ProcessImportance::Background;
                    app_name = kBackgroundApps[i % kBackgroundApps.size()];
                    prio = static_cast<uint32_t>(55 + (rng() % 25));
                    init_mem = (50ULL + (rng() % 30)) * 1024ULL * 1024ULL;
                } else {
                    importance = intel::ProcessImportance::Cached;
                    app_name = kCachedApps[i % kCachedApps.size()];
                    prio = static_cast<uint32_t>(80 + (rng() % 19));
                    init_mem = (40ULL + (rng() % 30)) * 1024ULL * 1024ULL;
                }
                arrival = (i % 3) * 5ULL;
                burst = 50ULL + (rng() % 50);
                req_mem = init_mem + (30ULL * 1024ULL * 1024ULL);

                // Additional dynamic memory demand
                spec.dynamic_events.push_back(MemoryDemandEvent{
                    .tick = arrival + 15ULL,
                    .delta_bytes = static_cast<int64_t>((25ULL + (rng() % 25)) * 1024ULL * 1024ULL),
                    .reason = "Foreground interaction memory surge"
                });
                break;
            }

            case WorkloadScenarioType::Mixed:
            case WorkloadScenarioType::Custom: {
                // Varied priority, memory size, and voluntary releases
                double r = std::generate_canonical<double, 53>(rng);
                if (r < 0.20) {
                    importance = intel::ProcessImportance::Foreground;
                    app_name = kForegroundApps[i % kForegroundApps.size()];
                } else if (r < 0.35) {
                    importance = intel::ProcessImportance::Visible;
                    app_name = kVisibleApps[i % kVisibleApps.size()];
                } else if (r < 0.55) {
                    importance = intel::ProcessImportance::Service;
                    app_name = kServiceApps[i % kServiceApps.size()];
                } else if (r < 0.80) {
                    importance = intel::ProcessImportance::Background;
                    app_name = kBackgroundApps[i % kBackgroundApps.size()];
                } else {
                    importance = intel::ProcessImportance::Cached;
                    app_name = kCachedApps[i % kCachedApps.size()];
                }
                prio = static_cast<uint32_t>(rng() % 100);
                init_mem = (30ULL + (rng() % 120)) * 1024ULL * 1024ULL;
                req_mem = init_mem + (40ULL * 1024ULL * 1024ULL);
                arrival = (i % 5) * 5ULL;
                burst = 20ULL + (rng() % 80);

                if (i % 2 == 0) {
                    spec.dynamic_events.push_back(MemoryDemandEvent{
                        .tick = arrival + 10ULL,
                        .delta_bytes = static_cast<int64_t>(20ULL * 1024ULL * 1024ULL),
                        .reason = "Working set expansion"
                    });
                } else {
                    spec.dynamic_events.push_back(MemoryDemandEvent{
                        .tick = arrival + 15ULL,
                        .delta_bytes = -static_cast<int64_t>(10ULL * 1024ULL * 1024ULL),
                        .reason = "Voluntary cache release"
                    });
                }
                break;
            }
        }

        spec.name = app_name + "_" + std::to_string(i + 1);
        spec.cpu_priority = prio;
        spec.importance = importance;
        spec.requested_memory_bytes = req_mem;
        spec.initial_memory_bytes = init_mem;
        spec.cpu_burst_ticks = burst;
        spec.arrival_tick = arrival;

        def.process_specs.push_back(std::move(spec));
    }

    def.fingerprint = def.compute_fingerprint();
    return core::Result<WorkloadDefinition>::ok(std::move(def));
}

core::Result<WorkloadDefinition> WorkloadGenerator::generate_scenario(
    WorkloadScenarioType scenario,
    uint64_t seed,
    size_t process_count,
    uint64_t total_memory_bytes
) {
    WorkloadGeneratorConfig cfg{scenario, seed, process_count, total_memory_bytes};
    return generate(cfg);
}

} // namespace kernox::experiment
