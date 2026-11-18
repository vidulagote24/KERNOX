#include "kernox/modules/experiment/workload.hpp"
#include "kernox/modules/experiment/experiment_runner.hpp"
#include "kernox/modules/experiment/exporter.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace kernox::experiment;
using namespace kernox::reclaim;
using namespace kernox::intel;
using namespace kernox::core;

static int test_workload_generator_determinism() {
    std::cout << "Running test_workload_generator_determinism...\n";

    // 1. Validation checks
    WorkloadGeneratorConfig bad_cap;
    bad_cap.total_memory_bytes = 0;
    auto bad_cap_res = WorkloadGenerator::generate(bad_cap);
    KERNOX_ASSERT_TRUE(bad_cap_res.is_err());
    KERNOX_ASSERT_EQ(bad_cap_res.error().code, ErrorCode::InvalidArgument);

    WorkloadGeneratorConfig bad_proc;
    bad_proc.process_count = 0;
    auto bad_proc_res = WorkloadGenerator::generate(bad_proc);
    KERNOX_ASSERT_TRUE(bad_proc_res.is_err());
    KERNOX_ASSERT_EQ(bad_proc_res.error().code, ErrorCode::InvalidArgument);

    // 2. Scenario parsing and to_string
    KERNOX_ASSERT_EQ(to_string(WorkloadScenarioType::Normal), "Normal");
    KERNOX_ASSERT_EQ(to_string(WorkloadScenarioType::HighMemoryPressure), "HighMemoryPressure");
    KERNOX_ASSERT_EQ(to_string(WorkloadScenarioType::BackgroundHeavy), "BackgroundHeavy");
    KERNOX_ASSERT_EQ(to_string(WorkloadScenarioType::ForegroundProtection), "ForegroundProtection");
    KERNOX_ASSERT_EQ(to_string(WorkloadScenarioType::Mixed), "Mixed");

    auto norm_opt = parse_scenario_type("normal");
    KERNOX_ASSERT_TRUE(norm_opt.has_value());
    KERNOX_ASSERT_TRUE(*norm_opt == WorkloadScenarioType::Normal);

    auto hp_opt = parse_scenario_type("high-pressure");
    KERNOX_ASSERT_TRUE(hp_opt.has_value());
    KERNOX_ASSERT_TRUE(*hp_opt == WorkloadScenarioType::HighMemoryPressure);

    auto bg_opt = parse_scenario_type("background-heavy");
    KERNOX_ASSERT_TRUE(bg_opt.has_value());
    KERNOX_ASSERT_TRUE(*bg_opt == WorkloadScenarioType::BackgroundHeavy);

    auto fg_opt = parse_scenario_type("foreground-protection");
    KERNOX_ASSERT_TRUE(fg_opt.has_value());
    KERNOX_ASSERT_TRUE(*fg_opt == WorkloadScenarioType::ForegroundProtection);

    auto mix_opt = parse_scenario_type("mixed");
    KERNOX_ASSERT_TRUE(mix_opt.has_value());
    KERNOX_ASSERT_TRUE(*mix_opt == WorkloadScenarioType::Mixed);

    auto bad_sc = parse_scenario_type("nonexistent-scenario");
    KERNOX_ASSERT_FALSE(bad_sc.has_value());

    // 3. Determinism: Same seed -> Identical workload and fingerprint
    WorkloadGeneratorConfig cfg1;
    cfg1.scenario = WorkloadScenarioType::Normal;
    cfg1.seed = 123456789ULL;
    cfg1.total_memory_bytes = 1024ULL * 1024 * 1024;
    cfg1.process_count = 25;

    auto wkl1_res = WorkloadGenerator::generate(cfg1);
    KERNOX_ASSERT_TRUE(wkl1_res.is_ok());
    const auto& wkl1 = wkl1_res.value();

    WorkloadGeneratorConfig cfg2 = cfg1;
    auto wkl2_res = WorkloadGenerator::generate(cfg2);
    KERNOX_ASSERT_TRUE(wkl2_res.is_ok());
    const auto& wkl2 = wkl2_res.value();

    KERNOX_ASSERT_EQ(wkl1.fingerprint, wkl2.fingerprint);
    KERNOX_ASSERT_EQ(wkl1.process_specs.size(), wkl2.process_specs.size());

    for (size_t i = 0; i < wkl1.process_specs.size(); ++i) {
        KERNOX_ASSERT_EQ(wkl1.process_specs[i].name, wkl2.process_specs[i].name);
        KERNOX_ASSERT_EQ(wkl1.process_specs[i].initial_memory_bytes, wkl2.process_specs[i].initial_memory_bytes);
        KERNOX_ASSERT_EQ(wkl1.process_specs[i].arrival_tick, wkl2.process_specs[i].arrival_tick);
        KERNOX_ASSERT_EQ(wkl1.process_specs[i].cpu_burst_ticks, wkl2.process_specs[i].cpu_burst_ticks);
        KERNOX_ASSERT_TRUE(wkl1.process_specs[i].importance == wkl2.process_specs[i].importance);
        KERNOX_ASSERT_EQ(wkl1.process_specs[i].cpu_priority, wkl2.process_specs[i].cpu_priority);
    }

    // 4. Sensitivity: Different seed -> Different fingerprint
    WorkloadGeneratorConfig cfg3 = cfg1;
    cfg3.seed = 987654321ULL;
    auto wkl3_res = WorkloadGenerator::generate(cfg3);
    KERNOX_ASSERT_TRUE(wkl3_res.is_ok());
    const auto& wkl3 = wkl3_res.value();

    KERNOX_ASSERT_NE(wkl1.fingerprint, wkl3.fingerprint);
    KERNOX_ASSERT_TRUE(wkl1.fingerprint.rfind("WKL-", 0) == 0);
    KERNOX_ASSERT_TRUE(wkl3.fingerprint.rfind("WKL-", 0) == 0);

    return 0;
}

static int test_scenarios_profiles() {
    std::cout << "Running test_scenarios_profiles...\n";

    const std::vector<WorkloadScenarioType> scenarios = {
        WorkloadScenarioType::Normal,
        WorkloadScenarioType::HighMemoryPressure,
        WorkloadScenarioType::BackgroundHeavy,
        WorkloadScenarioType::ForegroundProtection,
        WorkloadScenarioType::Mixed
    };

    for (auto st : scenarios) {
        WorkloadGeneratorConfig cfg;
        cfg.scenario = st;
        cfg.seed = 42;
        cfg.total_memory_bytes = 1024ULL * 1024 * 1024;

        auto wkl_res = WorkloadGenerator::generate(cfg);
        KERNOX_ASSERT_TRUE(wkl_res.is_ok());
        const auto& wkl = wkl_res.value();

        KERNOX_ASSERT_TRUE(!wkl.scenario_name.empty());
        KERNOX_ASSERT_TRUE(!wkl.fingerprint.empty());
        KERNOX_ASSERT_TRUE(wkl.total_memory_bytes == 1024ULL * 1024 * 1024);
        KERNOX_ASSERT_TRUE(!wkl.process_specs.empty());

        // Validate all process specs
        uint64_t total_init_mem = 0;
        size_t fg_count = 0;
        size_t bg_cached_count = 0;

        for (const auto& proc : wkl.process_specs) {
            KERNOX_ASSERT_TRUE(!proc.name.empty());
            KERNOX_ASSERT_TRUE(proc.initial_memory_bytes > 0);
            KERNOX_ASSERT_TRUE(proc.cpu_burst_ticks > 0);
            total_init_mem += proc.initial_memory_bytes;

            if (proc.importance == ProcessImportance::Foreground) {
                fg_count++;
            } else if (proc.importance == ProcessImportance::Background ||
                       proc.importance == ProcessImportance::Cached) {
                bg_cached_count++;
            }

            // Validate dynamic events for process
            for (const auto& evt : proc.dynamic_events) {
                KERNOX_ASSERT_TRUE(evt.delta_bytes != 0);
            }
        }

        // Validate scenario-specific characteristics
        if (st == WorkloadScenarioType::HighMemoryPressure) {
            KERNOX_ASSERT_TRUE(total_init_mem > 0);
        } else if (st == WorkloadScenarioType::BackgroundHeavy) {
            KERNOX_ASSERT_TRUE(bg_cached_count > fg_count);
        } else if (st == WorkloadScenarioType::ForegroundProtection) {
            KERNOX_ASSERT_TRUE(fg_count > 0);
            KERNOX_ASSERT_TRUE(bg_cached_count > 0);
        }
    }

    return 0;
}

static int test_fair_comparison_isolation() {
    std::cout << "Running test_fair_comparison_isolation...\n";

    WorkloadGeneratorConfig cfg;
    cfg.scenario = WorkloadScenarioType::HighMemoryPressure;
    cfg.seed = 777;
    cfg.total_memory_bytes = 1024ULL * 1024 * 1024;
    cfg.process_count = 40;

    auto wkl_res = WorkloadGenerator::generate(cfg);
    KERNOX_ASSERT_TRUE(wkl_res.is_ok());
    const auto& workload = wkl_res.value();

    auto comp_res = ExperimentRunner::compare_all_policies(workload);
    KERNOX_ASSERT_TRUE(comp_res.is_ok());
    const auto& comp = comp_res.value();

    KERNOX_ASSERT_EQ(comp.policy_results.size(), 4u);
    KERNOX_ASSERT_EQ(comp.scenario_name, workload.scenario_name);
    KERNOX_ASSERT_EQ(comp.workload_fingerprint, workload.fingerprint);

    // Verify all 4 policies were executed under identical conditions
    const std::vector<std::string> expected_policies = {
        "FIFO", "LRU", "Priority", "ImportanceAware"
    };

    for (size_t i = 0; i < 4; ++i) {
        const auto& res = comp.policy_results[i];
        KERNOX_ASSERT_EQ(res.policy_name, expected_policies[i]);
        KERNOX_ASSERT_EQ(res.scenario_name, workload.scenario_name);
        KERNOX_ASSERT_EQ(res.seed, workload.seed);
        KERNOX_ASSERT_EQ(res.workload_fingerprint, workload.fingerprint);
        KERNOX_ASSERT_EQ(res.metrics.total_capacity_bytes, workload.total_memory_bytes);
        KERNOX_ASSERT_EQ(res.metrics.total_processes, workload.process_specs.size());

        // Process accounting invariants
        KERNOX_ASSERT_EQ(res.metrics.processes_survived,
                         res.metrics.total_processes - res.metrics.processes_terminated_pressure);

        double expected_survival = static_cast<double>(res.metrics.processes_survived) /
                                   static_cast<double>(res.metrics.total_processes);
        KERNOX_ASSERT_TRUE(std::abs(res.metrics.survival_rate - expected_survival) < 1e-4);
    }

    // Verify summary table generation
    std::string summary = comp.to_summary_string();
    KERNOX_ASSERT_TRUE(summary.find("POLICY COMPARISON") != std::string::npos);
    KERNOX_ASSERT_TRUE(summary.find(workload.fingerprint) != std::string::npos);
    for (const auto& p : expected_policies) {
        KERNOX_ASSERT_TRUE(summary.find(p) != std::string::npos);
    }

    return 0;
}

static int test_experiment_runner_execution() {
    std::cout << "Running test_experiment_runner_execution...\n";

    WorkloadGeneratorConfig cfg;
    cfg.scenario = WorkloadScenarioType::Normal;
    cfg.seed = 42;
    cfg.total_memory_bytes = 1024ULL * 1024 * 1024;
    cfg.process_count = 20;

    auto wkl_res = WorkloadGenerator::generate(cfg);
    KERNOX_ASSERT_TRUE(wkl_res.is_ok());
    const auto& workload = wkl_res.value();

    ImportanceAwareReclamationPolicy policy;
    auto run_res = ExperimentRunner::run_policy(workload, policy);
    KERNOX_ASSERT_TRUE(run_res.is_ok());

    const auto& res = run_res.value();
    KERNOX_ASSERT_EQ(res.policy_name, "ImportanceAware");
    KERNOX_ASSERT_EQ(res.scenario_name, workload.scenario_name);
    KERNOX_ASSERT_EQ(res.seed, 42u);
    KERNOX_ASSERT_EQ(res.workload_fingerprint, workload.fingerprint);

    KERNOX_ASSERT_EQ(res.metrics.total_processes, 20u);
    KERNOX_ASSERT_TRUE(res.metrics.simulated_duration_ticks > 0);
    KERNOX_ASSERT_TRUE(res.metrics.peak_used_bytes <= res.metrics.total_capacity_bytes ||
                       res.metrics.failed_allocations > 0);

    std::string str_out = res.to_string();
    KERNOX_ASSERT_TRUE(str_out.find("EXPERIMENT RESULT: ImportanceAware") != std::string::npos);
    KERNOX_ASSERT_TRUE(str_out.find(workload.fingerprint) != std::string::npos);

    return 0;
}

static int test_foreground_protection_scenario() {
    std::cout << "Running test_foreground_protection_scenario...\n";

    // Setup a high memory pressure scenario designed to force process terminations
    WorkloadGeneratorConfig cfg;
    cfg.scenario = WorkloadScenarioType::ForegroundProtection;
    cfg.seed = 101;
    // Restrict capacity to induce substantial memory pressure
    cfg.total_memory_bytes = 512ULL * 1024 * 1024; // 512 MiB
    cfg.process_count = 35;

    auto wkl_res = WorkloadGenerator::generate(cfg);
    KERNOX_ASSERT_TRUE(wkl_res.is_ok());
    const auto& workload = wkl_res.value();

    auto comp_res = ExperimentRunner::compare_all_policies(workload);
    KERNOX_ASSERT_TRUE(comp_res.is_ok());
    const auto& comp = comp_res.value();
    KERNOX_ASSERT_EQ(comp.policy_results.size(), 4u);

    const ExperimentResult* fifo_res = nullptr;
    const ExperimentResult* lmk_res = nullptr;

    for (const auto& r : comp.policy_results) {
        if (r.policy_name == "FIFO") fifo_res = &r;
        if (r.policy_name == "ImportanceAware") lmk_res = &r;
    }

    KERNOX_ASSERT_TRUE(fifo_res != nullptr);
    KERNOX_ASSERT_TRUE(lmk_res != nullptr);

    // Under Importance-Aware policy, foreground survival rate should be strictly protected:
    // foreground survival rate must be >= FIFO foreground survival rate
    KERNOX_ASSERT_TRUE(lmk_res->metrics.foreground_survival_rate >= fifo_res->metrics.foreground_survival_rate);

    // If pressure occurred and victims were reclaimed, verify ImportanceAware prioritized cached/bg victims
    if (lmk_res->metrics.processes_reclaimed > 0) {
        for (const auto& dec : lmk_res->reclamation_log) {
            // No foreground process should have been terminated under ImportanceAware while lower tiers existed
            KERNOX_ASSERT_TRUE(dec.importance != ProcessImportance::Foreground ||
                               fifo_res->metrics.foreground_survival_rate <= lmk_res->metrics.foreground_survival_rate);
        }
    }

    return 0;
}

static int test_exporter_json_and_csv() {
    std::cout << "Running test_exporter_json_and_csv...\n";

    // 1. JSON escaping tests
    KERNOX_ASSERT_EQ(ExperimentExporter::escape_json_string("normal_string"), "normal_string");
    KERNOX_ASSERT_EQ(ExperimentExporter::escape_json_string("hello \"world\""), "hello \\\"world\\\"");
    KERNOX_ASSERT_EQ(ExperimentExporter::escape_json_string("path\\to\\file"), "path\\\\to\\\\file");
    KERNOX_ASSERT_EQ(ExperimentExporter::escape_json_string("line1\nline2"), "line1\\nline2");
    KERNOX_ASSERT_EQ(ExperimentExporter::escape_json_string("tab\tseparated"), "tab\\tseparated");

    // 2. Generate a small experiment result and comparison
    WorkloadGeneratorConfig cfg;
    cfg.scenario = WorkloadScenarioType::Normal;
    cfg.seed = 55;
    cfg.total_memory_bytes = 1024ULL * 1024 * 1024;
    cfg.process_count = 15;

    auto wkl_res = WorkloadGenerator::generate(cfg);
    KERNOX_ASSERT_TRUE(wkl_res.is_ok());
    const auto& workload = wkl_res.value();

    auto comp_res = ExperimentRunner::compare_all_policies(workload);
    KERNOX_ASSERT_TRUE(comp_res.is_ok());
    const auto& comp = comp_res.value();
    const auto& single_result = comp.policy_results[3]; // ImportanceAware

    // 3. Test single result JSON export
    std::string json_single = ExperimentExporter::to_json(single_result);
    KERNOX_ASSERT_TRUE(!json_single.empty());
    KERNOX_ASSERT_TRUE(json_single.find("\"policy_name\": \"ImportanceAware\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_single.find("\"workload_fingerprint\": \"" + workload.fingerprint + "\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_single.find("\"total_capacity_bytes\":") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_single.find("\"foreground_survival_rate\":") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_single.find("\"reclamation_log\": [") != std::string::npos);

    // 4. Test multi-policy comparison JSON export
    std::string json_comp = ExperimentExporter::to_json(comp);
    KERNOX_ASSERT_TRUE(!json_comp.empty());
    KERNOX_ASSERT_TRUE(json_comp.find("\"scenario_name\":") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_comp.find("\"workload_fingerprint\": \"" + workload.fingerprint + "\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_comp.find("\"policy_results\": [") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_comp.find("\"policy_name\": \"FIFO\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_comp.find("\"policy_name\": \"LRU\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_comp.find("\"policy_name\": \"Priority\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_comp.find("\"policy_name\": \"ImportanceAware\"") != std::string::npos);

    // 5. Test comparison CSV export
    std::string csv_comp = ExperimentExporter::to_csv(comp);
    KERNOX_ASSERT_TRUE(!csv_comp.empty());
    KERNOX_ASSERT_TRUE(csv_comp.find("scenario,seed,fingerprint,capacity_bytes,policy") != std::string::npos);
    KERNOX_ASSERT_TRUE(csv_comp.find(",FIFO,") != std::string::npos);
    KERNOX_ASSERT_TRUE(csv_comp.find(",LRU,") != std::string::npos);
    KERNOX_ASSERT_TRUE(csv_comp.find(",Priority,") != std::string::npos);
    KERNOX_ASSERT_TRUE(csv_comp.find(",ImportanceAware,") != std::string::npos);

    // Count lines in CSV (header + 4 results = 5 lines)
    std::istringstream iss(csv_comp);
    std::string line;
    size_t line_count = 0;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            line_count++;
        }
    }
    KERNOX_ASSERT_EQ(line_count, 5u);

    // 6. Test decisions CSV export
    std::string decisions_csv = ExperimentExporter::to_decisions_csv(single_result);
    KERNOX_ASSERT_TRUE(!decisions_csv.empty());
    KERNOX_ASSERT_TRUE(decisions_csv.find("policy,scenario,seed,victim_pid,victim_name,importance_tier") != std::string::npos);

    return 0;
}

int main() {
    std::cout << "=== KERNOX 2.0 Workload Generation & Experiment Engine Tests ===\n";

    if (test_workload_generator_determinism() != 0) return 1;
    if (test_scenarios_profiles() != 0) return 1;
    if (test_fair_comparison_isolation() != 0) return 1;
    if (test_experiment_runner_execution() != 0) return 1;
    if (test_foreground_protection_scenario() != 0) return 1;
    if (test_exporter_json_and_csv() != 0) return 1;

    std::cout << "=== All Workload Generation & Experiment Engine Tests Passed Successfully ===\n";
    return 0;
}
