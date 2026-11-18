#include "kernox/modules/adb/bridge.hpp"
#include "kernox/modules/adb/parser.hpp"
#include "kernox/modules/adb/service.hpp"
#include "kernox/modules/adb/types.hpp"
#include "test_harness.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace kernox::adb;
using namespace kernox::core;

namespace {

std::string read_fixture(const std::string& filename) {
    std::vector<std::string> paths = {
#ifdef KERNOX_TEST_FIXTURE_DIR
        std::string(KERNOX_TEST_FIXTURE_DIR) + "/" + filename,
#endif
        "tests/fixtures/adb/" + filename,
        "../tests/fixtures/adb/" + filename,
        "../../tests/fixtures/adb/" + filename,
        "../../../tests/fixtures/adb/" + filename,
        "../../../../tests/fixtures/adb/" + filename
    };

    for (const auto& path : paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }
    std::cerr << "Could not open fixture file: " << filename << "\n";
    return "";
}

} // namespace

static int test_device_discovery_parsing() {
    std::cout << "Running test_device_discovery_parsing...\n";

    // 1. Empty devices listing
    std::string empty_txt = read_fixture("devices_empty.txt");
    auto res_empty = AdbParser::parse_devices(empty_txt);
    KERNOX_ASSERT_TRUE(res_empty.is_ok());
    KERNOX_ASSERT_EQ(res_empty.value().size(), 0u);

    // 2. Single online emulator device
    std::string single_txt = read_fixture("devices_single.txt");
    auto res_single = AdbParser::parse_devices(single_txt);
    KERNOX_ASSERT_TRUE(res_single.is_ok());
    KERNOX_ASSERT_EQ(res_single.value().size(), 1u);
    const auto& dev1 = res_single.value()[0];
    KERNOX_ASSERT_EQ(dev1.serial, "emulator-5554");
    KERNOX_ASSERT_TRUE(dev1.state == AdbConnectionState::Device);
    KERNOX_ASSERT_TRUE(dev1.is_usable());
    KERNOX_ASSERT_EQ(dev1.product, "sdk_gphone64_x86_64");
    KERNOX_ASSERT_EQ(dev1.model, "sdk_gphone64_x86_64");
    KERNOX_ASSERT_EQ(dev1.transport_id, "1");

    // 3. Multiple devices
    std::string multi_txt = read_fixture("devices_multiple.txt");
    auto res_multi = AdbParser::parse_devices(multi_txt);
    KERNOX_ASSERT_TRUE(res_multi.is_ok());
    KERNOX_ASSERT_EQ(res_multi.value().size(), 2u);
    KERNOX_ASSERT_EQ(res_multi.value()[0].serial, "emulator-5554");
    KERNOX_ASSERT_EQ(res_multi.value()[1].serial, "2A231JEHN04123");
    KERNOX_ASSERT_EQ(res_multi.value()[1].model, "Pixel_7");
    KERNOX_ASSERT_EQ(res_multi.value()[1].transport_id, "2");

    // 4. Unauthorized device
    std::string unauth_txt = read_fixture("devices_unauthorized.txt");
    auto res_unauth = AdbParser::parse_devices(unauth_txt);
    KERNOX_ASSERT_TRUE(res_unauth.is_ok());
    KERNOX_ASSERT_EQ(res_unauth.value().size(), 1u);
    const auto& dev_unauth = res_unauth.value()[0];
    KERNOX_ASSERT_EQ(dev_unauth.serial, "R58M41ABCDE");
    KERNOX_ASSERT_TRUE(dev_unauth.state == AdbConnectionState::Unauthorized);
    KERNOX_ASSERT_FALSE(dev_unauth.is_usable());

    // 5. Offline device
    std::string off_txt = read_fixture("devices_offline.txt");
    auto res_off = AdbParser::parse_devices(off_txt);
    KERNOX_ASSERT_TRUE(res_off.is_ok());
    KERNOX_ASSERT_EQ(res_off.value().size(), 1u);
    const auto& dev_off = res_off.value()[0];
    KERNOX_ASSERT_EQ(dev_off.serial, "192.168.1.105:5555");
    KERNOX_ASSERT_TRUE(dev_off.state == AdbConnectionState::Offline);
    KERNOX_ASSERT_FALSE(dev_off.is_usable());

    // 6. Output with adb daemon startup logs
    std::string daemon_txt =
        "* daemon not running; starting now at tcp:5037 *\n"
        "* daemon started successfully *\n"
        "List of devices attached\n"
        "emulator-5554 device\n";
    auto res_daemon = AdbParser::parse_devices(daemon_txt);
    KERNOX_ASSERT_TRUE(res_daemon.is_ok());
    KERNOX_ASSERT_EQ(res_daemon.value().size(), 1u);
    KERNOX_ASSERT_EQ(res_daemon.value()[0].serial, "emulator-5554");

    return 0;
}

static int test_device_properties_parsing() {
    std::cout << "Running test_device_properties_parsing...\n";

    // 1. Clean getprop output
    std::string prop1 = read_fixture("getprop_pixel.txt");
    KERNOX_ASSERT_EQ(AdbParser::parse_getprop(prop1), "Pixel 7");

    // 2. Bracketed getprop output (common in raw adb shell getprop dump)
    std::string bracketed = "  [Pixel 7 Pro] \r\n";
    KERNOX_ASSERT_EQ(AdbParser::parse_getprop(bracketed), "Pixel 7 Pro");

    // 3. String helper checks
    KERNOX_ASSERT_EQ(to_string(AdbConnectionState::Device), "device");
    KERNOX_ASSERT_EQ(to_string(AdbConnectionState::Unauthorized), "unauthorized");
    KERNOX_ASSERT_EQ(to_string(AdbConnectionState::Offline), "offline");

    auto p1 = parse_connection_state("device");
    KERNOX_ASSERT_TRUE(p1.has_value());
    KERNOX_ASSERT_TRUE(*p1 == AdbConnectionState::Device);

    auto p2 = parse_connection_state("unauthorized");
    KERNOX_ASSERT_TRUE(p2.has_value());
    KERNOX_ASSERT_TRUE(*p2 == AdbConnectionState::Unauthorized);

    auto p_inv = parse_connection_state("random_unrecognized_state");
    KERNOX_ASSERT_TRUE(p_inv.has_value());
    KERNOX_ASSERT_TRUE(*p_inv == AdbConnectionState::Unknown);

    // 4. Byte parsing utility
    auto b1 = AdbParser::parse_formatted_bytes("1024 kB");
    KERNOX_ASSERT_TRUE(b1.is_ok());
    KERNOX_ASSERT_EQ(b1.value(), 1024ULL * 1024);

    auto b2 = AdbParser::parse_formatted_bytes("7,856,128K");
    KERNOX_ASSERT_TRUE(b2.is_ok());
    KERNOX_ASSERT_EQ(b2.value(), 7856128ULL * 1024);

    auto b3 = AdbParser::parse_formatted_bytes("4 MB");
    KERNOX_ASSERT_TRUE(b3.is_ok());
    KERNOX_ASSERT_EQ(b3.value(), 4ULL * 1024 * 1024);

    auto b_bad = AdbParser::parse_formatted_bytes("not_numeric kB");
    KERNOX_ASSERT_TRUE(b_bad.is_err());

    return 0;
}

static int test_proc_meminfo_parsing() {
    std::cout << "Running test_proc_meminfo_parsing...\n";

    // 1. Standard /proc/meminfo
    std::string mem_std = read_fixture("meminfo_standard.txt");
    auto res_std = AdbParser::parse_proc_meminfo(mem_std);
    KERNOX_ASSERT_TRUE(res_std.is_ok());
    const auto& s = res_std.value();

    KERNOX_ASSERT_EQ(s.source, "android_adb_observation");
    KERNOX_ASSERT_TRUE(s.total_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.total_ram_bytes, 7856128ULL * 1024);

    KERNOX_ASSERT_TRUE(s.free_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.free_ram_bytes, 960000ULL * 1024);

    KERNOX_ASSERT_TRUE(s.available_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.available_ram_bytes, 3214560ULL * 1024);

    KERNOX_ASSERT_TRUE(s.buffers_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.buffers_bytes, 124560ULL * 1024);

    KERNOX_ASSERT_TRUE(s.cached_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.cached_bytes, 2254560ULL * 1024);

    KERNOX_ASSERT_TRUE(s.swap_total_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.swap_total_bytes, 2097148ULL * 1024);

    KERNOX_ASSERT_TRUE(s.swap_free_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.swap_free_bytes, 1116748ULL * 1024);

    // Used RAM should be Total - Available
    KERNOX_ASSERT_TRUE(s.used_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*s.used_ram_bytes, (7856128ULL - 3214560ULL) * 1024);

    // 2. Kernel without MemAvailable (older Linux / Android)
    std::string mem_no_avail = read_fixture("meminfo_no_available.txt");
    auto res_no_avail = AdbParser::parse_proc_meminfo(mem_no_avail);
    KERNOX_ASSERT_TRUE(res_no_avail.is_ok());
    const auto& s2 = res_no_avail.value();

    KERNOX_ASSERT_EQ(*s2.total_ram_bytes, 2048000ULL * 1024);
    KERNOX_ASSERT_FALSE(s2.available_ram_bytes.has_value()); // Explicit representation of missing field!
    KERNOX_ASSERT_TRUE(s2.used_ram_bytes.has_value());
    // Computed from free (300MB) + buffers (50MB) + cached (650MB) = 1000MB -> Used = 1048MB
    KERNOX_ASSERT_EQ(*s2.used_ram_bytes, (2048000ULL - (300000ULL + 50000ULL + 650000ULL)) * 1024);

    // 3. Malformed /proc/meminfo without valid MemTotal
    std::string mem_mal = read_fixture("meminfo_malformed.txt");
    auto res_mal = AdbParser::parse_proc_meminfo(mem_mal);
    KERNOX_ASSERT_TRUE(res_mal.is_err());
    KERNOX_ASSERT_EQ(res_mal.error().code, ErrorCode::InvalidArgument);

    return 0;
}

static int test_dumpsys_meminfo_parsing() {
    std::cout << "Running test_dumpsys_meminfo_parsing...\n";

    // 1. Dumpsys RAM Summary
    std::string summary_txt = read_fixture("dumpsys_meminfo_summary.txt");
    auto snap_res = AdbParser::parse_dumpsys_meminfo_summary(summary_txt);
    KERNOX_ASSERT_TRUE(snap_res.is_ok());
    const auto& snap = snap_res.value();

    KERNOX_ASSERT_TRUE(snap.total_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*snap.total_ram_bytes, 7856128ULL * 1024);

    KERNOX_ASSERT_TRUE(snap.free_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*snap.free_ram_bytes, 3214560ULL * 1024);

    KERNOX_ASSERT_TRUE(snap.used_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*snap.used_ram_bytes, 4120400ULL * 1024);

    KERNOX_ASSERT_TRUE(snap.lost_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*snap.lost_ram_bytes, 521168ULL * 1024);

    KERNOX_ASSERT_TRUE(snap.zram_physical_used_bytes.has_value());
    KERNOX_ASSERT_EQ(*snap.zram_physical_used_bytes, 340120ULL * 1024);

    KERNOX_ASSERT_TRUE(snap.swap_total_bytes.has_value());
    KERNOX_ASSERT_EQ(*snap.swap_total_bytes, 2097148ULL * 1024);

    // 2. Dumpsys Process Listings
    std::string procs_txt = read_fixture("dumpsys_meminfo_processes.txt");
    auto procs_res = AdbParser::parse_dumpsys_meminfo_processes(procs_txt);
    KERNOX_ASSERT_TRUE(procs_res.is_ok());
    const auto& procs = procs_res.value();

    KERNOX_ASSERT_TRUE(!procs.empty());

    // Verify critical PID separation: real Android PID is type AndroidPid
    bool found_surfaceflinger = false;
    bool found_launcher = false;
    bool found_cached = false;

    for (const auto& p : procs) {
        if (p.pid == 612u) {
            found_surfaceflinger = true;
            KERNOX_ASSERT_EQ(p.process_name, "surfaceflinger");
            KERNOX_ASSERT_TRUE(p.pss_bytes.has_value());
            KERNOX_ASSERT_EQ(*p.pss_bytes, 41200ULL * 1024);
        } else if (p.pid == 3100u) {
            found_launcher = true;
            KERNOX_ASSERT_EQ(p.process_name, "com.google.android.apps.nexuslauncher");
            KERNOX_ASSERT_TRUE(p.pss_bytes.has_value());
            KERNOX_ASSERT_EQ(*p.pss_bytes, 150000ULL * 1024);
            KERNOX_ASSERT_TRUE(p.normalized_importance.has_value());
            KERNOX_ASSERT_EQ(*p.normalized_importance, "Foreground");
        } else if (p.pid == 5200u) {
            found_cached = true;
            KERNOX_ASSERT_EQ(p.process_name, "com.example.cachedgallery");
            KERNOX_ASSERT_TRUE(p.normalized_importance.has_value());
            KERNOX_ASSERT_EQ(*p.normalized_importance, "Cached");
        }
    }

    KERNOX_ASSERT_TRUE(found_surfaceflinger);
    KERNOX_ASSERT_TRUE(found_launcher);
    KERNOX_ASSERT_TRUE(found_cached);

    // 3. Activity processes parsing
    std::string act_txt = read_fixture("dumpsys_activity_processes.txt");
    auto act_res = AdbParser::parse_dumpsys_activity_processes(act_txt);
    KERNOX_ASSERT_TRUE(act_res.is_ok());
    const auto& act_procs = act_res.value();
    KERNOX_ASSERT_EQ(act_procs.size(), 3u);

    KERNOX_ASSERT_EQ(act_procs[0].pid, 1420u);
    KERNOX_ASSERT_EQ(act_procs[0].process_name, "com.android.systemui");
    KERNOX_ASSERT_TRUE(act_procs[0].oom_score_adj.has_value());
    KERNOX_ASSERT_EQ(*act_procs[0].oom_score_adj, 0);

    KERNOX_ASSERT_EQ(act_procs[1].pid, 3100u);
    KERNOX_ASSERT_TRUE(act_procs[1].oom_score_adj.has_value());
    KERNOX_ASSERT_EQ(*act_procs[1].oom_score_adj, 100);

    KERNOX_ASSERT_EQ(act_procs[2].pid, 5200u);
    KERNOX_ASSERT_TRUE(act_procs[2].oom_score_adj.has_value());
    KERNOX_ASSERT_EQ(*act_procs[2].oom_score_adj, 905);

    return 0;
}

static int test_safety_and_mock_execution() {
    std::cout << "Running test_safety_and_mock_execution...\n";

    // 1. Positive Allowlist Validation - Every Allowed Command Form
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"devices"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"devices", "-l"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"get-state"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "getprop"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "ro.product.model"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "ro.product.manufacturer"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "ro.build.version.release"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "ro.build.version.sdk"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "meminfo"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "meminfo", "com.android.chrome"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "meminfo", "system"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "meminfo", "-s"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "meminfo", "-d"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "meminfo", "-c"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "activity", "processes"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "ps"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "ps", "-A"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "ps", "-A", "-o", "PID,NAME,PSS,RSS"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"shell", "ps", "-A", "-o", "PID,USER,%CPU,%MEM,VSZ,RSS,TTY,STAT,START,TIME,COMMAND"}));

    // 2. Device-Selected Variations with Valid Serials
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "devices"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "devices", "-l"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "get-state"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "getprop"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "getprop", "ro.product.model"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "dumpsys", "meminfo"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "dumpsys", "meminfo", "com.android.chrome"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "dumpsys", "activity", "processes"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "cat", "/proc/meminfo"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "ps"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "ps", "-A"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554", "shell", "ps", "-A", "-o", "PID,NAME,PSS,RSS"}));

    // Hardware serial format (e.g. Pixel 7) and TCP/IP serial format
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "2A231JEHN04123", "shell", "cat", "/proc/meminfo"}));
    KERNOX_ASSERT_TRUE(AdbBridge::is_safe_read_only_command({"-s", "192.168.1.105:5555", "shell", "dumpsys", "meminfo"}));

    // 3. Rejected Mutating and Destructive Commands
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "kill", "-9", "1000"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "killall", "zygote"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "am", "force-stop", "com.example.app"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "pm", "clear", "com.example.app"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "pm", "uninstall", "com.example.app"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "setprop", "sys.test", "1"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "su"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "su", "-c", "id"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "rm", "-rf", "/data"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "rmdir", "/cache"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dd", "if=/dev/zero", "of=/dev/block"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "mkfs", "/dev/block"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "format"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "wipe"}));

    // 4. Rejected Unlisted ADB Subcommands
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"forward", "tcp:8080", "tcp:8080"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"reverse", "tcp:8080", "tcp:8080"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"push", "test.txt", "/sdcard/"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"pull", "/sdcard/test.txt", "test.txt"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"install", "app.apk"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"uninstall", "com.example.app"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"reboot"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"reboot", "bootloader"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"remount"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"root"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"unroot"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"tcpip", "5555"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"connect", "192.168.1.50:5555"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"disconnect"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"sync"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"logcat"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"bugreport"}));

    // 5. Rejected Arbitrary Shell Commands and Unauthorized Targets
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "ls"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "echo", "hello"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "id"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "whoami"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "sh"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/system/build.prop"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/data/system/packages.xml"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "battery"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "power"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "cpuinfo"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "activity", "broadcasts"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "ps", "-ef"}));

    // 6. Shell Metacharacter and Injection Attempts
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"devices", ";", "rm"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"devices", "&"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo; id"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo", "&&", "id"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo | grep Mem"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo > /sdcard/out"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "ro.product.model; rm -rf /"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "$USER"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "getprop", "`id`"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo\nid"}));

    // 7. Malformed Serial Arguments and Command Sequences
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"-s"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"-s", "emulator-5554"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"-s", "-d", "devices"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"-s", "bad;serial", "devices"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"-s", "bad serial with space", "devices"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"-s", std::string(70, 'a'), "devices"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"devices", "-l", "extra"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"get-state", "extra"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "cat", "/proc/meminfo", "extra"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "activity"}));
    KERNOX_ASSERT_FALSE(AdbBridge::is_safe_read_only_command({"shell", "dumpsys", "activity", "processes", "extra"}));

    // 8. Mock command runner setup & Direct Library API Enforcement
    auto mock_runner = std::make_shared<MockCommandRunner>();
    AdbBridge bridge(mock_runner);

    // Attempting unlisted or dangerous commands through bridge must be blocked immediately with PermissionDenied
    auto reject_kill = bridge.run_adb({"shell", "kill", "-9", "1234"});
    KERNOX_ASSERT_TRUE(reject_kill.is_err());
    KERNOX_ASSERT_EQ(reject_kill.error().code, ErrorCode::PermissionDenied);

    auto reject_fwd = bridge.run_adb({"forward", "tcp:8080", "tcp:8080"});
    KERNOX_ASSERT_TRUE(reject_fwd.is_err());
    KERNOX_ASSERT_EQ(reject_fwd.error().code, ErrorCode::PermissionDenied);

    auto reject_push = bridge.run_adb({"push", "malicious.bin", "/data/local/tmp"});
    KERNOX_ASSERT_TRUE(reject_push.is_err());
    KERNOX_ASSERT_EQ(reject_push.error().code, ErrorCode::PermissionDenied);

    auto reject_install = bridge.run_adb({"install", "app.apk"});
    KERNOX_ASSERT_TRUE(reject_install.is_err());
    KERNOX_ASSERT_EQ(reject_install.error().code, ErrorCode::PermissionDenied);

    auto reject_reboot = bridge.run_adb({"reboot"});
    KERNOX_ASSERT_TRUE(reject_reboot.is_err());
    KERNOX_ASSERT_EQ(reject_reboot.error().code, ErrorCode::PermissionDenied);

    auto reject_su = bridge.run_adb({"shell", "su"});
    KERNOX_ASSERT_TRUE(reject_su.is_err());
    KERNOX_ASSERT_EQ(reject_su.error().code, ErrorCode::PermissionDenied);

    auto reject_cat_pkg = bridge.run_adb({"shell", "cat", "/data/system/packages.xml"});
    KERNOX_ASSERT_TRUE(reject_cat_pkg.is_err());
    KERNOX_ASSERT_EQ(reject_cat_pkg.error().code, ErrorCode::PermissionDenied);

    auto reject_bad_serial = bridge.run_adb({"-s", "-bad", "devices"});
    KERNOX_ASSERT_TRUE(reject_bad_serial.is_err());
    KERNOX_ASSERT_EQ(reject_bad_serial.error().code, ErrorCode::PermissionDenied);

    // Direct run_device_shell API enforcement
    auto reject_shell_rm = bridge.run_device_shell("emulator-5554", {"rm", "-rf", "/data"});
    KERNOX_ASSERT_TRUE(reject_shell_rm.is_err());
    KERNOX_ASSERT_EQ(reject_shell_rm.error().code, ErrorCode::PermissionDenied);

    auto reject_shell_su = bridge.run_device_shell("emulator-5554", {"su"});
    KERNOX_ASSERT_TRUE(reject_shell_su.is_err());
    KERNOX_ASSERT_EQ(reject_shell_su.error().code, ErrorCode::PermissionDenied);

    auto reject_shell_bad_serial = bridge.run_device_shell("bad;serial", {"getprop", "ro.product.model"});
    KERNOX_ASSERT_TRUE(reject_shell_bad_serial.is_err());
    KERNOX_ASSERT_EQ(reject_shell_bad_serial.error().code, ErrorCode::PermissionDenied);

    // 3. Service multi-device ambiguity enforcement
    CommandOutput multi_out{};
    multi_out.exit_code = 0;
    multi_out.stdout_content = read_fixture("devices_multiple.txt");
    mock_runner->set_response("devices -l", multi_out);

    AdbObservationService service(std::make_shared<AdbBridge>(mock_runner));

    // Resolving without serial when multiple are connected MUST fail with InvalidArgument
    auto ambig_res = service.resolve_target_device(std::nullopt);
    KERNOX_ASSERT_TRUE(ambig_res.is_err());
    KERNOX_ASSERT_EQ(ambig_res.error().code, ErrorCode::InvalidArgument);

    // Resolving with explicit serial succeeds
    auto spec_res = service.resolve_target_device("2A231JEHN04123");
    KERNOX_ASSERT_TRUE(spec_res.is_ok());
    KERNOX_ASSERT_EQ(spec_res.value().serial, "2A231JEHN04123");

    // Resolving non-existent serial fails with NotFound
    auto non_res = service.resolve_target_device("NON_EXISTENT_SERIAL");
    KERNOX_ASSERT_TRUE(non_res.is_err());
    KERNOX_ASSERT_EQ(non_res.error().code, ErrorCode::NotFound);

    // 4. Unauthorized device handling
    CommandOutput unauth_out{};
    unauth_out.exit_code = 0;
    unauth_out.stdout_content = read_fixture("devices_unauthorized.txt");
    mock_runner->set_response("devices -l", unauth_out);

    auto unauth_res = service.resolve_target_device(std::nullopt);
    KERNOX_ASSERT_TRUE(unauth_res.is_err());
    KERNOX_ASSERT_EQ(unauth_res.error().code, ErrorCode::PermissionDenied);

    // 5. Offline device handling
    CommandOutput off_out{};
    off_out.exit_code = 0;
    off_out.stdout_content = read_fixture("devices_offline.txt");
    mock_runner->set_response("devices -l", off_out);

    auto off_res = service.resolve_target_device(std::nullopt);
    KERNOX_ASSERT_TRUE(off_res.is_err());
    KERNOX_ASSERT_EQ(off_res.error().code, ErrorCode::InvalidState);

    // 6. Missing ADB binary handling
    mock_runner->set_executable_available(false);
    auto missing_res = service.discover_devices();
    KERNOX_ASSERT_TRUE(missing_res.is_err());
    KERNOX_ASSERT_EQ(missing_res.error().code, ErrorCode::NotFound);

    return 0;
}

static int test_report_generation_with_mocks() {
    std::cout << "Running test_report_generation_with_mocks...\n";

    auto mock_runner = std::make_shared<MockCommandRunner>();

    CommandOutput dev_out{};
    dev_out.exit_code = 0;
    dev_out.stdout_content = read_fixture("devices_single.txt");
    mock_runner->set_response("devices -l", dev_out);

    CommandOutput mfg_out{};
    mfg_out.exit_code = 0;
    mfg_out.stdout_content = "Google\n";
    mock_runner->set_response("-s emulator-5554 shell getprop ro.product.manufacturer", mfg_out);

    CommandOutput mdl_out{};
    mdl_out.exit_code = 0;
    mdl_out.stdout_content = "Pixel 7\n";
    mock_runner->set_response("-s emulator-5554 shell getprop ro.product.model", mdl_out);

    CommandOutput rel_out{};
    rel_out.exit_code = 0;
    rel_out.stdout_content = "14\n";
    mock_runner->set_response("-s emulator-5554 shell getprop ro.build.version.release", rel_out);

    CommandOutput sdk_out{};
    sdk_out.exit_code = 0;
    sdk_out.stdout_content = "34\n";
    mock_runner->set_response("-s emulator-5554 shell getprop ro.build.version.sdk", sdk_out);

    CommandOutput mem_out{};
    mem_out.exit_code = 0;
    mem_out.stdout_content = read_fixture("dumpsys_meminfo_processes.txt");
    mock_runner->set_response("-s emulator-5554 shell dumpsys meminfo", mem_out);

    AdbObservationService service(std::make_shared<AdbBridge>(mock_runner));

    auto rep_res = service.generate_snapshot_report("emulator-5554");
    KERNOX_ASSERT_TRUE(rep_res.is_ok());

    const auto& rep = rep_res.value();
    KERNOX_ASSERT_EQ(rep.source, "android_adb_observation");
    KERNOX_ASSERT_EQ(rep.device_info.serial, "emulator-5554");
    KERNOX_ASSERT_EQ(rep.device_info.model, "Pixel 7");
    KERNOX_ASSERT_EQ(rep.device_info.manufacturer, "Google");
    KERNOX_ASSERT_TRUE(rep.device_info.sdk_version.has_value());
    KERNOX_ASSERT_EQ(*rep.device_info.sdk_version, 34u);

    KERNOX_ASSERT_TRUE(rep.memory_snapshot.has_value());
    KERNOX_ASSERT_TRUE(rep.memory_snapshot->total_ram_bytes.has_value());
    KERNOX_ASSERT_EQ(*rep.memory_snapshot->total_ram_bytes, 7856128ULL * 1024);

    KERNOX_ASSERT_TRUE(!rep.processes.empty());

    // Test serialization to string and JSON
    std::string str_rep = rep.to_string();
    KERNOX_ASSERT_TRUE(str_rep.find("KERNOX 2.0 ANDROID ADB OBSERVATION REPORT") != std::string::npos);
    KERNOX_ASSERT_TRUE(str_rep.find("emulator-5554") != std::string::npos);
    KERNOX_ASSERT_TRUE(str_rep.find("surfaceflinger") != std::string::npos);

    std::string json_rep = rep.to_json();
    KERNOX_ASSERT_TRUE(json_rep.find("\"source\": \"android_adb_observation\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_rep.find("\"serial\": \"emulator-5554\"") != std::string::npos);
    KERNOX_ASSERT_TRUE(json_rep.find("\"processes\": [") != std::string::npos);

    return 0;
}

int main() {
    std::cout << "=== KERNOX 2.0 Android ADB Observation Tests ===\n";

    if (test_device_discovery_parsing() != 0) return 1;
    if (test_device_properties_parsing() != 0) return 1;
    if (test_proc_meminfo_parsing() != 0) return 1;
    if (test_dumpsys_meminfo_parsing() != 0) return 1;
    if (test_safety_and_mock_execution() != 0) return 1;
    if (test_report_generation_with_mocks() != 0) return 1;

    std::cout << "=== All Android ADB Observation Tests Passed Successfully ===\n";
    return 0;
}
