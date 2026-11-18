#include "kernox/modules/simulator/custom_process.hpp"
#include "kernox/modules/simulator/workload_builder.hpp"
#include "kernox/modules/simulator/terminal_reader.hpp"
#include "kernox/modules/simulator/interactive_menu.hpp"
#include "kernox/modules/simulator/analytics_bridge.hpp"
#include "kernox/modules/adb/bridge.hpp"
#include "kernox/modules/experiment/experiment_runner.hpp"
#include "kernox/modules/experiment/exporter.hpp"
#include "kernox/modules/reclaim/reclamation_policy.hpp"
#include "kernox/cli/command.hpp"
#include "test_harness.hpp"

#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace kernox::simulator;
using namespace kernox::experiment;
using namespace kernox::reclaim;
using namespace kernox::intel;
using namespace kernox::core;
using namespace kernox::cli;

// =============================================================================
// Phase 1 Tests: Validation & Workload Builder
// =============================================================================

static int test_device_config_validation() {
    std::cout << "Running test_device_config_validation...\n";

    // 1. Default configuration is valid
    CustomDeviceConfiguration valid_cfg;
    auto res = validate_device_config(valid_cfg);
    KERNOX_ASSERT_TRUE(res.is_ok());

    // 2. RAM capacity = 0 fails
    CustomDeviceConfiguration zero_ram{.total_ram_mb = 0};
    auto zero_res = validate_device_config(zero_ram);
    KERNOX_ASSERT_TRUE(zero_res.is_err());
    KERNOX_ASSERT_EQ(zero_res.error().code, ErrorCode::InvalidArgument);

    // 3. Moderate threshold <= 0 fails
    CustomDeviceConfiguration mod_low{.moderate_threshold_ratio = 0.0};
    KERNOX_ASSERT_TRUE(validate_device_config(mod_low).is_err());

    // 4. Moderate threshold >= 1.0 fails
    CustomDeviceConfiguration mod_high{.moderate_threshold_ratio = 1.0};
    KERNOX_ASSERT_TRUE(validate_device_config(mod_high).is_err());

    // 5. Critical threshold <= 0 fails
    CustomDeviceConfiguration crit_low{.critical_threshold_ratio = 0.0};
    KERNOX_ASSERT_TRUE(validate_device_config(crit_low).is_err());

    // 6. Critical threshold > 1.0 fails
    CustomDeviceConfiguration crit_high{.critical_threshold_ratio = 1.05};
    KERNOX_ASSERT_TRUE(validate_device_config(crit_high).is_err());

    // 7. Moderate >= Critical fails
    CustomDeviceConfiguration inverted{.moderate_threshold_ratio = 0.85, .critical_threshold_ratio = 0.80};
    KERNOX_ASSERT_TRUE(validate_device_config(inverted).is_err());

    CustomDeviceConfiguration equal_thresh{.moderate_threshold_ratio = 0.80, .critical_threshold_ratio = 0.80};
    KERNOX_ASSERT_TRUE(validate_device_config(equal_thresh).is_err());

    // 8. Conversion to PressureThresholds in bytes
    CustomDeviceConfiguration test_dev{.total_ram_mb = 4096, .moderate_threshold_ratio = 0.70, .critical_threshold_ratio = 0.85};
    auto thresh = test_dev.to_pressure_thresholds();
    KERNOX_ASSERT_EQ(thresh.total_memory_bytes, 4096ULL * 1024ULL * 1024ULL);
    KERNOX_ASSERT_EQ(thresh.moderate_threshold_ratio, 0.70);
    KERNOX_ASSERT_EQ(thresh.critical_threshold_ratio, 0.85);

    return 0;
}

static int test_process_input_validation() {
    std::cout << "Running test_process_input_validation...\n";

    // 1. Valid process
    CustomProcessInput valid_proc{
        .name = "com.android.chrome",
        .cpu_priority = 20,
        .importance = ProcessImportance::Foreground,
        .initial_memory_mb = 256,
        .requested_memory_mb = 512,
        .cpu_burst_ticks = 40,
        .arrival_tick = 0,
        .dynamic_events = {}
    };
    KERNOX_ASSERT_TRUE(validate_process_input(valid_proc).is_ok());

    // 2. Empty process name fails
    CustomProcessInput empty_name = valid_proc;
    empty_name.name = "";
    KERNOX_ASSERT_TRUE(validate_process_input(empty_name).is_err());

    // 3. Whitespace-only name fails
    CustomProcessInput space_name = valid_proc;
    space_name.name = "   \t\n ";
    KERNOX_ASSERT_TRUE(validate_process_input(space_name).is_err());

    // 4. Priority bounds [0, 99]
    CustomProcessInput prio_ok_0 = valid_proc;
    prio_ok_0.cpu_priority = 0;
    KERNOX_ASSERT_TRUE(validate_process_input(prio_ok_0).is_ok());

    CustomProcessInput prio_ok_99 = valid_proc;
    prio_ok_99.cpu_priority = 99;
    KERNOX_ASSERT_TRUE(validate_process_input(prio_ok_99).is_ok());

    CustomProcessInput prio_bad = valid_proc;
    prio_bad.cpu_priority = 100;
    KERNOX_ASSERT_TRUE(validate_process_input(prio_bad).is_err());

    // 5. Invalid importance
    CustomProcessInput imp_bad = valid_proc;
    imp_bad.importance = static_cast<ProcessImportance>(999);
    KERNOX_ASSERT_TRUE(validate_process_input(imp_bad).is_err());

    // 6. Zero initial memory fails
    CustomProcessInput zero_mem = valid_proc;
    zero_mem.initial_memory_mb = 0;
    KERNOX_ASSERT_TRUE(validate_process_input(zero_mem).is_err());

    // 7. Requested memory < Initial memory fails
    CustomProcessInput bad_req_mem = valid_proc;
    bad_req_mem.initial_memory_mb = 256;
    bad_req_mem.requested_memory_mb = 128;
    KERNOX_ASSERT_TRUE(validate_process_input(bad_req_mem).is_err());

    // Requested memory == Initial memory succeeds
    CustomProcessInput eq_req_mem = valid_proc;
    eq_req_mem.initial_memory_mb = 256;
    eq_req_mem.requested_memory_mb = 256;
    KERNOX_ASSERT_TRUE(validate_process_input(eq_req_mem).is_ok());

    // 8. Zero CPU burst duration fails
    CustomProcessInput zero_burst = valid_proc;
    zero_burst.cpu_burst_ticks = 0;
    KERNOX_ASSERT_TRUE(validate_process_input(zero_burst).is_err());

    // 9. Dynamic memory event before arrival tick fails
    CustomProcessInput late_proc = valid_proc;
    late_proc.arrival_tick = 10;
    late_proc.dynamic_events.push_back(CustomMemoryEvent{
        .tick = 5,
        .delta_mb = 64,
        .reason = "Premature event"
    });
    KERNOX_ASSERT_TRUE(validate_process_input(late_proc).is_err());

    // Dynamic memory event at arrival tick or later succeeds
    late_proc.dynamic_events[0].tick = 10;
    KERNOX_ASSERT_TRUE(validate_process_input(late_proc).is_ok());

    late_proc.dynamic_events[0].tick = 15;
    KERNOX_ASSERT_TRUE(validate_process_input(late_proc).is_ok());

    return 0;
}

static int test_process_collection_validation() {
    std::cout << "Running test_process_collection_validation...\n";

    CustomDeviceConfiguration dev{.total_ram_mb = 1024};

    // 1. Empty collection fails
    std::vector<CustomProcessInput> empty_procs;
    auto empty_res = validate_process_collection(empty_procs, dev);
    KERNOX_ASSERT_TRUE(empty_res.is_err());
    KERNOX_ASSERT_EQ(empty_res.error().code, ErrorCode::InvalidArgument);

    // 2. Duplicate process names fail
    std::vector<CustomProcessInput> dup_procs = {
        CustomProcessInput{.name = "com.app.service", .initial_memory_mb = 100, .requested_memory_mb = 100, .cpu_burst_ticks = 10},
        CustomProcessInput{.name = "com.app.service", .initial_memory_mb = 200, .requested_memory_mb = 200, .cpu_burst_ticks = 10}
    };
    auto dup_res = validate_process_collection(dup_procs, dev);
    KERNOX_ASSERT_TRUE(dup_res.is_err());
    KERNOX_ASSERT_EQ(dup_res.error().code, ErrorCode::InvalidArgument);

    // 3. Single process exceeding device RAM capacity fails
    std::vector<CustomProcessInput> huge_proc = {
        CustomProcessInput{.name = "com.app.huge", .initial_memory_mb = 1025, .requested_memory_mb = 1025, .cpu_burst_ticks = 10}
    };
    auto huge_res = validate_process_collection(huge_proc, dev);
    KERNOX_ASSERT_TRUE(huge_res.is_err());
    KERNOX_ASSERT_EQ(huge_res.error().code, ErrorCode::InvalidArgument);

    // 4. Sum of initial memory at tick 0 exceeding device RAM fails
    std::vector<CustomProcessInput> tick0_overflow = {
        CustomProcessInput{.name = "com.app.one", .initial_memory_mb = 600, .requested_memory_mb = 600, .cpu_burst_ticks = 10, .arrival_tick = 0},
        CustomProcessInput{.name = "com.app.two", .initial_memory_mb = 500, .requested_memory_mb = 500, .cpu_burst_ticks = 10, .arrival_tick = 0}
    };
    auto overflow_res = validate_process_collection(tick0_overflow, dev);
    KERNOX_ASSERT_TRUE(overflow_res.is_err());
    KERNOX_ASSERT_EQ(overflow_res.error().code, ErrorCode::InvalidArgument);

    // 5. Valid collection with different arrival ticks fitting in memory succeeds
    std::vector<CustomProcessInput> valid_procs = {
        CustomProcessInput{.name = "com.app.one", .initial_memory_mb = 400, .requested_memory_mb = 400, .cpu_burst_ticks = 10, .arrival_tick = 0},
        CustomProcessInput{.name = "com.app.two", .initial_memory_mb = 300, .requested_memory_mb = 300, .cpu_burst_ticks = 10, .arrival_tick = 0},
        CustomProcessInput{.name = "com.app.three", .initial_memory_mb = 400, .requested_memory_mb = 400, .cpu_burst_ticks = 10, .arrival_tick = 20}
    };
    auto ok_res = validate_process_collection(valid_procs, dev);
    KERNOX_ASSERT_TRUE(ok_res.is_ok());

    return 0;
}

static int test_workload_builder_deterministic() {
    std::cout << "Running test_workload_builder_deterministic...\n";

    CustomDeviceConfiguration dev{.total_ram_mb = 2048};
    CustomProcessInput p1{
        .name = "com.android.chrome",
        .cpu_priority = 10,
        .importance = ProcessImportance::Foreground,
        .initial_memory_mb = 256,
        .requested_memory_mb = 512,
        .cpu_burst_ticks = 30,
        .arrival_tick = 0,
        .dynamic_events = {
            CustomMemoryEvent{.tick = 10, .delta_mb = 128, .reason = "Tab load"}
        }
    };
    CustomProcessInput p2{
        .name = "com.spotify.music",
        .cpu_priority = 30,
        .importance = ProcessImportance::Service,
        .initial_memory_mb = 128,
        .requested_memory_mb = 128,
        .cpu_burst_ticks = 40,
        .arrival_tick = 5,
        .dynamic_events = {}
    };

    // 1. Fluent builder
    CustomWorkloadBuilder builder;
    builder.set_device_config(dev)
           .set_scenario_name("InteractiveDemo")
           .set_seed(12345)
           .add_process(p1)
           .add_process(p2);

    auto build_res = builder.build();
    KERNOX_ASSERT_TRUE(build_res.is_ok());
    const auto& wkl = build_res.value();

    KERNOX_ASSERT_EQ(wkl.scenario_name, "InteractiveDemo");
    KERNOX_ASSERT_TRUE(wkl.scenario_type == WorkloadScenarioType::Custom);
    KERNOX_ASSERT_EQ(wkl.seed, 12345ULL);
    KERNOX_ASSERT_EQ(wkl.total_memory_bytes, 2048ULL * 1024ULL * 1024ULL);
    KERNOX_ASSERT_EQ(wkl.process_specs.size(), 2ULL);

    // Verify process 1 translation
    const auto& spec1 = wkl.process_specs[0];
    KERNOX_ASSERT_EQ(spec1.name, "com.android.chrome");
    KERNOX_ASSERT_EQ(spec1.cpu_priority, 10U);
    KERNOX_ASSERT_TRUE(spec1.importance == ProcessImportance::Foreground);
    KERNOX_ASSERT_EQ(spec1.initial_memory_bytes, 256ULL * 1024ULL * 1024ULL);
    KERNOX_ASSERT_EQ(spec1.requested_memory_bytes, 512ULL * 1024ULL * 1024ULL);
    KERNOX_ASSERT_EQ(spec1.cpu_burst_ticks, 30ULL);
    KERNOX_ASSERT_EQ(spec1.arrival_tick, 0ULL);
    KERNOX_ASSERT_EQ(spec1.dynamic_events.size(), 1ULL);
    KERNOX_ASSERT_EQ(spec1.dynamic_events[0].tick, 10ULL);
    KERNOX_ASSERT_EQ(spec1.dynamic_events[0].delta_bytes, 128LL * 1024LL * 1024LL);
    KERNOX_ASSERT_EQ(spec1.dynamic_events[0].reason, "Tab load");

    // Verify fingerprint reproducibility
    KERNOX_ASSERT_FALSE(wkl.fingerprint.empty());
    std::string fp1 = wkl.fingerprint;

    // Second build with identical settings yields identical fingerprint
    auto wkl2_res = CustomWorkloadBuilder::build_from(dev, {p1, p2}, 12345, "InteractiveDemo");
    KERNOX_ASSERT_TRUE(wkl2_res.is_ok());
    KERNOX_ASSERT_EQ(wkl2_res.value().fingerprint, fp1);

    // Different seed produces different fingerprint
    auto wkl3_res = CustomWorkloadBuilder::build_from(dev, {p1, p2}, 99999, "InteractiveDemo");
    KERNOX_ASSERT_TRUE(wkl3_res.is_ok());
    KERNOX_ASSERT_NE(wkl3_res.value().fingerprint, fp1);

    return 0;
}

static int test_simulation_execution_integration() {
    std::cout << "Running test_simulation_execution_integration...\n";

    // Build a custom scenario with moderate memory pressure
    CustomDeviceConfiguration dev{
        .total_ram_mb = 1024,
        .moderate_threshold_ratio = 0.70,
        .critical_threshold_ratio = 0.85
    };

    std::vector<CustomProcessInput> procs = {
        CustomProcessInput{
            .name = "com.android.dialer",
            .cpu_priority = 5,
            .importance = ProcessImportance::Foreground,
            .initial_memory_mb = 150,
            .requested_memory_mb = 200,
            .cpu_burst_ticks = 20,
            .arrival_tick = 0
        },
        CustomProcessInput{
            .name = "com.android.systemui",
            .cpu_priority = 10,
            .importance = ProcessImportance::Visible,
            .initial_memory_mb = 250,
            .requested_memory_mb = 300,
            .cpu_burst_ticks = 30,
            .arrival_tick = 0
        },
        CustomProcessInput{
            .name = "com.google.android.gms",
            .cpu_priority = 40,
            .importance = ProcessImportance::Service,
            .initial_memory_mb = 200,
            .requested_memory_mb = 250,
            .cpu_burst_ticks = 40,
            .arrival_tick = 2
        },
        CustomProcessInput{
            .name = "com.facebook.katana",
            .cpu_priority = 60,
            .importance = ProcessImportance::Background,
            .initial_memory_mb = 250,
            .requested_memory_mb = 350,
            .cpu_burst_ticks = 30,
            .arrival_tick = 4,
            .dynamic_events = {
                CustomMemoryEvent{.tick = 6, .delta_mb = 100, .reason = "Feed sync"}
            }
        },
        CustomProcessInput{
            .name = "com.rovio.angrybirds",
            .cpu_priority = 80,
            .importance = ProcessImportance::Cached,
            .initial_memory_mb = 150,
            .requested_memory_mb = 200,
            .cpu_burst_ticks = 25,
            .arrival_tick = 5
        }
    };

    auto wkl_res = CustomWorkloadBuilder::build_from(dev, procs, 42, "CustomStressTest");
    KERNOX_ASSERT_TRUE(wkl_res.is_ok());
    const auto& wkl = wkl_res.value();

    // 1. Run single policy (LMK)
    ImportanceAwareReclamationPolicy lmk_pol;
    ExperimentConfiguration exp_cfg{.thresholds = dev.to_pressure_thresholds()};
    auto lmk_res = ExperimentRunner::run_policy(wkl, lmk_pol, exp_cfg);
    KERNOX_ASSERT_TRUE(lmk_res.is_ok());

    const auto& res = lmk_res.value();
    KERNOX_ASSERT_EQ(res.scenario_name, "CustomStressTest");
    KERNOX_ASSERT_EQ(res.policy_name, "ImportanceAware");
    KERNOX_ASSERT_EQ(res.metrics.total_processes, 5ULL);
    // Foreground and Visible processes must survive under LMK
    KERNOX_ASSERT_EQ(res.metrics.foreground_survival_rate, 1.0);
    KERNOX_ASSERT_EQ(res.metrics.visible_survival_rate, 1.0);

    // 2. Compare all 4 policies
    auto comp_res = ExperimentRunner::compare_all_policies(wkl, exp_cfg);
    KERNOX_ASSERT_TRUE(comp_res.is_ok());
    const auto& comp = comp_res.value();

    KERNOX_ASSERT_EQ(comp.policy_results.size(), 4ULL);
    KERNOX_ASSERT_TRUE(comp.get_result("FIFO") != nullptr);
    KERNOX_ASSERT_TRUE(comp.get_result("LRU") != nullptr);
    KERNOX_ASSERT_TRUE(comp.get_result("Priority") != nullptr);
    KERNOX_ASSERT_TRUE(comp.get_result("ImportanceAware") != nullptr);

    std::string table = comp.to_table_string();
    KERNOX_ASSERT_FALSE(table.empty());

    return 0;
}

// =============================================================================
// Phase 2 Tests: TerminalReader, InteractiveMenu & CLI Routing
// =============================================================================

static int test_terminal_reader_parsing() {
    std::cout << "Running test_terminal_reader_parsing...\n";

    // 1. Reading string with default and cancellation
    {
        std::istringstream input("\ncom.android.chrome\nc\n");
        std::ostringstream output;
        TerminalReader reader(input, output);

        std::string s1;
        auto st1 = reader.read_string(s1, "Prompt 1", "default.app");
        KERNOX_ASSERT_TRUE(st1 == ReadStatus::Success);
        KERNOX_ASSERT_EQ(s1, "default.app");

        std::string s2;
        auto st2 = reader.read_string(s2, "Prompt 2");
        KERNOX_ASSERT_TRUE(st2 == ReadStatus::Success);
        KERNOX_ASSERT_EQ(s2, "com.android.chrome");

        std::string s3;
        auto st3 = reader.read_string(s3, "Prompt 3");
        KERNOX_ASSERT_TRUE(st3 == ReadStatus::Cancelled);
    }

    // 2. Reading uint64 with range enforcement, defaults, and error recovery
    {
        // First empty (uses default), then invalid text followed by valid number, then cancellation
        std::istringstream input("\nnot_a_number\n2048\nq\n");
        std::ostringstream output;
        TerminalReader reader(input, output);

        uint64_t val1 = 0;
        auto st1 = reader.read_uint64(val1, "RAM", 1024, 100, 8192);
        KERNOX_ASSERT_TRUE(st1 == ReadStatus::Success);
        KERNOX_ASSERT_EQ(val1, 1024ULL);

        uint64_t val2 = 0;
        auto st2 = reader.read_uint64(val2, "RAM", std::nullopt, 100, 8192);
        KERNOX_ASSERT_TRUE(st2 == ReadStatus::Success);
        KERNOX_ASSERT_EQ(val2, 2048ULL);

        uint64_t val3 = 0;
        auto st3 = reader.read_uint64(val3, "RAM", std::nullopt, 100, 8192);
        KERNOX_ASSERT_TRUE(st3 == ReadStatus::Cancelled);
    }

    // 3. Reading double with range checking
    {
        std::istringstream input("0.85\n1.5\n0.75\n");
        std::ostringstream output;
        TerminalReader reader(input, output);

        double d1 = 0.0;
        auto st1 = reader.read_double(d1, "Threshold", std::nullopt, 0.0, 1.0);
        KERNOX_ASSERT_TRUE(st1 == ReadStatus::Success);
        KERNOX_ASSERT_EQ(d1, 0.85);

        double d2 = 0.0; // 1.5 is rejected (> 1.0), then 0.75 succeeds
        auto st2 = reader.read_double(d2, "Threshold", std::nullopt, 0.0, 1.0);
        KERNOX_ASSERT_TRUE(st2 == ReadStatus::Success);
        KERNOX_ASSERT_EQ(d2, 0.75);
    }

    // 4. Reading bool (y/n)
    {
        std::istringstream input("y\nNO\n\n");
        std::ostringstream output;
        TerminalReader reader(input, output);

        bool b1 = false;
        KERNOX_ASSERT_TRUE(reader.read_bool(b1, "Confirm") == ReadStatus::Success);
        KERNOX_ASSERT_TRUE(b1 == true);

        bool b2 = true;
        KERNOX_ASSERT_TRUE(reader.read_bool(b2, "Confirm") == ReadStatus::Success);
        KERNOX_ASSERT_TRUE(b2 == false);

        bool b3 = false; // Empty uses default (true)
        KERNOX_ASSERT_TRUE(reader.read_bool(b3, "Confirm", true) == ReadStatus::Success);
        KERNOX_ASSERT_TRUE(b3 == true);
    }

    // 5. EOF Handling without crash or loop
    {
        std::istringstream empty_input("");
        std::ostringstream output;
        TerminalReader reader(empty_input, output);

        std::string s;
        KERNOX_ASSERT_TRUE(reader.read_string(s, "EOF test") == ReadStatus::Eof);
        KERNOX_ASSERT_TRUE(reader.is_eof());
    }

    return 0;
}

static int test_interactive_menu_navigation() {
    std::cout << "Running test_interactive_menu_navigation...\n";

    // 1. Direct exit via option 0
    {
        std::istringstream input("0\n");
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        int code = sim.run();
        KERNOX_ASSERT_EQ(code, 0);
        std::string out_str = output.str();
        KERNOX_ASSERT_TRUE(out_str.find("Goodbye") != std::string::npos);
    }

    // 2. EOF at main menu terminates safely
    {
        std::istringstream input("");
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        int code = sim.run();
        KERNOX_ASSERT_EQ(code, 0);
        std::string out_str = output.str();
        KERNOX_ASSERT_TRUE(out_str.find("EOF detected") != std::string::npos);
    }

    // 3. Invalid menu input recovers and accepts next valid option
    {
        std::istringstream input("99\ninvalid_choice\n0\n");
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        int code = sim.run();
        KERNOX_ASSERT_EQ(code, 0);
    }

    return 0;
}

static int test_interactive_device_configuration() {
    std::cout << "Running test_interactive_device_configuration...\n";

    // 1. Modify RAM and thresholds
    {
        // Option 1 (Configure Device):
        // RAM: 4096
        // Moderate: 0.70
        // Critical: 0.85
        // Option 0 (Exit)
        std::istringstream input("1\n4096\n0.70\n0.85\n0\n");
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        sim.run();

        const auto& cfg = sim.device_config();
        KERNOX_ASSERT_EQ(cfg.total_ram_mb, 4096ULL);
        KERNOX_ASSERT_EQ(cfg.moderate_threshold_ratio, 0.70);
        KERNOX_ASSERT_EQ(cfg.critical_threshold_ratio, 0.85);
    }

    // 2. Cancellation retains previous values
    {
        std::istringstream input("1\nc\n0\n");
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        sim.run();

        const auto& cfg = sim.device_config();
        KERNOX_ASSERT_EQ(cfg.total_ram_mb, 2048ULL); // Preserves default
    }

    return 0;
}

static int test_interactive_process_crud() {
    std::cout << "Running test_interactive_process_crud...\n";

    // 1. Add processes, edit, and remove
    {
        std::string scripted_input =
            // Add Chrome
            "2\ncom.android.chrome\n256\n512\n15\n1\n0\n30\nn\n"
            // Add Spotify
            "2\ncom.spotify.music\n128\n128\n35\n3\n2\n25\ny\n5\n64\nBuffer load\nn\n"
            // View
            "5\n"
            // Edit Chrome
            "3\n1\ncom.android.chrome\n300\n512\n10\n1\n0\n45\n"
            // Remove Spotify
            "4\n2\ny\n"
            // Exit
            "0\n";

        std::istringstream input(scripted_input);
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        int code = sim.run();
        KERNOX_ASSERT_EQ(code, 0);

        // Verification of state after operations
        KERNOX_ASSERT_EQ(sim.processes().size(), 1ULL);
        const auto& p = sim.processes()[0];
        KERNOX_ASSERT_EQ(p.name, "com.android.chrome");
        KERNOX_ASSERT_EQ(p.initial_memory_mb, 300ULL);
        KERNOX_ASSERT_EQ(p.requested_memory_mb, 512ULL);
        KERNOX_ASSERT_EQ(p.cpu_priority, 10U);
        KERNOX_ASSERT_TRUE(p.importance == ProcessImportance::Foreground);
        KERNOX_ASSERT_EQ(p.cpu_burst_ticks, 45ULL);
    }

    return 0;
}

static int test_interactive_policy_selection() {
    std::cout << "Running test_interactive_policy_selection...\n";

    // Select policies: 1 (FIFO), 2 (LRU), 3 (Priority), 4 (Importance-Aware)
    {
        std::istringstream input("6\n1\n6\n2\n6\n3\n6\n4\n0\n");
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        sim.run();

        KERNOX_ASSERT_TRUE(sim.selected_policy() == SelectedPolicy::ImportanceAware);
    }

    return 0;
}

static int test_interactive_simulation_and_history() {
    std::cout << "Running test_interactive_simulation_and_history...\n";

    {
        // 1. Run simulation with empty process list -> should be rejected
        // 2. Add 2 processes
        // 3. Run simulation (confirm 'y')
        // 4. Compare all 4 policies (confirm 'y')
        // 5. View history (view session #1 details, then 0 to return)
        // 6. Exit (0)
        std::string script =
            // 1. Run with empty list (error)
            "7\n"
            // 2. Add Process 1 (Foreground)
            "2\ncom.android.dialer\n200\n300\n10\n1\n0\n20\nn\n"
            // Add Process 2 (Cached)
            "2\ncom.rovio.game\n300\n400\n80\n5\n0\n20\nn\n"
            // 3. Run simulation
            "7\ny\n"
            // 4. Compare all policies
            "8\ny\n"
            // 5. View history
            "9\n1\n"
            // 6. Exit
            "0\n";

        std::istringstream input(script);
        std::ostringstream output;
        InteractiveSimulator sim(input, output, false);
        int code = sim.run();
        KERNOX_ASSERT_EQ(code, 0);

        // Check history contents
        const auto& hist = sim.history();
        KERNOX_ASSERT_EQ(hist.size(), 2ULL);

        // Run 1: Single policy run
        KERNOX_ASSERT_EQ(hist[0].sequence_id, 1ULL);
        KERNOX_ASSERT_EQ(hist[0].policy_name, "Importance-Aware");
        KERNOX_ASSERT_EQ(hist[0].total_processes, 2ULL);
        KERNOX_ASSERT_EQ(hist[0].foreground_survival_rate, 1.0);
        KERNOX_ASSERT_FALSE(hist[0].is_comparison);

        // Run 2: Comparative benchmark
        KERNOX_ASSERT_EQ(hist[1].sequence_id, 2ULL);
        KERNOX_ASSERT_TRUE(hist[1].is_comparison);
        KERNOX_ASSERT_EQ(hist[1].total_processes, 2ULL);
    }

    return 0;
}

static int test_cli_simulator_routing() {
    std::cout << "Running test_cli_simulator_routing...\n";

    // 1. kernox simulate --help
    int h1 = CommandDispatcher::run({"simulate", "--help"});
    KERNOX_ASSERT_EQ(h1, 0);

    // 2. kernox sim -h
    int h2 = CommandDispatcher::run({"sim", "-h"});
    KERNOX_ASSERT_EQ(h2, 0);

    // 3. kernox simulate (missing subcommand -> prints help)
    int h3 = CommandDispatcher::run({"simulate"});
    KERNOX_ASSERT_EQ(h3, 0);

    // 4. kernox simulate unknown (unknown subcommand -> error 1)
    int h4 = CommandDispatcher::run({"simulate", "invalid_subcommand"});
    KERNOX_ASSERT_EQ(h4, 1);

    return 0;
}

// =============================================================================
// Phase 3 Tests: Interactive Terminal Visualizations and Memory Analytics
// =============================================================================

static int test_ascii_utilization_bar() {
    std::cout << "Running test_ascii_utilization_bar...\n";

    // 1. Zero percent utilization
    std::string bar_0 = AsciiVisualizer::render_utilization_bar(0, 1024ULL * 1024 * 1024, 0.75, 0.90, 20, false);
    KERNOX_ASSERT_NE(bar_0.find("0.0%"), std::string::npos);
    KERNOX_ASSERT_NE(bar_0.find("(0.0 MB / 1024.0 MB)"), std::string::npos);
    KERNOX_ASSERT_EQ(bar_0.find("\033["), std::string::npos); // No ANSI when use_color = false

    // 2. Normal utilization (50%)
    std::string bar_50 = AsciiVisualizer::render_utilization_bar(512ULL * 1024 * 1024, 1024ULL * 1024 * 1024, 0.75, 0.90, 20, false);
    KERNOX_ASSERT_NE(bar_50.find("50.0%"), std::string::npos);
    KERNOX_ASSERT_NE(bar_50.find("(512.0 MB / 1024.0 MB)"), std::string::npos);

    // 3. Moderate threshold coloring (80% >= 75%)
    std::string bar_mod_color = AsciiVisualizer::render_utilization_bar(800ULL * 1024 * 1024, 1000ULL * 1024 * 1024, 0.75, 0.90, 20, true);
    KERNOX_ASSERT_NE(bar_mod_color.find("80.0%"), std::string::npos);
    KERNOX_ASSERT_NE(bar_mod_color.find("\033[1;33m"), std::string::npos); // Yellow ANSI for moderate

    // 4. Critical threshold coloring (95% >= 90%)
    std::string bar_crit_color = AsciiVisualizer::render_utilization_bar(950ULL * 1024 * 1024, 1000ULL * 1024 * 1024, 0.75, 0.90, 20, true);
    KERNOX_ASSERT_NE(bar_crit_color.find("95.0%"), std::string::npos);
    KERNOX_ASSERT_NE(bar_crit_color.find("\033[1;31m"), std::string::npos); // Red ANSI for critical

    // 5. Exact 100% memory utilization
    std::string bar_100 = AsciiVisualizer::render_utilization_bar(1000ULL * 1024 * 1024, 1000ULL * 1024 * 1024, 0.75, 0.90, 20, false);
    KERNOX_ASSERT_NE(bar_100.find("100.0%"), std::string::npos);
    KERNOX_ASSERT_NE(bar_100.find("(1000.0 MB / 1000.0 MB)"), std::string::npos);

    // 6. Overcommit (> 100%)
    std::string bar_over = AsciiVisualizer::render_utilization_bar(1200ULL * 1024 * 1024, 1000ULL * 1024 * 1024, 0.75, 0.90, 20, false);
    KERNOX_ASSERT_NE(bar_over.find("120.0%"), std::string::npos);

    // 7. Zero total memory safety
    std::string bar_zero_tot = AsciiVisualizer::render_utilization_bar(0, 0, 0.75, 0.90, 20, false);
    KERNOX_ASSERT_NE(bar_zero_tot.find("0.0%"), std::string::npos);

    return 0;
}

static int test_ascii_memory_overview() {
    std::cout << "Running test_ascii_memory_overview...\n";

    PressureThresholds thresh{
        .total_memory_bytes = 2048ULL * 1024 * 1024,
        .moderate_threshold_ratio = 0.75,
        .critical_threshold_ratio = 0.90
    };

    // 1. Normal pressure overview
    std::string norm_ov = AsciiVisualizer::render_memory_overview(500ULL * 1024 * 1024, thresh.total_memory_bytes, thresh, false);
    KERNOX_ASSERT_NE(norm_ov.find("DEVICE MEMORY OVERVIEW"), std::string::npos);
    KERNOX_ASSERT_NE(norm_ov.find("NORMAL"), std::string::npos);
    KERNOX_ASSERT_NE(norm_ov.find("2048.0 MB"), std::string::npos);
    KERNOX_ASSERT_NE(norm_ov.find("500.0 MB"), std::string::npos);
    KERNOX_ASSERT_EQ(norm_ov.find("\033["), std::string::npos);

    // 2. Moderate pressure overview
    std::string mod_ov = AsciiVisualizer::render_memory_overview(1600ULL * 1024 * 1024, thresh.total_memory_bytes, thresh, true);
    KERNOX_ASSERT_NE(mod_ov.find("MODERATE"), std::string::npos);
    KERNOX_ASSERT_NE(mod_ov.find("\033[1;33m"), std::string::npos); // Yellow

    // 3. Critical pressure overview
    std::string crit_ov = AsciiVisualizer::render_memory_overview(1950ULL * 1024 * 1024, thresh.total_memory_bytes, thresh, true);
    KERNOX_ASSERT_NE(crit_ov.find("CRITICAL"), std::string::npos);
    KERNOX_ASSERT_NE(crit_ov.find("\033[1;31m"), std::string::npos); // Red

    return 0;
}

static int test_ascii_process_distribution() {
    std::cout << "Running test_ascii_process_distribution...\n";

    uint64_t cap = 2048ULL * 1024 * 1024;

    // 1. Empty input list
    std::string empty_dist = AsciiVisualizer::render_process_input_distribution({}, cap, 20, false);
    KERNOX_ASSERT_NE(empty_dist.find("[No processes configured"), std::string::npos);

    // 2. Populated input list
    std::vector<CustomProcessInput> procs = {
        CustomProcessInput{
            .name = "com.android.systemui",
            .cpu_priority = 10,
            .importance = ProcessImportance::Foreground,
            .initial_memory_mb = 256,
            .requested_memory_mb = 300,
            .cpu_burst_ticks = 30,
            .arrival_tick = 0,
            .dynamic_events = {}
        },
        CustomProcessInput{
            .name = "com.cached.app",
            .cpu_priority = 80,
            .importance = ProcessImportance::Cached,
            .initial_memory_mb = 128,
            .requested_memory_mb = 150,
            .cpu_burst_ticks = 20,
            .arrival_tick = 0,
            .dynamic_events = {}
        }
    };
    std::string proc_dist = AsciiVisualizer::render_process_input_distribution(procs, cap, 20, false);
    KERNOX_ASSERT_NE(proc_dist.find("com.android.systemui"), std::string::npos);
    KERNOX_ASSERT_NE(proc_dist.find("Foreground"), std::string::npos);
    KERNOX_ASSERT_NE(proc_dist.find("256.0 MB"), std::string::npos);
    KERNOX_ASSERT_NE(proc_dist.find("com.cached.app"), std::string::npos);
    KERNOX_ASSERT_NE(proc_dist.find("Cached"), std::string::npos);
    KERNOX_ASSERT_NE(proc_dist.find("128.0 MB"), std::string::npos);

    // 3. Process execution records
    std::string empty_rec = AsciiVisualizer::render_process_memory_distribution({}, cap, 20, false);
    KERNOX_ASSERT_NE(empty_rec.find("[No process execution records"), std::string::npos);

    std::vector<ProcessExecutionRecord> records = {
        ProcessExecutionRecord{
            .pid = 1001,
            .name = "com.android.systemui",
            .importance = ProcessImportance::Foreground,
            .cpu_priority = 10,
            .initial_memory_bytes = 256ULL * 1024 * 1024,
            .peak_memory_bytes = 280ULL * 1024 * 1024,
            .final_memory_bytes = 256ULL * 1024 * 1024,
            .final_state = ProcessLifecycleState::Running,
            .killed_under_pressure = false,
            .termination_reason = ""
        },
        ProcessExecutionRecord{
            .pid = 1002,
            .name = "com.cached.app",
            .importance = ProcessImportance::Cached,
            .cpu_priority = 80,
            .initial_memory_bytes = 128ULL * 1024 * 1024,
            .peak_memory_bytes = 128ULL * 1024 * 1024,
            .final_memory_bytes = 0,
            .final_state = ProcessLifecycleState::Terminated,
            .killed_under_pressure = true,
            .termination_reason = "Lowest importance tier: Cached"
        }
    };

    std::string rec_dist = AsciiVisualizer::render_process_memory_distribution(records, cap, 20, false);
    KERNOX_ASSERT_NE(rec_dist.find("1001"), std::string::npos);
    KERNOX_ASSERT_NE(rec_dist.find("Survived"), std::string::npos);
    KERNOX_ASSERT_NE(rec_dist.find("1002"), std::string::npos);
    KERNOX_ASSERT_NE(rec_dist.find("Reclaimed"), std::string::npos);

    // 4. Long process name truncation and alignment
    std::vector<CustomProcessInput> long_name_procs = {
        CustomProcessInput{
            .name = "com.very.long.enterprise.application.package.name.that.exceeds.standard.column.width",
            .cpu_priority = 50,
            .importance = ProcessImportance::Service,
            .initial_memory_mb = 100,
            .requested_memory_mb = 120,
            .cpu_burst_ticks = 10,
            .arrival_tick = 0,
            .dynamic_events = {}
        }
    };
    std::string long_dist = AsciiVisualizer::render_process_input_distribution(long_name_procs, cap, 20, false);
    KERNOX_ASSERT_NE(long_dist.find("com.very.long.enterprise.appl"), std::string::npos);

    return 0;
}

static int test_ascii_timeline_chart() {
    std::cout << "Running test_ascii_timeline_chart...\n";

    PressureThresholds thresh{
        .total_memory_bytes = 1000ULL * 1024 * 1024,
        .moderate_threshold_ratio = 0.75,
        .critical_threshold_ratio = 0.90
    };

    // 1. Empty timeline
    std::string empty_tl = AsciiVisualizer::render_timeline_chart({}, thresh.total_memory_bytes, thresh, 8, 40, false);
    KERNOX_ASSERT_NE(empty_tl.find("[No timeline samples recorded"), std::string::npos);

    // 2. Populated timeline
    std::vector<MemoryTimelinePoint> timeline;
    for (uint64_t t = 0; t <= 30; ++t) {
        uint64_t mem = (500 + t * 15) * 1024 * 1024; // Increases from 500 MB to 950 MB
        MemoryPressureLevel p = (mem >= 900ULL * 1024 * 1024)
            ? MemoryPressureLevel::Critical
            : ((mem >= 750ULL * 1024 * 1024) ? MemoryPressureLevel::Moderate : MemoryPressureLevel::Normal);
        timeline.push_back(MemoryTimelinePoint{
            .tick = t,
            .consumed_bytes = mem,
            .pressure_level = p,
            .active_processes = 5
        });
    }

    std::string chart = AsciiVisualizer::render_timeline_chart(timeline, thresh.total_memory_bytes, thresh, 8, 40, false);
    KERNOX_ASSERT_NE(chart.find("MEMORY CONSUMPTION TIMELINE"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("100% |"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("[CRIT]|"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("[MOD] |"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("0% |"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("Tick:  0"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("Peak Used:"), std::string::npos);
    KERNOX_ASSERT_NE(chart.find("Samples: 31"), std::string::npos);

    return 0;
}

static int test_ascii_reclamation_log() {
    std::cout << "Running test_ascii_reclamation_log...\n";

    // 1. Empty log
    std::string empty_log = AsciiVisualizer::render_reclamation_log({}, false);
    KERNOX_ASSERT_NE(empty_log.find("[No reclamation events executed"), std::string::npos);

    // 2. Populated log
    std::vector<ReclamationDecision> log = {
        ReclamationDecision{
            .victim_pid = 1005,
            .victim_name = "com.game.cached",
            .importance = ProcessImportance::Cached,
            .cpu_priority = 90,
            .memory_reclaimed_bytes = 150ULL * 1024 * 1024,
            .policy_name = "Importance-Aware",
            .reason = "Lowest importance tier: Cached (score 500)",
            .timestamp_ticks = 14,
            .pressure_level = MemoryPressureLevel::Critical
        },
        ReclamationDecision{
            .victim_pid = 1004,
            .victim_name = "com.browser.tab",
            .importance = ProcessImportance::Background,
            .cpu_priority = 50,
            .memory_reclaimed_bytes = 200ULL * 1024 * 1024,
            .policy_name = "Importance-Aware",
            .reason = "Lowest importance tier: Background (score 400)",
            .timestamp_ticks = 18,
            .pressure_level = MemoryPressureLevel::Moderate
        }
    };

    std::string rendered = AsciiVisualizer::render_reclamation_log(log, false);
    KERNOX_ASSERT_NE(rendered.find("RECLAMATION AUDIT TRAIL"), std::string::npos);
    KERNOX_ASSERT_NE(rendered.find("1005"), std::string::npos);
    KERNOX_ASSERT_NE(rendered.find("CRIT"), std::string::npos);
    KERNOX_ASSERT_NE(rendered.find("com.game.cached"), std::string::npos);
    KERNOX_ASSERT_NE(rendered.find("150.0 MB"), std::string::npos);
    KERNOX_ASSERT_NE(rendered.find("Lowest importance tier: Cached"), std::string::npos);
    KERNOX_ASSERT_NE(rendered.find("Total Reclaimed: 350.0 MB across 2 process termination(s)."), std::string::npos);

    return 0;
}

static int test_ascii_comparison_matrix() {
    std::cout << "Running test_ascii_comparison_matrix...\n";

    // Build a workload that exerts pressure
    CustomDeviceConfiguration dev_cfg{.total_ram_mb = 600, .moderate_threshold_ratio = 0.70, .critical_threshold_ratio = 0.85};
    std::vector<CustomProcessInput> procs = {
        CustomProcessInput{.name = "fg.app1", .cpu_priority = 10, .importance = ProcessImportance::Foreground, .initial_memory_mb = 150, .requested_memory_mb = 250, .cpu_burst_ticks = 20, .arrival_tick = 0, .dynamic_events = {}},
        CustomProcessInput{.name = "fg.app2", .cpu_priority = 15, .importance = ProcessImportance::Foreground, .initial_memory_mb = 150, .requested_memory_mb = 250, .cpu_burst_ticks = 20, .arrival_tick = 0, .dynamic_events = {}},
        CustomProcessInput{.name = "cached.app", .cpu_priority = 90, .importance = ProcessImportance::Cached, .initial_memory_mb = 100, .requested_memory_mb = 200, .cpu_burst_ticks = 20, .arrival_tick = 0, .dynamic_events = {}},
        CustomProcessInput{.name = "bg.app", .cpu_priority = 50, .importance = ProcessImportance::Background, .initial_memory_mb = 100, .requested_memory_mb = 200, .cpu_burst_ticks = 20, .arrival_tick = 0, .dynamic_events = {}}
    };

    CustomWorkloadBuilder builder;
    builder.set_device_config(dev_cfg)
           .set_processes(procs)
           .set_seed(42)
           .set_scenario_name("MatrixTestScenario");

    auto wkl = builder.build().value();
    ExperimentConfiguration exp_cfg{.thresholds = dev_cfg.to_pressure_thresholds()};
    auto comp = ExperimentRunner::compare_all_policies(wkl, exp_cfg).value();

    std::string matrix = AsciiVisualizer::render_comparison_matrix(comp, false);
    KERNOX_ASSERT_NE(matrix.find("4-POLICY COMPARATIVE BENCHMARK MATRIX"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("FIFO"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("LRU"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("Priority"), std::string::npos);
    KERNOX_ASSERT_TRUE(matrix.find("ImportanceAware") != std::string::npos || matrix.find("Importance-Aware") != std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("Victims"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("Peak Util %"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("Fail Allocs"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("Survival Ratio Visual Bar"), std::string::npos);
    KERNOX_ASSERT_NE(matrix.find("Key Insight:"), std::string::npos);

    return 0;
}

static int test_interactive_visualizations_integration() {
    std::cout << "Running test_interactive_visualizations_integration...\n";

    std::string script =
        // 1. Add Process 1 (Foreground)
        "2\ncom.android.dialer\n200\n300\n10\n1\n0\n20\nn\n"
        // 2. Add Process 2 (Cached)
        "2\ncom.rovio.game\n300\n400\n80\n5\n0\n20\nn\n"
        // 3. View process list (Option 5) -> renders input distribution
        "5\n"
        // 4. Run simulation (Option 7) -> renders overview, timeline, records, reclamation log
        "7\ny\n"
        // 5. Compare all policies (Option 8) -> renders comparison matrix
        "8\ny\n"
        // 6. View history (Option 9) -> inspect session 1 details
        "9\n1\n"
        // 7. Exit (Option 0)
        "0\n";

    std::istringstream input(script);
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false);
    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    // Verify last_result and last_comparison are populated
    KERNOX_ASSERT_TRUE(sim.last_result().has_value());
    KERNOX_ASSERT_TRUE(sim.last_comparison().has_value());

    const auto& res = sim.last_result().value();
    KERNOX_ASSERT_FALSE(res.memory_timeline.empty());
    KERNOX_ASSERT_FALSE(res.process_records.empty());

    // Verify output stream contains visualization elements
    std::string out_str = output.str();
    KERNOX_ASSERT_NE(out_str.find("PROCESS MEMORY DISTRIBUTION"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("DEVICE MEMORY OVERVIEW"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("4-POLICY COMPARATIVE BENCHMARK MATRIX"), std::string::npos);

    // Verify session history has saved visual blocks
    const auto& hist = sim.history();
    KERNOX_ASSERT_EQ(hist.size(), 2ULL);
    KERNOX_ASSERT_FALSE(hist[0].visual_overview.empty());
    KERNOX_ASSERT_FALSE(hist[0].timeline_chart.empty());
    KERNOX_ASSERT_FALSE(hist[1].comparison_matrix.empty());

    return 0;
}

// =============================================================================
// Phase 4B Tests: AnalyticsBridge & Python Integration
// =============================================================================

class MockAnalyticsRunner : public kernox::adb::ICommandRunner {
public:
    bool all_available{true};
    std::vector<std::string> available_executables{};
    kernox::adb::CommandOutput response{};
    std::vector<std::vector<std::string>> calls{};

    bool is_executable_available(const std::string& executable) override {
        if (!all_available) return false;
        if (!available_executables.empty()) {
            for (const auto& a : available_executables) {
                if (a == executable) return true;
            }
            return false;
        }
        return true;
    }

    kernox::core::Result<kernox::adb::CommandOutput> execute(
        const std::string& executable,
        const std::vector<std::string>& arguments,
        std::chrono::milliseconds timeout
    ) override {
        (void)timeout;
        std::vector<std::string> call = {executable};
        for (const auto& a : arguments) call.push_back(a);
        calls.push_back(call);
        return kernox::core::Result<kernox::adb::CommandOutput>::ok(response);
    }
};

static int test_analytics_bridge_discovery() {
    std::cout << "Running test_analytics_bridge_discovery...\n";

    auto mock = std::make_shared<MockAnalyticsRunner>();
    AnalyticsBridge bridge(mock);

    // 1. Prioritizes .venv/bin/python when available
    mock->available_executables = {".venv/bin/python", "python3"};
    std::string detected = bridge.detect_python_interpreter();
    KERNOX_ASSERT_EQ(detected, ".venv/bin/python");

    // 2. Falls back to python3 if .venv is absent
    mock->available_executables = {"python3"};
    std::string fb = bridge.detect_python_interpreter();
    KERNOX_ASSERT_EQ(fb, "python3");

    // 3. Returns empty string if no python executable exists
    mock->all_available = false;
    mock->available_executables.clear();
    std::string none = bridge.detect_python_interpreter();
    KERNOX_ASSERT_EQ(none, "");

    // 4. Custom override takes precedence when available
    mock->all_available = true;
    mock->available_executables = {"/custom/env/bin/python", "python3"};
    bridge.set_python_override("/custom/env/bin/python");
    KERNOX_ASSERT_EQ(bridge.detect_python_interpreter(), "/custom/env/bin/python");

    return 0;
}

static int test_analytics_bridge_environment_checks() {
    std::cout << "Running test_analytics_bridge_environment_checks...\n";

    auto mock = std::make_shared<MockAnalyticsRunner>();
    AnalyticsBridge bridge(mock);

    // 1. Missing python returns NotFound error
    mock->all_available = false;
    auto no_py_res = bridge.check_environment();
    KERNOX_ASSERT_TRUE(no_py_res.is_err());
    KERNOX_ASSERT_EQ(no_py_res.error().code, ErrorCode::NotFound);

    // 2. Missing dependencies (exit code 1 on import test)
    mock->all_available = true;
    mock->response.exit_code = 1;
    mock->response.stderr_content = "ModuleNotFoundError: No module named 'pandas'";
    auto dep_res = bridge.check_environment();
    KERNOX_ASSERT_TRUE(dep_res.is_err());
    KERNOX_ASSERT_EQ(dep_res.error().code, ErrorCode::ProcessFailed);
    KERNOX_ASSERT_NE(dep_res.error().message.find("pandas"), std::string::npos);

    // 3. Healthy environment (exit code 0)
    mock->response.exit_code = 0;
    mock->response.stderr_content = "";
    auto ok_res = bridge.check_environment();
    KERNOX_ASSERT_TRUE(ok_res.is_ok());

    return 0;
}

static int test_analytics_bridge_execution_handling() {
    std::cout << "Running test_analytics_bridge_execution_handling...\n";

    auto mock = std::make_shared<MockAnalyticsRunner>();
    AnalyticsBridge bridge(mock);

    // 1. Missing input JSON file returns NotFound
    auto no_file = bridge.generate_analytics("non_existent_file_12345.json");
    KERNOX_ASSERT_TRUE(no_file.is_err());
    KERNOX_ASSERT_EQ(no_file.error().code, ErrorCode::NotFound);

    // 2. Create temporary file for valid path testing
    ::mkdir("artifacts", 0755);
    ::mkdir("artifacts/experiments", 0755);
    std::string tmp_json = "artifacts/experiments/tmp_test_spec.json";
    auto wr = ExperimentExporter::write_to_file(tmp_json, "{}");
    KERNOX_ASSERT_TRUE(wr.is_ok());

    // 3. CLI execution failure captures exit code and stderr
    mock->response.exit_code = 2;
    mock->response.stderr_content = "ValidationError: Empty experiment dataset";
    auto fail_res = bridge.generate_analytics(tmp_json);
    KERNOX_ASSERT_TRUE(fail_res.is_err());
    KERNOX_ASSERT_EQ(fail_res.error().code, ErrorCode::ProcessFailed);
    KERNOX_ASSERT_NE(fail_res.error().message.find("ValidationError"), std::string::npos);

    // 4. Timeout handling
    mock->response.timed_out = true;
    auto to_res = bridge.generate_analytics(tmp_json);
    KERNOX_ASSERT_TRUE(to_res.is_err());
    KERNOX_ASSERT_EQ(to_res.error().code, ErrorCode::Timeout);
    mock->response.timed_out = false;

    // 5. Successful analytics execution populates report and chart list
    mock->response.exit_code = 0;
    mock->response.stderr_content = "";
    mock->response.stdout_content = "Analysis complete.";
    auto ok_res = bridge.generate_analytics(tmp_json, "artifacts/analytics/test_out");
    KERNOX_ASSERT_TRUE(ok_res.is_ok());
    const auto& res = ok_res.value();
    KERNOX_ASSERT_TRUE(res.success);
    KERNOX_ASSERT_EQ(res.report_markdown_path, "artifacts/analytics/test_out/experiment_summary.md");
    KERNOX_ASSERT_EQ(res.generated_charts.size(), 4ULL);

    // Verify arguments passed to runner were properly separated (Refinement 2)
    KERNOX_ASSERT_FALSE(mock->calls.empty());
    const auto& last_call = mock->calls.back();
    KERNOX_ASSERT_EQ(last_call[1], "-m");
    KERNOX_ASSERT_EQ(last_call[2], "kernox_analytics.cli");
    KERNOX_ASSERT_EQ(last_call[3], "report");
    KERNOX_ASSERT_EQ(last_call[4], "-i");
    KERNOX_ASSERT_EQ(last_call[5], tmp_json);
    KERNOX_ASSERT_EQ(last_call[6], "-o");
    KERNOX_ASSERT_EQ(last_call[7], "artifacts/analytics/test_out");

    return 0;
}

static int test_analytics_bridge_export_optionality() {
    std::cout << "Running test_analytics_bridge_export_optionality...\n";

    auto mock = std::make_shared<MockAnalyticsRunner>();
    AnalyticsBridge bridge(mock);

    // Construct a dummy ExperimentResult
    ExperimentResult res;
    res.policy_name = "ImportanceAware";
    res.scenario_name = "OptionalityTest";
    res.seed = 42;
    res.workload_fingerprint = "WKL-TEST-OPT";
    res.metrics.total_capacity_bytes = 1024ULL * 1024ULL * 1024ULL;
    res.metrics.survival_rate = 1.0;

    std::string test_json = "artifacts/experiments/test_optionality.json";

    // Case A: run_analytics = false -> JSON is exported, success = true, no python called
    auto res_no_ana = bridge.export_and_analyze(res, test_json, "artifacts/analytics/test_opt", false);
    KERNOX_ASSERT_TRUE(res_no_ana.is_ok());
    KERNOX_ASSERT_TRUE(res_no_ana.value().json_exported);
    KERNOX_ASSERT_TRUE(res_no_ana.value().success);
    KERNOX_ASSERT_EQ(::access(test_json.c_str(), R_OK), 0);

    // Case B: Analytics fails (Refinement 4: JSON export MUST still succeed)
    mock->response.exit_code = 1;
    mock->response.stderr_content = "Simulated missing dependency error";
    auto res_fail = bridge.export_and_analyze(res, test_json, "artifacts/analytics/test_opt", true);
    KERNOX_ASSERT_TRUE(res_fail.is_ok());
    const auto& inv_fail = res_fail.value();
    // Crucial check: JSON was exported successfully despite analytics failure
    KERNOX_ASSERT_TRUE(inv_fail.json_exported);
    KERNOX_ASSERT_FALSE(inv_fail.success);
    KERNOX_ASSERT_FALSE(inv_fail.error_message.empty());
    KERNOX_ASSERT_EQ(::access(test_json.c_str(), R_OK), 0);

    return 0;
}

static int test_real_python_analytics_integration() {
    std::cout << "Running test_real_python_analytics_integration...\n";

    // Use default SystemCommandRunner to test actual host Python if available
    AnalyticsBridge bridge;
    std::string py = bridge.detect_python_interpreter();
    if (py.empty()) {
        std::cout << "  [SKIP] Python interpreter not found on host; skipping live integration.\n";
        return 0;
    }

    auto env_res = bridge.check_environment();
    if (env_res.is_err()) {
        std::cout << "  [SKIP] Python environment dependencies not fully satisfied: "
                  << env_res.error().message << "; skipping live integration.\n";
        return 0;
    }

    // Build small valid comparison
    CustomDeviceConfiguration dev{.total_ram_mb = 1024};
    CustomProcessInput p1{
        .name = "com.test.app1",
        .cpu_priority = 10,
        .importance = ProcessImportance::Foreground,
        .initial_memory_mb = 200,
        .requested_memory_mb = 200,
        .cpu_burst_ticks = 20,
        .arrival_tick = 0
    };
    CustomProcessInput p2{
        .name = "com.test.app2",
        .cpu_priority = 5,
        .importance = ProcessImportance::Background,
        .initial_memory_mb = 300,
        .requested_memory_mb = 300,
        .cpu_burst_ticks = 20,
        .arrival_tick = 0
    };

    CustomWorkloadBuilder builder;
    builder.set_device_config(dev).set_processes({p1, p2}).set_seed(42);
    auto wkl = builder.build().value();

    ExperimentConfiguration cfg{.thresholds = dev.to_pressure_thresholds()};
    auto comp = ExperimentRunner::compare_all_policies(wkl, cfg).value();

    std::string json_path = "artifacts/experiments/test_live_comparison.json";
    std::string out_dir = "artifacts/analytics/test_live_comparison";

    auto inv = bridge.export_and_analyze(comp, json_path, out_dir, true);
    KERNOX_ASSERT_TRUE(inv.is_ok());
    const auto& res = inv.value();
    KERNOX_ASSERT_TRUE(res.json_exported);
    KERNOX_ASSERT_TRUE(res.success);

    // Verify report markdown was generated on disk
    std::string report_file = out_dir + "/experiment_summary.md";
    KERNOX_ASSERT_EQ(::access(report_file.c_str(), R_OK), 0);

    // Verify charts were generated on disk
    KERNOX_ASSERT_EQ(::access((out_dir + "/policy_comparison.png").c_str(), R_OK), 0);
    KERNOX_ASSERT_EQ(::access((out_dir + "/process_survival.png").c_str(), R_OK), 0);
    KERNOX_ASSERT_EQ(::access((out_dir + "/memory_pressure.png").c_str(), R_OK), 0);
    KERNOX_ASSERT_EQ(::access((out_dir + "/execution_performance.png").c_str(), R_OK), 0);

    std::cout << "  [SUCCESS] Live Python analytics generated report & charts verified on disk.\n";
    return 0;
}

static int test_interactive_menu_option_10_empty() {
    std::cout << "Running test_interactive_menu_option_10_empty...\n";

    // Selecting option 10 before running any simulation should output error and not crash.
    std::istringstream input("10\n0\n");
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false);
    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    std::string out_str = output.str();
    KERNOX_ASSERT_NE(out_str.find("[ERROR] No simulation results available to export"), std::string::npos);
    return 0;
}

static int test_interactive_menu_option_10_single_run_and_stale() {
    std::cout << "Running test_interactive_menu_option_10_single_run_and_stale...\n";

    ::unlink("artifacts/experiments/test_menu_single.json");
    ::unlink("artifacts/experiments/test_menu_stale.json");

    auto runner = std::make_shared<MockAnalyticsRunner>();
    runner->all_available = true;
    runner->response.exit_code = 0;
    runner->response.stdout_content = "Analysis complete. Report generated at artifacts/analytics/mock/experiment_summary.md\n";
    auto bridge = std::make_shared<AnalyticsBridge>(runner);

    // Workflow:
    // 1. Add process
    // 2. Run simulation (7 -> y)
    // 3. Option 10 (Export single run -> accept default json path -> accept run_analytics -> accept output dir)
    // 4. Edit process (modify config) -> sets config_modified_since_run_ = true
    // 5. Option 10 again -> checks that stale warning is displayed!
    // 6. Exit (0)
    std::string script =
        // Add Process
        "2\ncom.android.browser\n200\n300\n20\n1\n0\n30\nn\n"
        // Run sim (7 -> y)
        "7\ny\n"
        // Option 10: Export
        "10\nartifacts/experiments/test_menu_single.json\ny\nartifacts/analytics/test_menu_single\n"
        // Edit Process (Option 3 -> proc 1 -> change initial mem to 250)
        "3\n1\ncom.android.browser\n250\n300\n20\n1\n0\n30\n"
        // Option 10 again: checks stale warning
        "10\nartifacts/experiments/test_menu_stale.json\nn\n"
        // Exit
        "0\n";

    std::istringstream input(script);
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false);
    sim.set_analytics_bridge(bridge);

    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    KERNOX_ASSERT_TRUE(sim.last_result().has_value());
    KERNOX_ASSERT_TRUE(sim.config_modified_since_run());
    KERNOX_ASSERT_TRUE(sim.latest_simulation_type() == LatestSimulationType::SingleResult);

    std::string out_str = output.str();
    // Verify first export receipt
    KERNOX_ASSERT_NE(out_str.find("test_menu_single.json"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("SIMULATION EXPORT & ANALYTICS RECEIPT"), std::string::npos);
    // Verify stale warning was shown on second export
    KERNOX_ASSERT_NE(out_str.find("Workload/device configuration has changed since the last simulation"), std::string::npos);

    // Verify mock runner was invoked
    KERNOX_ASSERT_FALSE(runner->calls.empty());
    return 0;
}

static int test_interactive_menu_option_10_comparison_target_selection() {
    std::cout << "Running test_interactive_menu_option_10_comparison_target_selection...\n";

    ::unlink("artifacts/experiments/test_menu_comp.json");

    auto runner = std::make_shared<MockAnalyticsRunner>();
    runner->all_available = true;
    runner->response.exit_code = 0;
    auto bridge = std::make_shared<AnalyticsBridge>(runner);

    // Workflow:
    // 1. Add process
    // 2. Run single simulation (7 -> y)
    // 3. Compare all policies (8 -> y) -> latest is now Comparison!
    // 4. Option 10:
    //    Prompts: Target [1-2] (default 2 for comparison) -> accept default
    //    JSON path -> custom path
    //    run_analytics -> y
    //    output dir -> custom dir
    // 5. Exit (0)
    std::string script =
        "2\ncom.android.calculator\n100\n150\n50\n4\n0\n10\nn\n"
        "7\ny\n"
        "8\ny\n"
        "10\n2\nartifacts/experiments/test_menu_comp.json\ny\nartifacts/analytics/test_menu_comp\n"
        "0\n";

    std::istringstream input(script);
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false);
    sim.set_analytics_bridge(bridge);

    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    KERNOX_ASSERT_TRUE(sim.last_result().has_value());
    KERNOX_ASSERT_TRUE(sim.last_comparison().has_value());
    KERNOX_ASSERT_TRUE(sim.latest_simulation_type() == LatestSimulationType::Comparison);

    std::string out_str = output.str();
    KERNOX_ASSERT_NE(out_str.find("Select Experiment Data to Export"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("4-Policy Comparison Benchmark [LATEST RUN]"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("test_menu_comp.json"), std::string::npos);

    return 0;
}

static int test_interactive_menu_post_simulation_prompt() {
    std::cout << "Running test_interactive_menu_post_simulation_prompt...\n";

    ::unlink("artifacts/experiments/interactive_simulation.json");

    auto runner = std::make_shared<MockAnalyticsRunner>();
    runner->all_available = true;
    runner->response.exit_code = 0;
    auto bridge = std::make_shared<AnalyticsBridge>(runner);

    // Workflow with post-simulation prompt enabled:
    // 1. Add process
    // 2. Run simulation (7 -> y)
    //    Simulator prompts: "Export results and generate Python analytics report? (y/N): "
    //    User answers: "y"
    //    Then export wizard prompts:
    //    JSON path -> enter \n (uses default)
    //    run_analytics -> enter \n (default yes)
    //    output dir -> enter \n (default)
    // 3. Exit (0)
    std::string script =
        "2\ncom.android.dialer\n120\n200\n10\n1\n0\n20\nn\n"
        "7\ny\ny\n\n\n\n"
        "0\n";

    std::istringstream input(script);
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false, true); // enable_post_simulation_prompt = true
    sim.set_analytics_bridge(bridge);

    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    std::string out_str = output.str();
    KERNOX_ASSERT_NE(out_str.find("Export results and generate Python analytics report?"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("SIMULATION EXPORT & ANALYTICS RECEIPT"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("interactive_simulation.json"), std::string::npos);

    return 0;
}

static int test_export_overwrite_declined() {
    std::cout << "Running test_export_overwrite_declined...\n";

    std::string test_path = "artifacts/experiments/test_overwrite_declined.json";
    ::mkdir("artifacts", 0755);
    ::mkdir("artifacts/experiments", 0755);

    // Pre-create file with sentinel content
    {
        std::ofstream f(test_path, std::ios::out | std::ios::trunc);
        f << "{\"sentinel\": \"ORIGINAL_CONTENT\"}\n";
    }

    auto runner = std::make_shared<MockAnalyticsRunner>();
    runner->all_available = true;
    runner->response.exit_code = 0;
    auto bridge = std::make_shared<AnalyticsBridge>(runner);

    // Workflow:
    // Add process -> run simulation (7 -> y)
    // Option 10:
    // Target: 1
    // JSON path: test_path
    // When prompted "Overwrite existing JSON export? (y/N)": respond 'n'
    // Exit: 0
    std::string script =
        "2\ncom.android.chrome\n200\n300\n20\n1\n0\n30\nn\n"
        "7\ny\n"
        "10\n" + test_path + "\nn\n"
        "0\n";

    std::istringstream input(script);
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false);
    sim.set_analytics_bridge(bridge);

    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    std::string out_str = output.str();
    KERNOX_ASSERT_NE(out_str.find("Destination file already exists"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("Export cancelled: Existing JSON file preserved"), std::string::npos);

    // Verify file content is UNCHANGED
    std::ifstream f_in(test_path);
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    KERNOX_ASSERT_NE(content.find("ORIGINAL_CONTENT"), std::string::npos);

    return 0;
}

static int test_export_overwrite_accepted() {
    std::cout << "Running test_export_overwrite_accepted...\n";

    std::string test_path = "artifacts/experiments/test_overwrite_accepted.json";
    ::mkdir("artifacts", 0755);
    ::mkdir("artifacts/experiments", 0755);

    // Pre-create file with sentinel content
    {
        std::ofstream f(test_path, std::ios::out | std::ios::trunc);
        f << "{\"sentinel\": \"STALE_DATA\"}\n";
    }

    auto runner = std::make_shared<MockAnalyticsRunner>();
    runner->all_available = true;
    runner->response.exit_code = 0;
    auto bridge = std::make_shared<AnalyticsBridge>(runner);

    // Workflow:
    // Add process -> run simulation (7 -> y)
    // Option 10:
    // JSON path: test_path
    // When prompted "Overwrite existing JSON export? (y/N)": respond 'y'
    // run_analytics: 'n'
    // Exit: 0
    std::string script =
        "2\ncom.android.chrome\n200\n300\n20\n1\n0\n30\nn\n"
        "7\ny\n"
        "10\n" + test_path + "\ny\nn\n"
        "0\n";

    std::istringstream input(script);
    std::ostringstream output;
    InteractiveSimulator sim(input, output, false);
    sim.set_analytics_bridge(bridge);

    int code = sim.run();
    KERNOX_ASSERT_EQ(code, 0);

    std::string out_str = output.str();
    KERNOX_ASSERT_NE(out_str.find("Destination file already exists"), std::string::npos);
    KERNOX_ASSERT_NE(out_str.find("[SUCCESS] artifacts/experiments/test_overwrite_accepted.json"), std::string::npos);

    // Verify file content was overwritten with genuine simulation result
    std::ifstream f_in(test_path);
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    KERNOX_ASSERT_EQ(content.find("STALE_DATA"), std::string::npos);
    KERNOX_ASSERT_NE(content.find("InteractiveCustomWorkload"), std::string::npos);

    return 0;
}

static int test_project_root_discovery_missing_and_cwd_variants() {
    std::cout << "Running test_project_root_discovery_missing_and_cwd_variants...\n";

    AnalyticsBridge bridge;

    // 1. Launch from repository root: discovery succeeds and finds CMakeLists.txt
    std::string root = bridge.discover_project_root();
    KERNOX_ASSERT_FALSE(root.empty());
    KERNOX_ASSERT_EQ(::access((root + "/CMakeLists.txt").c_str(), R_OK), 0);
    KERNOX_ASSERT_EQ(::access((root + "/tools/analytics/src").c_str(), R_OK), 0);

    // 2. Interpreter detection derives project root virtualenv
    std::string py = bridge.detect_python_interpreter();
    KERNOX_ASSERT_FALSE(py.empty());
    KERNOX_ASSERT_NE(py.find(".venv/bin/python"), std::string::npos);

    // 3. Simulated launch from build/debug:
    char original_cwd[1024];
    if (::getcwd(original_cwd, sizeof(original_cwd)) != nullptr) {
        if (::chdir("build/debug") == 0) {
            AnalyticsBridge debug_bridge;
            std::string debug_root = debug_bridge.discover_project_root();
            KERNOX_ASSERT_FALSE(debug_root.empty());
            KERNOX_ASSERT_EQ(debug_root, root);
            std::string debug_py = debug_bridge.detect_python_interpreter();
            KERNOX_ASSERT_FALSE(debug_py.empty());
            KERNOX_ASSERT_NE(debug_py.find(".venv/bin/python"), std::string::npos);

            // 4. Simulated launch from /tmp (unrelated working directory):
            if (::chdir("/tmp") == 0) {
                AnalyticsBridge tmp_bridge;
                std::string tmp_root = tmp_bridge.discover_project_root();
                // /proc/self/exe is still the test binary in the build directory, so root is discovered!
                KERNOX_ASSERT_FALSE(tmp_root.empty());
                KERNOX_ASSERT_EQ(tmp_root, root);
                std::string tmp_py = tmp_bridge.detect_python_interpreter();
                KERNOX_ASSERT_FALSE(tmp_py.empty());
                KERNOX_ASSERT_NE(tmp_py.find(".venv/bin/python"), std::string::npos);
            }

            // Restore original working directory
            int chdir_res = ::chdir(original_cwd);
            KERNOX_ASSERT_EQ(chdir_res, 0);
        }
    }

    // 5. Explicit missing project root override
    AnalyticsBridge missing_bridge;
    missing_bridge.set_project_root_override("/tmp/nonexistent_kernox_root_dir_abc123");
    std::string missing_root = missing_bridge.discover_project_root();
    KERNOX_ASSERT_EQ(missing_root, "/tmp/nonexistent_kernox_root_dir_abc123");
    // Verify fallback to candidates or PATH python without crashing
    std::string fallback_py = missing_bridge.detect_python_interpreter();
    KERNOX_ASSERT_FALSE(fallback_py.empty());

    return 0;
}

static int test_python_failure_json_preservation() {
    std::cout << "Running test_python_failure_json_preservation...\n";

    auto fail_runner = std::make_shared<MockAnalyticsRunner>();
    fail_runner->all_available = true;
    fail_runner->response.exit_code = 1;
    fail_runner->response.stderr_content = "SyntaxError: simulated Python runtime crash";
    AnalyticsBridge bridge(fail_runner);

    CustomDeviceConfiguration dev{.total_ram_mb = 1024};
    CustomProcessInput p{
        .name = "com.test.preserve",
        .cpu_priority = 10,
        .importance = ProcessImportance::Foreground,
        .initial_memory_mb = 200,
        .requested_memory_mb = 200,
        .cpu_burst_ticks = 20,
        .arrival_tick = 0
    };
    CustomWorkloadBuilder b;
    b.set_device_config(dev).set_processes({p}).set_seed(42);
    auto wkl = b.build().value();
    ExperimentConfiguration cfg{.thresholds = dev.to_pressure_thresholds()};
    FifoReclamationPolicy pol;
    auto res = ExperimentRunner::run_policy(wkl, pol, cfg).value();

    std::string json_path = "artifacts/experiments/test_preserve_on_fail.json";
    std::string out_dir = "artifacts/analytics/test_preserve_on_fail";
    ::unlink(json_path.c_str());

    auto inv_res = bridge.export_and_analyze(res, json_path, out_dir, true);
    KERNOX_ASSERT_TRUE(inv_res.is_ok());

    const auto& inv = inv_res.value();
    KERNOX_ASSERT_TRUE(inv.json_exported);
    KERNOX_ASSERT_FALSE(inv.success);
    KERNOX_ASSERT_NE(inv.error_message.find("simulated Python runtime crash"), std::string::npos);

    // Verify JSON file is intact on disk and non-empty
    KERNOX_ASSERT_EQ(::access(json_path.c_str(), R_OK), 0);
    std::ifstream jf(json_path);
    std::string line;
    std::getline(jf, line);
    KERNOX_ASSERT_FALSE(line.empty());

    return 0;
}

int main() {
    int failures = 0;

    // Phase 1 tests
    failures += test_device_config_validation();
    failures += test_process_input_validation();
    failures += test_process_collection_validation();
    failures += test_workload_builder_deterministic();
    failures += test_simulation_execution_integration();

    // Phase 2 tests
    failures += test_terminal_reader_parsing();
    failures += test_interactive_menu_navigation();
    failures += test_interactive_device_configuration();
    failures += test_interactive_process_crud();
    failures += test_interactive_policy_selection();
    failures += test_interactive_simulation_and_history();
    failures += test_cli_simulator_routing();

    // Phase 3 tests
    failures += test_ascii_utilization_bar();
    failures += test_ascii_memory_overview();
    failures += test_ascii_process_distribution();
    failures += test_ascii_timeline_chart();
    failures += test_ascii_reclamation_log();
    failures += test_ascii_comparison_matrix();
    failures += test_interactive_visualizations_integration();

    // Phase 4B tests (AnalyticsBridge & Python Integration)
    failures += test_analytics_bridge_discovery();
    failures += test_analytics_bridge_environment_checks();
    failures += test_analytics_bridge_execution_handling();
    failures += test_analytics_bridge_export_optionality();
    failures += test_real_python_analytics_integration();
    failures += test_interactive_menu_option_10_empty();
    failures += test_interactive_menu_option_10_single_run_and_stale();
    failures += test_interactive_menu_option_10_comparison_target_selection();
    failures += test_interactive_menu_post_simulation_prompt();
    failures += test_export_overwrite_declined();
    failures += test_export_overwrite_accepted();
    failures += test_project_root_discovery_missing_and_cwd_variants();
    failures += test_python_failure_json_preservation();

    if (failures == 0) {
        std::cout << "\n[PASS] All interactive simulator Phase 1, Phase 2, Phase 3 & Phase 4B unit & integration tests passed!\n";
    } else {
        std::cerr << "\n[FAIL] " << failures << " interactive simulator test(s) failed!\n";
    }

    return failures;
}
