#include "kernox/modules/simulator/interactive_menu.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace kernox::simulator {

std::string_view to_string(SelectedPolicy pol) noexcept {
    switch (pol) {
        case SelectedPolicy::Fifo:
            return "FIFO";
        case SelectedPolicy::Lru:
            return "LRU";
        case SelectedPolicy::Priority:
            return "Priority";
        case SelectedPolicy::ImportanceAware:
            return "Importance-Aware";
    }
    return "Unknown";
}

InteractiveSimulator::InteractiveSimulator(
    std::istream& in,
    std::ostream& out,
    bool use_color,
    bool enable_post_simulation_prompt
) : reader_(in, out),
    out_(out),
    use_color_(use_color && (&out == &std::cout) && (::isatty(STDOUT_FILENO) != 0)),
    enable_post_simulation_prompt_(enable_post_simulation_prompt || ((&in == &std::cin) && (::isatty(STDIN_FILENO) != 0))),
    analytics_bridge_(std::make_shared<AnalyticsBridge>()) {}

std::string InteractiveSimulator::format_time_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    ::localtime_r(&tt, &tm_buf);
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf) != 0) {
        return std::string(buf);
    }
    return "00:00:00";
}

void InteractiveSimulator::print_header() {
    const std::string cyan = use_color_ ? "\033[1;36m" : "";
    const std::string bold = use_color_ ? "\033[1m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    out_ << "\n" << cyan
         << "================================================================================\n"
         << "  KERNOX 2.0 — ANDROID RESOURCE INTELLIGENCE SIMULATOR\n"
         << "  Device RAM: " << device_config_.total_ram_mb << " MB | Processes: "
         << processes_.size() << " | Policy: " << to_string(selected_policy_) << "\n"
         << "================================================================================"
         << reset << "\n";
}

void InteractiveSimulator::print_main_menu() {
    const std::string bold = use_color_ ? "\033[1m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    out_ << "  1. Configure Android Device\n"
         << "  2. Add New Process\n"
         << "  3. Edit Existing Process\n"
         << "  4. Remove Process\n"
         << "  5. View Process List\n"
         << "  6. Select Reclamation Policy\n"
         << "  7. Run Simulation\n"
         << "  8. Compare All Policies\n"
         << "  9. View Simulation History\n"
         << " 10. Export Results & Generate Analytics\n"
         << "  0. Exit Simulator\n"
         << "--------------------------------------------------------------------------------\n";
}

int InteractiveSimulator::run() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    while (true) {
        print_header();
        print_main_menu();

        uint32_t choice = 0;
        ReadStatus status = reader_.read_uint32(
            choice,
            "Select menu option [0-10]",
            std::nullopt,
            0,
            10,
            false
        );

        if (status == ReadStatus::Eof) {
            out_ << "\n[EOF detected: Exiting interactive simulator]\n";
            return 0;
        }

        if (status == ReadStatus::Cancelled || status == ReadStatus::Empty) {
            continue;
        }

        switch (choice) {
            case 0:
                out_ << "\n" << green
                     << "Exiting KERNOX Android Resource Simulator. Goodbye!"
                     << reset << "\n\n";
                return 0;
            case 1:
                handle_configure_device();
                break;
            case 2:
                handle_add_process();
                break;
            case 3:
                handle_edit_process();
                break;
            case 4:
                handle_remove_process();
                break;
            case 5:
                handle_view_processes();
                break;
            case 6:
                handle_select_policy();
                break;
            case 7:
                handle_run_simulation();
                break;
            case 8:
                handle_compare_policies();
                break;
            case 9:
                handle_view_history();
                break;
            case 10:
                handle_export_and_analytics();
                break;
            default:
                break;
        }
    }
}

void InteractiveSimulator::handle_configure_device() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string red = use_color_ ? "\033[1;31m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    out_ << "\n================================================================================\n"
         << "CONFIGURE SIMULATED ANDROID DEVICE\n"
         << "================================================================================\n"
         << "Current Configuration:\n"
         << "  * Total RAM Capacity : " << device_config_.total_ram_mb << " MB ("
         << std::fixed << std::setprecision(2)
         << (static_cast<double>(device_config_.total_ram_mb) / 1024.0) << " GiB)\n"
         << "  * Moderate Threshold : " << std::fixed << std::setprecision(1)
         << (device_config_.moderate_threshold_ratio * 100.0) << "% ("
         << static_cast<uint64_t>(static_cast<double>(device_config_.total_ram_mb) * device_config_.moderate_threshold_ratio) << " MB)\n"
         << "  * Critical Threshold : " << std::fixed << std::setprecision(1)
         << (device_config_.critical_threshold_ratio * 100.0) << "% ("
         << static_cast<uint64_t>(static_cast<double>(device_config_.total_ram_mb) * device_config_.critical_threshold_ratio) << " MB)\n"
         << "--------------------------------------------------------------------------------\n";

    uint64_t ram = 0;
    ReadStatus s1 = reader_.read_uint64(
        ram,
        "Total RAM capacity in MB",
        device_config_.total_ram_mb,
        1,
        1024ULL * 1024ULL
    );
    if (s1 != ReadStatus::Success) return;

    double mod = 0.0;
    ReadStatus s2 = reader_.read_double(
        mod,
        "Moderate pressure threshold ratio (e.g. 0.75)",
        device_config_.moderate_threshold_ratio,
        0.01,
        0.99
    );
    if (s2 != ReadStatus::Success) return;

    double crit = 0.0;
    ReadStatus s3 = reader_.read_double(
        crit,
        "Critical pressure threshold ratio (e.g. 0.90)",
        device_config_.critical_threshold_ratio,
        0.02,
        1.00
    );
    if (s3 != ReadStatus::Success) return;

    CustomDeviceConfiguration new_cfg{
        .total_ram_mb = ram,
        .moderate_threshold_ratio = mod,
        .critical_threshold_ratio = crit
    };

    auto val_res = validate_device_config(new_cfg);
    if (val_res.is_err()) {
        out_ << red << "  [ERROR] " << val_res.error().message << reset << "\n";
        return;
    }

    if (!processes_.empty()) {
        auto col_res = validate_process_collection(processes_, new_cfg);
        if (col_res.is_err()) {
            out_ << red << "  [ERROR] Incompatible with current process list: "
                 << col_res.error().message << reset << "\n";
            return;
        }
    }

    device_config_ = new_cfg;
    config_modified_since_run_ = true;
    out_ << green << "  [SUCCESS] Device configuration successfully updated." << reset << "\n";
}

void InteractiveSimulator::handle_add_process() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string red = use_color_ ? "\033[1;31m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    out_ << "\n================================================================================\n"
         << "ADD NEW SIMULATED APPLICATION PROCESS\n"
         << "================================================================================\n";

    std::string name;
    ReadStatus s_name = reader_.read_string(name, "Application process name (e.g. com.android.chrome)");
    if (s_name != ReadStatus::Success) return;

    uint64_t init_mem = 0;
    ReadStatus s_init = reader_.read_uint64(
        init_mem,
        "Initial resident memory in MB",
        128,
        1,
        device_config_.total_ram_mb
    );
    if (s_init != ReadStatus::Success) return;

    uint64_t req_mem = 0;
    ReadStatus s_req = reader_.read_uint64(
        req_mem,
        "Requested peak memory in MB",
        init_mem,
        init_mem,
        device_config_.total_ram_mb * 2
    );
    if (s_req != ReadStatus::Success) return;

    uint32_t prio = 0;
    ReadStatus s_prio = reader_.read_uint32(
        prio,
        "CPU scheduling priority (0 highest, 99 lowest)",
        20,
        0,
        99
    );
    if (s_prio != ReadStatus::Success) return;

    out_ << "Select Android Importance Category:\n"
         << "  1. Foreground (Top interactive app - protected under pressure)\n"
         << "  2. Visible    (Visible UI window / split-screen)\n"
         << "  3. Service    (Background service with notification)\n"
         << "  4. Background (Invisible background task)\n"
         << "  5. Cached     (Empty cached process - primary reclamation victim)\n";

    uint32_t imp_choice = 0;
    ReadStatus s_imp = reader_.read_uint32(imp_choice, "Importance Tier [1-5]", 4, 1, 5);
    if (s_imp != ReadStatus::Success) return;

    intel::ProcessImportance importance = intel::ProcessImportance::Background;
    switch (imp_choice) {
        case 1: importance = intel::ProcessImportance::Foreground; break;
        case 2: importance = intel::ProcessImportance::Visible; break;
        case 3: importance = intel::ProcessImportance::Service; break;
        case 4: importance = intel::ProcessImportance::Background; break;
        case 5: importance = intel::ProcessImportance::Cached; break;
        default: break;
    }

    uint64_t arr_tick = 0;
    ReadStatus s_arr = reader_.read_uint64(arr_tick, "Arrival tick in simulation", 0, 0);
    if (s_arr != ReadStatus::Success) return;

    uint64_t burst = 0;
    ReadStatus s_burst = reader_.read_uint64(burst, "CPU execution burst duration in ticks", 40, 1);
    if (s_burst != ReadStatus::Success) return;

    std::vector<CustomMemoryEvent> events;
    bool add_event = false;
    ReadStatus s_bev = reader_.read_bool(add_event, "Add dynamic memory demand event?", false);
    if (s_bev == ReadStatus::Success && add_event) {
        while (add_event) {
            uint64_t ev_tick = 0;
            ReadStatus s_et = reader_.read_uint64(
                ev_tick,
                "Event trigger tick",
                arr_tick,
                arr_tick
            );
            if (s_et != ReadStatus::Success) break;

            std::string delta_str;
            ReadStatus s_ds = reader_.read_string(
                delta_str,
                "Memory delta in MB (positive to allocate, negative to release)"
            );
            if (s_ds != ReadStatus::Success) break;

            int64_t delta_mb = 0;
            try {
                delta_mb = std::stoll(delta_str);
            } catch (...) {
                out_ << red << "  [Invalid delta value: '" << delta_str << "']" << reset << "\n";
                continue;
            }

            std::string reason;
            ReadStatus s_re = reader_.read_string(
                reason,
                "Diagnostic event description",
                "Dynamic memory request"
            );
            if (s_re != ReadStatus::Success) break;

            events.push_back(CustomMemoryEvent{
                .tick = ev_tick,
                .delta_mb = delta_mb,
                .reason = reason
            });

            ReadStatus s_more = reader_.read_bool(add_event, "Add another dynamic memory event?", false);
            if (s_more != ReadStatus::Success || !add_event) {
                break;
            }
        }
    }

    CustomProcessInput new_proc{
        .name = name,
        .cpu_priority = prio,
        .importance = importance,
        .initial_memory_mb = init_mem,
        .requested_memory_mb = req_mem,
        .cpu_burst_ticks = burst,
        .arrival_tick = arr_tick,
        .dynamic_events = std::move(events)
    };

    auto single_res = validate_process_input(new_proc);
    if (single_res.is_err()) {
        out_ << red << "  [ERROR] " << single_res.error().message << reset << "\n";
        return;
    }

    std::vector<CustomProcessInput> candidate_procs = processes_;
    candidate_procs.push_back(new_proc);

    auto col_res = validate_process_collection(candidate_procs, device_config_);
    if (col_res.is_err()) {
        out_ << red << "  [ERROR] " << col_res.error().message << reset << "\n";
        return;
    }

    processes_.push_back(std::move(new_proc));
    config_modified_since_run_ = true;
    out_ << green << "  [SUCCESS] Process '" << name << "' added successfully." << reset << "\n";
}

void InteractiveSimulator::print_process_table() {
    out_ << "\n=============================================================================================================\n"
         << "ID  | Process Name                   | Importance | Initial RAM | Req RAM | Priority | Arrival | Burst | Events\n"
         << "-------------------------------------------------------------------------------------------------------------\n";

    for (size_t i = 0; i < processes_.size(); ++i) {
        const auto& p = processes_[i];
        std::string imp_str = std::string(intel::to_string(p.importance));
        out_ << std::left << std::setw(4) << (i + 1)
             << "| " << std::left << std::setw(31) << p.name.substr(0, 31)
             << "| " << std::left << std::setw(11) << imp_str
             << "| " << std::right << std::setw(8) << p.initial_memory_mb << " MB "
             << "| " << std::right << std::setw(5) << p.requested_memory_mb << " MB "
             << "| " << std::right << std::setw(8) << p.cpu_priority << " "
             << "| " << std::right << std::setw(7) << p.arrival_tick << " "
             << "| " << std::right << std::setw(5) << p.cpu_burst_ticks << " "
             << "| " << std::right << std::setw(6) << p.dynamic_events.size() << "\n";
    }
    out_ << "=============================================================================================================\n";
}

void InteractiveSimulator::handle_edit_process() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string red = use_color_ ? "\033[1;31m" : "";
    const std::string yellow = use_color_ ? "\033[1;33m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (processes_.empty()) {
        out_ << yellow << "\n  [INFO] No processes available to edit. Add a process first (option 2)."
             << reset << "\n";
        return;
    }

    print_process_table();

    uint64_t sel_id = 0;
    ReadStatus s_sel = reader_.read_uint64(
        sel_id,
        "Select Process ID to edit",
        std::nullopt,
        1,
        processes_.size()
    );
    if (s_sel != ReadStatus::Success) return;

    size_t idx = sel_id - 1;
    const auto& current = processes_[idx];

    out_ << "\nEditing Process #" << sel_id << " (" << current.name << "):\n";

    std::string name;
    ReadStatus s_name = reader_.read_string(name, "Process name", current.name);
    if (s_name != ReadStatus::Success) return;

    uint64_t init_mem = 0;
    ReadStatus s_init = reader_.read_uint64(
        init_mem,
        "Initial resident memory in MB",
        current.initial_memory_mb,
        1,
        device_config_.total_ram_mb
    );
    if (s_init != ReadStatus::Success) return;

    uint64_t req_mem = 0;
    ReadStatus s_req = reader_.read_uint64(
        req_mem,
        "Requested memory in MB",
        current.requested_memory_mb,
        init_mem,
        device_config_.total_ram_mb * 2
    );
    if (s_req != ReadStatus::Success) return;

    uint32_t prio = 0;
    ReadStatus s_prio = reader_.read_uint32(
        prio,
        "CPU priority (0-99)",
        current.cpu_priority,
        0,
        99
    );
    if (s_prio != ReadStatus::Success) return;

    uint32_t cur_imp_choice = 4;
    switch (current.importance) {
        case intel::ProcessImportance::Foreground: cur_imp_choice = 1; break;
        case intel::ProcessImportance::Visible: cur_imp_choice = 2; break;
        case intel::ProcessImportance::Service: cur_imp_choice = 3; break;
        case intel::ProcessImportance::Background: cur_imp_choice = 4; break;
        case intel::ProcessImportance::Cached: cur_imp_choice = 5; break;
    }

    out_ << "Select Android Importance Category:\n"
         << "  1. Foreground  2. Visible  3. Service  4. Background  5. Cached\n";
    uint32_t imp_choice = 0;
    ReadStatus s_imp = reader_.read_uint32(
        imp_choice,
        "Importance Tier [1-5]",
        cur_imp_choice,
        1,
        5
    );
    if (s_imp != ReadStatus::Success) return;

    intel::ProcessImportance importance = intel::ProcessImportance::Background;
    switch (imp_choice) {
        case 1: importance = intel::ProcessImportance::Foreground; break;
        case 2: importance = intel::ProcessImportance::Visible; break;
        case 3: importance = intel::ProcessImportance::Service; break;
        case 4: importance = intel::ProcessImportance::Background; break;
        case 5: importance = intel::ProcessImportance::Cached; break;
        default: break;
    }

    uint64_t arr_tick = 0;
    ReadStatus s_arr = reader_.read_uint64(arr_tick, "Arrival tick", current.arrival_tick, 0);
    if (s_arr != ReadStatus::Success) return;

    uint64_t burst = 0;
    ReadStatus s_burst = reader_.read_uint64(burst, "CPU burst ticks", current.cpu_burst_ticks, 1);
    if (s_burst != ReadStatus::Success) return;

    CustomProcessInput edited = current;
    edited.name = name;
    edited.initial_memory_mb = init_mem;
    edited.requested_memory_mb = req_mem;
    edited.cpu_priority = prio;
    edited.importance = importance;
    edited.arrival_tick = arr_tick;
    edited.cpu_burst_ticks = burst;

    std::vector<CustomProcessInput> candidate_procs = processes_;
    candidate_procs[idx] = edited;

    auto col_res = validate_process_collection(candidate_procs, device_config_);
    if (col_res.is_err()) {
        out_ << red << "  [ERROR] " << col_res.error().message << reset << "\n";
        return;
    }

    processes_[idx] = std::move(edited);
    config_modified_since_run_ = true;
    out_ << green << "  [SUCCESS] Process #" << sel_id << " (" << name << ") updated successfully."
         << reset << "\n";
}

void InteractiveSimulator::handle_remove_process() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string yellow = use_color_ ? "\033[1;33m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (processes_.empty()) {
        out_ << yellow << "\n  [INFO] No processes available to remove." << reset << "\n";
        return;
    }

    print_process_table();

    uint64_t sel_id = 0;
    ReadStatus s_sel = reader_.read_uint64(
        sel_id,
        "Select Process ID to remove",
        std::nullopt,
        1,
        processes_.size()
    );
    if (s_sel != ReadStatus::Success) return;

    size_t idx = sel_id - 1;
    std::string target_name = processes_[idx].name;

    bool confirm = false;
    ReadStatus s_conf = reader_.read_bool(
        confirm,
        "Are you sure you want to remove process '" + target_name + "'?",
        false
    );
    if (s_conf != ReadStatus::Success || !confirm) {
        out_ << "  [Removal cancelled]\n";
        return;
    }

    processes_.erase(processes_.begin() + static_cast<std::ptrdiff_t>(idx));
    config_modified_since_run_ = true;
    out_ << green << "  [SUCCESS] Process '" << target_name << "' removed." << reset << "\n";
}

void InteractiveSimulator::handle_view_processes() {
    const std::string yellow = use_color_ ? "\033[1;33m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (processes_.empty()) {
        out_ << yellow << "\n  [INFO] Process list is currently empty. Use option 2 to add a process."
             << reset << "\n";
        return;
    }

    uint64_t total_init = 0;
    uint64_t tick0_init = 0;
    for (const auto& p : processes_) {
        total_init += p.initial_memory_mb;
        if (p.arrival_tick == 0) {
            tick0_init += p.initial_memory_mb;
        }
    }

    out_ << "\n=============================================================================================================\n"
         << "CURRENT SIMULATED PROCESS POPULATION (" << processes_.size() << " processes | Total Initial: "
         << total_init << " MB | Tick 0: " << tick0_init << " MB / " << device_config_.total_ram_mb << " MB RAM)\n"
         << "=============================================================================================================\n";

    print_process_table();

    out_ << "\n" << AsciiVisualizer::render_process_input_distribution(
        processes_,
        device_config_.total_ram_mb * 1024ULL * 1024ULL,
        22,
        use_color_
    );
}

void InteractiveSimulator::handle_select_policy() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    out_ << "\n================================================================================\n"
         << "SELECT MEMORY RECLAMATION POLICY\n"
         << "================================================================================\n"
         << "Current Active Policy: " << to_string(selected_policy_) << "\n\n"
         << "Available Policies:\n"
         << "  1. FIFO             - Reclaims oldest resident application first\n"
         << "  2. LRU              - Reclaims least recently scheduled application first\n"
         << "  3. Priority         - Reclaims application with lowest CPU urgency first\n"
         << "  4. Importance-Aware - Android Low Memory Killer (protects Foreground/Visible)\n"
         << "--------------------------------------------------------------------------------\n";

    uint32_t choice = 0;
    ReadStatus s_pol = reader_.read_uint32(
        choice,
        "Select policy [1-4]",
        static_cast<uint32_t>(selected_policy_),
        1,
        4
    );
    if (s_pol != ReadStatus::Success) return;

    selected_policy_ = static_cast<SelectedPolicy>(choice);
    out_ << green << "  [SUCCESS] Active reclamation policy set to '" << to_string(selected_policy_)
         << "'." << reset << "\n";
}

void InteractiveSimulator::handle_run_simulation() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string red = use_color_ ? "\033[1;31m" : "";
    const std::string yellow = use_color_ ? "\033[1;33m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (processes_.empty()) {
        out_ << red << "\n  [ERROR] Cannot execute simulation: No processes have been configured.\n"
             << "  Please add at least one application process (option 2) before running."
             << reset << "\n";
        return;
    }

    CustomWorkloadBuilder builder;
    builder.set_device_config(device_config_)
           .set_processes(processes_)
           .set_seed(seed_)
           .set_scenario_name("InteractiveCustomWorkload");

    auto wkl_res = builder.build();
    if (wkl_res.is_err()) {
        out_ << red << "  [ERROR] Workload validation error: " << wkl_res.error().message
             << reset << "\n";
        return;
    }
    const auto& workload = wkl_res.value();

    uint64_t init_total_mb = 0;
    for (const auto& p : processes_) {
        init_total_mb += p.initial_memory_mb;
    }
    double util_ratio = (device_config_.total_ram_mb > 0)
        ? (static_cast<double>(init_total_mb) / static_cast<double>(device_config_.total_ram_mb))
        : 0.0;

    out_ << "\n================================================================================\n"
         << "SIMULATION PRE-FLIGHT VERIFICATION\n"
         << "================================================================================\n"
         << "Device RAM Capacity : " << device_config_.total_ram_mb << " MB\n"
         << "Process Population  : " << processes_.size() << " processes\n"
         << "Total Initial RAM   : " << init_total_mb << " MB ("
         << std::fixed << std::setprecision(1) << (util_ratio * 100.0) << "% of total capacity)\n"
         << "Reclamation Policy  : " << to_string(selected_policy_) << "\n"
         << "Workload Fingerprint: " << workload.fingerprint << "\n"
         << "Random PRNG Seed    : " << seed_ << "\n"
         << "================================================================================\n";

    bool proceed = true;
    ReadStatus s_proc = reader_.read_bool(proceed, "Proceed with simulation run?", true);
    if (s_proc != ReadStatus::Success || !proceed) {
        out_ << "  [Simulation execution aborted]\n";
        return;
    }

    std::unique_ptr<reclaim::IReclamationPolicy> policy;
    switch (selected_policy_) {
        case SelectedPolicy::Fifo:
            policy = std::make_unique<reclaim::FifoReclamationPolicy>();
            break;
        case SelectedPolicy::Lru:
            policy = std::make_unique<reclaim::LruReclamationPolicy>();
            break;
        case SelectedPolicy::Priority:
            policy = std::make_unique<reclaim::PriorityReclamationPolicy>();
            break;
        case SelectedPolicy::ImportanceAware:
            policy = std::make_unique<reclaim::ImportanceAwareReclamationPolicy>();
            break;
    }

    experiment::ExperimentConfiguration exp_cfg{
        .thresholds = device_config_.to_pressure_thresholds()
    };

    auto run_res = experiment::ExperimentRunner::run_policy(workload, *policy, exp_cfg);
    if (run_res.is_err()) {
        out_ << red << "  [ERROR] Simulation runtime failure: " << run_res.error().message
             << reset << "\n";
        return;
    }

    const auto& res = run_res.value();
    last_result_ = res;
    latest_simulation_type_ = LatestSimulationType::SingleResult;
    config_modified_since_run_ = false;

    // Phase 3 Visualizations
    out_ << "\n" << AsciiVisualizer::render_memory_overview(
        res.metrics.peak_used_bytes,
        res.metrics.total_capacity_bytes,
        exp_cfg.thresholds,
        use_color_
    );

    out_ << "\n" << res.to_string() << "\n";

    if (!res.process_records.empty()) {
        out_ << "\n" << AsciiVisualizer::render_process_memory_distribution(
            res.process_records,
            res.metrics.total_capacity_bytes,
            20,
            use_color_
        );
    }

    if (!res.memory_timeline.empty()) {
        out_ << AsciiVisualizer::render_timeline_chart(
            res.memory_timeline,
            res.metrics.total_capacity_bytes,
            exp_cfg.thresholds,
            8,
            50,
            use_color_
        );
    }

    out_ << "\n" << AsciiVisualizer::render_reclamation_log(res.reclamation_log, use_color_);

    HistoryEntry entry;
    entry.sequence_id = next_history_id_++;
    entry.timestamp = format_time_now();
    entry.policy_name = std::string(to_string(selected_policy_));
    entry.workload_fingerprint = res.workload_fingerprint;
    entry.total_processes = res.metrics.total_processes;
    entry.processes_survived = res.metrics.processes_survived;
    entry.survival_rate = res.metrics.survival_rate;
    entry.foreground_survival_rate = res.metrics.foreground_survival_rate;
    entry.reclaimed_bytes = res.metrics.total_reclaimed_bytes;
    entry.peak_used_bytes = res.metrics.peak_used_bytes;
    entry.capacity_bytes = res.metrics.total_capacity_bytes;
    entry.average_utilization_ratio = res.metrics.average_utilization_ratio;
    entry.wall_clock_elapsed_microseconds = res.metrics.wall_clock_elapsed_microseconds;
    entry.is_comparison = false;
    entry.summary_text = res.to_string();
    entry.visual_overview = AsciiVisualizer::render_memory_overview(
        res.metrics.peak_used_bytes,
        res.metrics.total_capacity_bytes,
        exp_cfg.thresholds,
        false
    );
    entry.timeline_chart = AsciiVisualizer::render_timeline_chart(
        res.memory_timeline,
        res.metrics.total_capacity_bytes,
        exp_cfg.thresholds,
        8,
        50,
        false
    );
    entry.reclamation_log = AsciiVisualizer::render_reclamation_log(res.reclamation_log, false);

    history_.push_back(std::move(entry));
    out_ << green << "\n  [SUCCESS] Simulation run recorded into session history (#"
         << history_.back().sequence_id << ")." << reset << "\n";

    if (enable_post_simulation_prompt_) {
        bool export_now = false;
        ReadStatus s_exp = reader_.read_bool(
            export_now,
            "Export results and generate Python analytics report?",
            false
        );
        if (s_exp == ReadStatus::Success && export_now) {
            handle_export_and_analytics();
        }
    }
}

void InteractiveSimulator::handle_compare_policies() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string red = use_color_ ? "\033[1;31m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (processes_.empty()) {
        out_ << red << "\n  [ERROR] Cannot execute comparison: No processes have been configured.\n"
             << "  Please add at least one application process (option 2) before comparing."
             << reset << "\n";
        return;
    }

    CustomWorkloadBuilder builder;
    builder.set_device_config(device_config_)
           .set_processes(processes_)
           .set_seed(seed_)
           .set_scenario_name("InteractiveCustomComparison");

    auto wkl_res = builder.build();
    if (wkl_res.is_err()) {
        out_ << red << "  [ERROR] Workload validation error: " << wkl_res.error().message
             << reset << "\n";
        return;
    }
    const auto& workload = wkl_res.value();

    out_ << "\n================================================================================\n"
         << "BENCHMARK PRE-FLIGHT: COMPARISON ACROSS ALL 4 POLICIES\n"
         << "================================================================================\n"
         << "Workload Fingerprint: " << workload.fingerprint << "\n"
         << "Process Count       : " << processes_.size() << " processes\n"
         << "Memory Capacity     : " << device_config_.total_ram_mb << " MB\n"
         << "Policies Evaluated  : FIFO, LRU, Priority, Importance-Aware\n"
         << "Isolation Guarantee : Fresh identical state reconstructed per policy run\n"
         << "================================================================================\n";

    bool proceed = true;
    ReadStatus s_proc = reader_.read_bool(proceed, "Execute 4-policy comparison?", true);
    if (s_proc != ReadStatus::Success || !proceed) {
        out_ << "  [Comparison aborted]\n";
        return;
    }

    experiment::ExperimentConfiguration exp_cfg{
        .thresholds = device_config_.to_pressure_thresholds()
    };

    auto comp_res = experiment::ExperimentRunner::compare_all_policies(workload, exp_cfg);
    if (comp_res.is_err()) {
        out_ << red << "  [ERROR] Comparison execution failed: " << comp_res.error().message
             << reset << "\n";
        return;
    }

    const auto& comp = comp_res.value();
    last_comparison_ = comp;
    latest_simulation_type_ = LatestSimulationType::Comparison;
    config_modified_since_run_ = false;

    out_ << "\n" << AsciiVisualizer::render_comparison_matrix(comp, use_color_) << "\n"
         << comp.to_summary_string() << "\n";

    HistoryEntry entry;
    entry.sequence_id = next_history_id_++;
    entry.timestamp = format_time_now();
    entry.policy_name = "All 4 Policies (Comparison)";
    entry.workload_fingerprint = comp.workload_fingerprint;
    entry.total_processes = processes_.size();
    entry.capacity_bytes = comp.total_capacity_bytes;
    entry.is_comparison = true;
    entry.summary_text = comp.to_table_string() + "\n" + comp.to_summary_string();
    entry.comparison_matrix = AsciiVisualizer::render_comparison_matrix(comp, false);

    history_.push_back(std::move(entry));
    out_ << green << "\n  [SUCCESS] Comparative benchmark recorded into session history (#"
         << history_.back().sequence_id << ")." << reset << "\n";

    if (enable_post_simulation_prompt_) {
        bool export_now = false;
        ReadStatus s_exp = reader_.read_bool(
            export_now,
            "Export results and generate Python analytics report?",
            false
        );
        if (s_exp == ReadStatus::Success && export_now) {
            handle_export_and_analytics();
        }
    }
}

void InteractiveSimulator::handle_view_history() {
    const std::string yellow = use_color_ ? "\033[1;33m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (history_.empty()) {
        out_ << yellow << "\n  [INFO] No simulations have been executed in this session yet."
             << reset << "\n";
        return;
    }

    out_ << "\n=============================================================================================================\n"
         << "SESSION SIMULATION HISTORY (" << history_.size() << " runs executed)\n"
         << "=============================================================================================================\n"
         << "#   | Timestamp | Policy          | Fingerprint      | Procs | Survived | Reclaimed | FG Surv | Peak Util\n"
         << "-------------------------------------------------------------------------------------------------------------\n";

    for (const auto& h : history_) {
        out_ << std::left << std::setw(4) << h.sequence_id
             << "| " << std::left << std::setw(10) << h.timestamp
             << "| " << std::left << std::setw(16) << h.policy_name.substr(0, 16)
             << "| " << std::left << std::setw(17) << h.workload_fingerprint.substr(0, 17)
             << "| " << std::right << std::setw(5) << h.total_processes << " ";

        if (h.is_comparison) {
            out_ << "| " << std::right << std::setw(8) << "All (4)  "
                 << "| " << std::right << std::setw(9) << "-"
                 << "| " << std::right << std::setw(7) << "-"
                 << "| " << std::right << std::setw(9) << "-\n";
        } else {
            double rec_mb = static_cast<double>(h.reclaimed_bytes) / (1024.0 * 1024.0);
            double peak_ratio = (h.capacity_bytes > 0)
                ? (static_cast<double>(h.peak_used_bytes) / static_cast<double>(h.capacity_bytes))
                : 0.0;

            out_ << "| " << std::right << std::setw(3) << h.processes_survived << " ("
                 << std::fixed << std::setprecision(0) << (h.survival_rate * 100.0) << "%) "
                 << "| " << std::right << std::setw(6) << std::fixed << std::setprecision(1) << rec_mb << " MB "
                 << "| " << std::right << std::setw(6) << std::fixed << std::setprecision(1) << (h.foreground_survival_rate * 100.0) << "% "
                 << "| " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << (peak_ratio * 100.0) << "%\n";
        }
    }
    out_ << "=============================================================================================================\n";

    uint64_t view_id = 0;
    ReadStatus s_view = reader_.read_uint64(
        view_id,
        "Enter Session # to inspect details (0 to return)",
        0,
        0,
        history_.back().sequence_id
    );

    if (s_view == ReadStatus::Success && view_id > 0) {
        for (const auto& h : history_) {
            if (h.sequence_id == view_id) {
                out_ << "\n" << h.summary_text << "\n";
                if (!h.is_comparison) {
                    if (!h.visual_overview.empty()) {
                        out_ << "\n" << h.visual_overview;
                    }
                    if (!h.timeline_chart.empty()) {
                        out_ << h.timeline_chart;
                    }
                    if (!h.reclamation_log.empty()) {
                        out_ << "\n" << h.reclamation_log;
                    }
                } else {
                    if (!h.comparison_matrix.empty()) {
                        out_ << "\n" << h.comparison_matrix;
                    }
                }
                break;
            }
        }
    }
}

void InteractiveSimulator::handle_export_and_analytics() {
    const std::string green = use_color_ ? "\033[1;32m" : "";
    const std::string red = use_color_ ? "\033[1;31m" : "";
    const std::string yellow = use_color_ ? "\033[1;33m" : "";
    const std::string reset = use_color_ ? "\033[0m" : "";

    if (!last_result_.has_value() && !last_comparison_.has_value()) {
        out_ << red << "\n  [ERROR] No simulation results available to export.\n"
             << "  Please run a single simulation (option 7) or 4-policy comparison (option 8) first."
             << reset << "\n";
        return;
    }

    out_ << "\n================================================================================\n"
         << "EXPORT SIMULATION RESULTS & GENERATE PYTHON ANALYTICS\n"
         << "================================================================================\n";

    if (latest_simulation_type_ == LatestSimulationType::SingleResult && last_result_.has_value()) {
        out_ << "Latest Simulation Run : Single Policy Simulation (" << last_result_->policy_name << ")\n"
             << "Workload Fingerprint  : " << last_result_->workload_fingerprint << "\n"
             << "Scenario Name         : " << last_result_->scenario_name << " (Seed: " << last_result_->seed << ")\n";
    } else if (latest_simulation_type_ == LatestSimulationType::Comparison && last_comparison_.has_value()) {
        out_ << "Latest Simulation Run : 4-Policy Comparative Benchmark\n"
             << "Workload Fingerprint  : " << last_comparison_->workload_fingerprint << "\n"
             << "Scenario Name         : " << last_comparison_->scenario_name << " (Seed: " << last_comparison_->seed << ")\n";
    }

    if (config_modified_since_run_) {
        out_ << yellow
             << "\n  [NOTICE] Workload/device configuration has changed since the last simulation.\n"
             << "           Export will use previously recorded simulation results, not pending edits.\n"
             << reset;
    }
    out_ << "--------------------------------------------------------------------------------\n";

    uint32_t target_choice = 1;
    if (last_result_.has_value() && last_comparison_.has_value()) {
        uint32_t default_target = (latest_simulation_type_ == LatestSimulationType::Comparison) ? 2 : 1;
        out_ << "Select Experiment Data to Export:\n"
             << "  1. Single Policy Simulation (" << last_result_->policy_name << ")"
             << (latest_simulation_type_ == LatestSimulationType::SingleResult ? " [LATEST RUN]" : " [EARLIER RUN]") << "\n"
             << "  2. 4-Policy Comparison Benchmark"
             << (latest_simulation_type_ == LatestSimulationType::Comparison ? " [LATEST RUN]" : " [EARLIER RUN]") << "\n";

        ReadStatus s_tgt = reader_.read_uint32(
            target_choice,
            "Target to export [1-2]",
            default_target,
            1,
            2
        );
        if (s_tgt != ReadStatus::Success) return;
    } else if (last_comparison_.has_value()) {
        target_choice = 2;
        out_ << "Target: 4-Policy Comparison Benchmark\n";
    } else {
        target_choice = 1;
        out_ << "Target: Single Policy Simulation (" << last_result_->policy_name << ")\n";
    }

    std::string default_json = (target_choice == 1)
        ? "artifacts/experiments/interactive_simulation.json"
        : "artifacts/experiments/interactive_comparison.json";
    std::string default_out = (target_choice == 1)
        ? "artifacts/analytics/interactive_simulation"
        : "artifacts/analytics/interactive_comparison";

    std::string json_path;
    ReadStatus s_json = reader_.read_string(
        json_path,
        "JSON export destination path",
        default_json
    );
    if (s_json != ReadStatus::Success) return;

    // Fix 1: Check whether requested destination file already exists
    struct stat st{};
    bool json_exists = false;
    if (::stat(json_path.c_str(), &st) == 0) {
        json_exists = true;
    } else if (errno != ENOENT) {
        out_ << red << "  [ERROR] Cannot access destination path '" << json_path
             << "': " << std::strerror(errno) << reset << "\n";
        return;
    }

    if (json_exists) {
        bool confirm_overwrite = false;
        out_ << yellow << "  [WARNING] Destination file already exists: " << json_path << reset << "\n";
        ReadStatus s_ovr = reader_.read_bool(
            confirm_overwrite,
            "Overwrite existing JSON export?",
            false
        );
        if (s_ovr != ReadStatus::Success || !confirm_overwrite) {
            out_ << "  [Export cancelled: Existing JSON file preserved]\n";
            return;
        }
    }

    bool run_analytics = true;
    ReadStatus s_ana = reader_.read_bool(
        run_analytics,
        "Generate analytical charts & markdown report with Python?",
        true
    );
    if (s_ana != ReadStatus::Success) return;

    std::string output_dir = default_out;
    if (run_analytics) {
        ReadStatus s_dir = reader_.read_string(
            output_dir,
            "Analytics report & charts output directory",
            default_out
        );
        if (s_dir != ReadStatus::Success) return;
    }

    if (!analytics_bridge_) {
        analytics_bridge_ = std::make_shared<AnalyticsBridge>();
    }

    out_ << "\nProcessing export...\n";

    core::Result<AnalyticsInvocationResult> export_res = (target_choice == 1)
        ? analytics_bridge_->export_and_analyze(*last_result_, json_path, output_dir, run_analytics)
        : analytics_bridge_->export_and_analyze(*last_comparison_, json_path, output_dir, run_analytics);

    if (export_res.is_err()) {
        out_ << red << "  [ERROR] Export failed: " << export_res.error().message << reset << "\n";
        return;
    }

    const auto& inv = export_res.value();

    out_ << "\n================================================================================\n"
         << "SIMULATION EXPORT & ANALYTICS RECEIPT\n"
         << "================================================================================\n";

    if (inv.json_exported) {
        out_ << green << "  * Experiment JSON   : [SUCCESS] " << inv.exported_json_path << reset << "\n";
    }

    if (run_analytics) {
        if (inv.success) {
            out_ << green << "  * Python Analytics  : [SUCCESS] Pipeline completed (exit code 0)\n"
                 << "  * Markdown Report   : " << inv.report_markdown_path << "\n"
                 << "  * Visual Figures    : " << inv.generated_charts.size() << " figure(s) generated in "
                 << inv.output_directory << "\n" << reset;
            for (const auto& ch : inv.generated_charts) {
                out_ << "      - " << ch << "\n";
            }
        } else {
            out_ << yellow << "  * Python Analytics  : [NON-FATAL ERROR / SKIPPED]\n"
                 << "    Diagnostic: " << inv.error_message << "\n"
                 << "  * Experiment JSON   : Safely preserved at " << inv.exported_json_path << "\n"
                 << "  * Manual Command    : To generate the report manually, run:\n"
                 << "    python3 -m kernox_analytics.cli analyze --input \"" << inv.exported_json_path
                 << "\" --output \"" << inv.output_directory << "\"\n"
                 << reset;
        }
    } else {
        out_ << "  * Python Analytics  : [SKIPPED by user request]\n"
             << "  * Manual Generation : python3 -m kernox_analytics.cli analyze --input \""
             << inv.exported_json_path << "\" --output \"" << default_out << "\"\n";
    }
    out_ << "================================================================================\n";
}

} // namespace kernox::simulator
