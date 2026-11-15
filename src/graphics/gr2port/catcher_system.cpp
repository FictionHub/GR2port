// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <fmt/format.h>
#include <fmt/xchar.h>

#ifdef _WIN32
#include <windows.h>
// After windows.h.
#include <pdh.h>
#include <psapi.h>
#include <winevt.h>
#endif

#include "graphics/gr2port/catcher_system.h"

namespace Gr2Port::Catcher::System {

#ifdef _WIN32

namespace {

using Clock = std::chrono::steady_clock;

// The samples kept, one each SamplePeriod, and the records listed at most.
constexpr size_t Samples = 120;
constexpr std::chrono::milliseconds SamplePeriod{500};
constexpr u32 MaxRecords = 24;
constexpr size_t RecordChars = 420;
constexpr size_t MaxGpus = 4;

std::string Narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(bytes, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes,
                        nullptr, nullptr);
    return out;
}

template <typename Function>
Function Find(HMODULE library, const char* name) {
    return library != nullptr ? reinterpret_cast<Function>(GetProcAddress(library, name)) : nullptr;
}

// Windows's settings for a GPU that does not answer, and for who schedules the GPU.
std::vector<std::string> Settings() {
    std::vector<std::string> lines;
    constexpr const wchar_t* Key = L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers";
    std::string line = "Windows's GPU time-out settings (unset means Windows's own: a GPU that "
                       "does not answer for 2 s is reset):";
    for (const wchar_t* name : {L"TdrLevel", L"TdrDelay", L"TdrDdiDelay", L"TdrLimitCount",
                                L"TdrLimitTime", L"HwSchMode"}) {
        DWORD value = 0;
        DWORD bytes = sizeof(value);
        const bool set = RegGetValueW(HKEY_LOCAL_MACHINE, Key, name, RRF_RT_REG_DWORD, nullptr,
                                      &value, &bytes) == ERROR_SUCCESS;
        line += fmt::format(" {} {}", Narrow(name), set ? std::to_string(value) : "unset");
    }
    lines.push_back(line + " (HwSchMode 2 = the GPU schedules itself, 1 = Windows does)");
    std::array<wchar_t, 1024> path{};
    GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    std::array<wchar_t, 256> preference{};
    DWORD bytes = sizeof(preference) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
                     path.data(), RRF_RT_REG_SZ, nullptr, preference.data(),
                     &bytes) == ERROR_SUCCESS) {
        lines.push_back(fmt::format("Windows's GPU preference for this executable: {}",
                                    Narrow(preference.data())));
    }
    return lines;
}

// The text between a record's tags, and the values its system part names.
std::string Flatten(std::wstring_view xml) {
    const auto attribute = [&](std::wstring_view tag, std::wstring_view name) {
        const size_t at = xml.find(tag);
        const size_t from = at != xml.npos ? xml.find(name, at) : xml.npos;
        if (from == xml.npos) {
            return std::wstring_view{};
        }
        const size_t start = from + name.size();
        const size_t end = xml.find(L'\'', start);
        return end != xml.npos ? xml.substr(start, end - start) : std::wstring_view{};
    };
    const std::wstring_view provider = attribute(L"<Provider", L"Name='");
    const std::wstring_view time = attribute(L"<TimeCreated", L"SystemTime='");
    std::wstring id;
    if (const size_t at = xml.find(L"<EventID"); at != xml.npos) {
        const size_t start = xml.find(L'>', at);
        const size_t end = xml.find(L'<', start);
        if (start != xml.npos && end != xml.npos) {
            id = xml.substr(start + 1, end - start - 1);
        }
    }
    std::wstring text;
    const size_t body = xml.find(L"</System>");
    bool in_tag = false;
    for (size_t i = body != xml.npos ? body : 0; i < xml.size(); i++) {
        const wchar_t c = xml[i];
        if (c == L'<') {
            in_tag = true;
            if (!text.empty() && text.back() != L' ') {
                text.push_back(L' ');
            }
        } else if (c == L'>') {
            in_tag = false;
        } else if (!in_tag && c >= 0x20) {
            if (c != L' ' || (!text.empty() && text.back() != L' ')) {
                text.push_back(c);
            }
        }
    }
    std::string out = fmt::format("{} UTC, {} event {}: {}", Narrow(time.substr(0, 19)),
                                  Narrow(provider), Narrow(id), Narrow(text));
    if (out.size() > RecordChars) {
        out.resize(RecordChars);
        out += "...";
    }
    return out;
}

// The records of a Windows log that match query, newest first; keep filters them by their text.
std::vector<std::string> Records(const wchar_t* log, const std::wstring& query,
                                 bool (*keep)(std::wstring_view)) {
    std::vector<std::string> out;
    const HMODULE library = LoadLibraryW(L"wevtapi.dll");
    const auto evt_query = Find<decltype(&EvtQuery)>(library, "EvtQuery");
    const auto evt_next = Find<decltype(&EvtNext)>(library, "EvtNext");
    const auto evt_render = Find<decltype(&EvtRender)>(library, "EvtRender");
    const auto evt_close = Find<decltype(&EvtClose)>(library, "EvtClose");
    if (evt_query == nullptr || evt_next == nullptr || evt_render == nullptr ||
        evt_close == nullptr) {
        out.push_back(fmt::format("Windows's {} log cannot be read here", Narrow(log)));
        return out;
    }
    const EVT_HANDLE results =
        evt_query(nullptr, log, query.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection);
    if (results == nullptr) {
        out.push_back(fmt::format("Windows's {} log gave error {}", Narrow(log), GetLastError()));
        return out;
    }
    std::vector<wchar_t> buffer(1 << 16);
    u32 looked = 0;
    while (out.size() < MaxRecords && looked < 400) {
        EVT_HANDLE record = nullptr;
        DWORD got = 0;
        if (evt_next(results, 1, &record, 1000, 0, &got) == 0 || got == 0) {
            break;
        }
        looked++;
        DWORD used = 0;
        DWORD properties = 0;
        if (evt_render(nullptr, record, EvtRenderEventXml,
                       static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), buffer.data(), &used,
                       &properties) != 0) {
            const std::wstring_view xml{buffer.data(), used / sizeof(wchar_t)};
            if (keep(xml)) {
                out.push_back(Flatten(xml));
            }
        }
        evt_close(record);
    }
    evt_close(results);
    return out;
}

bool AboutGraphics(std::wstring_view xml) {
    for (const wchar_t* word :
         {L"nvlddmkm", L"Display", L"dxgkrnl", L"DxgKrnl", L"igfx", L"amdkmdag", L"amdwddmg",
          L"WHEA", L"Kernel-Power", L"atikmdag", L"BasicDisplay", L"nvgpucomp", L"vulkan",
          L"Gravity Rush", L"LiveKernelEvent", L"Application Error", L"Application Hang"}) {
        if (xml.find(word) != xml.npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> AllRecords(u32 minutes) {
    const u64 ms = u64{minutes} * 60'000;
    std::vector<std::string> lines;
    const auto add = [&](const char* title, std::vector<std::string> records) {
        lines.push_back(fmt::format("{} of the last {} minutes, newest first: {}", title, minutes,
                                    records.empty() ? "none" : ""));
        for (std::string& record : records) {
            lines.push_back("  " + std::move(record));
        }
    };
    add("Windows's system log, errors and warnings about graphics, hardware and power",
        Records(L"System",
                fmt::format(L"*[System[(Level=1 or Level=2 or Level=3) and "
                            L"TimeCreated[timediff(@SystemTime) <= {}]]]",
                            ms),
                AboutGraphics));
    add("Windows's application log, error reports and crashes of this executable and of GPUs",
        Records(L"Application",
                fmt::format(L"*[System[(EventID=1000 or EventID=1001 or EventID=1002) and "
                            L"TimeCreated[timediff(@SystemTime) <= {}]]]",
                            ms),
                AboutGraphics));
    // Windows keeps a dump of a GPU it had to reset.
    std::string dumps;
    for (const wchar_t* folder :
         {L"C:\\Windows\\LiveKernelReports\\WATCHDOG\\*", L"C:\\Windows\\LiveKernelReports\\*"}) {
        WIN32_FIND_DATAW found;
        const HANDLE find = FindFirstFileW(folder, &found);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }
        FILETIME now_file;
        GetSystemTimeAsFileTime(&now_file);
        const u64 now = (u64{now_file.dwHighDateTime} << 32) | now_file.dwLowDateTime;
        do {
            const u64 written = (u64{found.ftLastWriteTime.dwHighDateTime} << 32) |
                                found.ftLastWriteTime.dwLowDateTime;
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                now - written < ms * 10'000) {
                dumps += fmt::format(" {} ({} minutes ago)", Narrow(found.cFileName),
                                     (now - written) / 600'000'000);
            }
        } while (FindNextFileW(find, &found) != 0);
        FindClose(find);
    }
    lines.push_back("Windows's dumps of GPU resets in that time:" +
                    (dumps.empty() ? std::string{" none seen"} : dumps));
    return lines;
}

// The NVIDIA driver's own library of GPU state.
struct Nvml {
    using Handle = void*;
    struct Utilization {
        unsigned gpu;
        unsigned memory;
    };
    struct MemoryInfo {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };
    int (*init)() = nullptr;
    int (*handle_by_index)(unsigned, Handle*) = nullptr;
    int (*utilization)(Handle, Utilization*) = nullptr;
    int (*temperature)(Handle, int, unsigned*) = nullptr;
    int (*power)(Handle, unsigned*) = nullptr;
    int (*memory)(Handle, MemoryInfo*) = nullptr;
    int (*clock)(Handle, int, unsigned*) = nullptr;
    int (*throttle)(Handle, unsigned long long*) = nullptr;
    Handle gpu = nullptr;

    void Load() {
        const HMODULE library = LoadLibraryW(L"nvml.dll");
        init = Find<decltype(init)>(library, "nvmlInit_v2");
        handle_by_index = Find<decltype(handle_by_index)>(library, "nvmlDeviceGetHandleByIndex_v2");
        utilization = Find<decltype(utilization)>(library, "nvmlDeviceGetUtilizationRates");
        temperature = Find<decltype(temperature)>(library, "nvmlDeviceGetTemperature");
        power = Find<decltype(power)>(library, "nvmlDeviceGetPowerUsage");
        memory = Find<decltype(memory)>(library, "nvmlDeviceGetMemoryInfo");
        clock = Find<decltype(clock)>(library, "nvmlDeviceGetClockInfo");
        throttle = Find<decltype(throttle)>(library, "nvmlDeviceGetCurrentClocksThrottleReasons");
        if (init == nullptr || handle_by_index == nullptr || init() != 0 ||
            handle_by_index(0, &gpu) != 0) {
            gpu = nullptr;
        }
    }
};

struct Sample {
    Clock::time_point time;
    // Each GPU's 3D load in percent, from this process and from every process.
    std::array<float, MaxGpus> own{};
    std::array<float, MaxGpus> all{};
    u32 gpus = 0;
    // The NVIDIA GPU: load in percent, degrees, watts, MHz, the reasons its clock is held down.
    bool nvidia = false;
    u32 nv_load = 0;
    u32 nv_degrees = 0;
    float nv_watts = 0.0f;
    u32 nv_mhz = 0;
    u32 nv_used_mb = 0;
    u64 nv_limits = 0;
    // The process's memory and Windows's.
    u32 working_mb = 0;
    u32 commit_mb = 0;
    u32 system_load = 0;
    u32 system_free_commit_mb = 0;
};

std::mutex g_mutex;
std::array<Sample, Samples> g_samples{};
size_t g_next = 0;
size_t g_count = 0;
std::vector<std::string> g_gpu_names;
std::thread g_thread;
Nvml g_nvml;

// Each GPU's 3D engines' load, through Windows's counters. Their names hold the process and the
// GPU: pid_N_luid_A_B_phys_0_eng_E_engtype_3D.
struct Counters {
    HMODULE library = nullptr;
    decltype(&PdhOpenQueryW) open = nullptr;
    decltype(&PdhAddEnglishCounterW) add = nullptr;
    decltype(&PdhCollectQueryData) collect = nullptr;
    decltype(&PdhGetFormattedCounterArrayW) array = nullptr;
    decltype(&PdhCloseQuery) close = nullptr;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    std::vector<u8> buffer;
    u32 reads = 0;

    bool Open() {
        if (library == nullptr) {
            library = LoadLibraryW(L"pdh.dll");
            open = Find<decltype(open)>(library, "PdhOpenQueryW");
            add = Find<decltype(add)>(library, "PdhAddEnglishCounterW");
            collect = Find<decltype(collect)>(library, "PdhCollectQueryData");
            array = Find<decltype(array)>(library, "PdhGetFormattedCounterArrayW");
            close = Find<decltype(close)>(library, "PdhCloseQuery");
        }
        if (open == nullptr || add == nullptr || collect == nullptr || array == nullptr ||
            close == nullptr) {
            return false;
        }
        if (query != nullptr) {
            close(query);
            query = nullptr;
        }
        if (open(nullptr, 0, &query) != ERROR_SUCCESS ||
            add(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
            query = nullptr;
            return false;
        }
        collect(query);
        return true;
    }

    void Read(Sample& sample) {
        // Processes and their engines come and go: the counter's list is made anew now and then.
        if ((query == nullptr || reads++ % 40 == 39) && !Open()) {
            return;
        }
        if (collect(query) != ERROR_SUCCESS) {
            return;
        }
        DWORD bytes = 0;
        DWORD items = 0;
        array(counter, PDH_FMT_DOUBLE, &bytes, &items, nullptr);
        if (bytes == 0) {
            return;
        }
        buffer.resize(bytes);
        auto* const values = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (array(counter, PDH_FMT_DOUBLE, &bytes, &items, values) != ERROR_SUCCESS) {
            return;
        }
        const std::wstring own = fmt::format(L"pid_{}_", GetCurrentProcessId());
        const std::scoped_lock lock{g_mutex};
        for (DWORD i = 0; i < items; i++) {
            const std::wstring_view name{values[i].szName};
            const size_t luid = name.find(L"luid_");
            const size_t end = name.find(L"_phys");
            if (name.find(L"engtype_3D") == name.npos || luid == name.npos || end == name.npos ||
                values[i].FmtValue.CStatus != ERROR_SUCCESS) {
                continue;
            }
            const std::string gpu = Narrow(name.substr(luid + 5, end - luid - 5));
            size_t index = std::ranges::find(g_gpu_names, gpu) - g_gpu_names.begin();
            if (index == g_gpu_names.size()) {
                if (index == MaxGpus) {
                    continue;
                }
                g_gpu_names.push_back(gpu);
            }
            const float load = static_cast<float>(values[i].FmtValue.doubleValue);
            sample.all[index] += load;
            if (name.starts_with(own)) {
                sample.own[index] += load;
            }
        }
        sample.gpus = static_cast<u32>(g_gpu_names.size());
    }
};

Counters g_counters;
std::atomic<bool> g_sampling{false};

Sample Take() {
    Sample sample{.time = Clock::now()};
    g_counters.Read(sample);
    if (g_nvml.gpu != nullptr) {
        sample.nvidia = true;
        Nvml::Utilization load{};
        if (g_nvml.utilization != nullptr && g_nvml.utilization(g_nvml.gpu, &load) == 0) {
            sample.nv_load = load.gpu;
        }
        unsigned value = 0;
        if (g_nvml.temperature != nullptr && g_nvml.temperature(g_nvml.gpu, 0, &value) == 0) {
            sample.nv_degrees = value;
        }
        if (g_nvml.power != nullptr && g_nvml.power(g_nvml.gpu, &value) == 0) {
            sample.nv_watts = static_cast<float>(value) / 1000.0f;
        }
        if (g_nvml.clock != nullptr && g_nvml.clock(g_nvml.gpu, 0, &value) == 0) {
            sample.nv_mhz = value;
        }
        Nvml::MemoryInfo memory{};
        if (g_nvml.memory != nullptr && g_nvml.memory(g_nvml.gpu, &memory) == 0) {
            sample.nv_used_mb = static_cast<u32>(memory.used >> 20);
        }
        unsigned long long limits = 0;
        if (g_nvml.throttle != nullptr && g_nvml.throttle(g_nvml.gpu, &limits) == 0) {
            sample.nv_limits = limits;
        }
    }
    PROCESS_MEMORY_COUNTERS counters{.cb = sizeof(counters)};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        sample.working_mb = static_cast<u32>(counters.WorkingSetSize >> 20);
        sample.commit_mb = static_cast<u32>(counters.PagefileUsage >> 20);
    }
    MEMORYSTATUSEX status{.dwLength = sizeof(status)};
    if (GlobalMemoryStatusEx(&status) != 0) {
        sample.system_load = status.dwMemoryLoad;
        sample.system_free_commit_mb = static_cast<u32>(status.ullAvailPageFile >> 20);
    }
    return sample;
}

// The NVIDIA driver's reasons for a clock below its highest, in words.
std::string Limits(u64 reasons) {
    constexpr std::array<const char*, 9> Names{
        "idle",
        "a clock setting",
        "a power cap",
        "hardware slowdown",
        "sync boost",
        "thermal limit",
        "hardware heat",
        "hardware power brake",
        "the display's clock",
    };
    std::string out;
    for (size_t bit = 0; bit < Names.size(); bit++) {
        if ((reasons >> bit & 1) != 0) {
            out += (out.empty() ? "" : "+") + std::string{Names[bit]};
        }
    }
    return out.empty() ? (reasons == 0 ? "nothing" : fmt::format("{:#x}", reasons)) : out;
}

std::string Describe(const Sample& sample, Clock::time_point now) {
    std::string line =
        fmt::format("{:.1f} s ago:", std::chrono::duration<double>(now - sample.time).count());
    for (u32 gpu = 0; gpu < sample.gpus; gpu++) {
        line +=
            fmt::format(" GPU {} {:.0f}% ours {:.0f}% all,", gpu, sample.own[gpu], sample.all[gpu]);
    }
    if (sample.nvidia) {
        line += fmt::format(" NVIDIA {}% {} C {:.0f} W {} MHz {} MB clock held by {},",
                            sample.nv_load, sample.nv_degrees, sample.nv_watts, sample.nv_mhz,
                            sample.nv_used_mb, Limits(sample.nv_limits));
    }
    return line + fmt::format(" process {} MB in use {} MB committed, Windows {}% of RAM in use "
                              "{} MB of commit free",
                              sample.working_mb, sample.commit_mb, sample.system_load,
                              sample.system_free_commit_mb);
}

void Run() {
    while (g_sampling.load(std::memory_order_relaxed)) {
        const Sample sample = Take();
        {
            const std::scoped_lock lock{g_mutex};
            g_samples[g_next] = sample;
            g_next = (g_next + 1) % Samples;
            g_count = std::min(g_count + 1, Samples);
        }
        std::this_thread::sleep_for(SamplePeriod);
    }
}

// The power source and the processors' speed now.
std::string Power() {
    std::string line;
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) != 0) {
        line = fmt::format("power now: {}, {}, saver {}",
                           power.ACLineStatus == 1   ? "plugged in"
                           : power.ACLineStatus == 0 ? "on battery"
                                                     : "unknown",
                           power.BatteryLifePercent == 255
                               ? std::string{"no battery"}
                               : fmt::format("battery {}%", u32{power.BatteryLifePercent}),
                           u32{power.SystemStatusFlag});
    }
    struct Processor {
        ULONG number;
        ULONG max_mhz;
        ULONG current_mhz;
        ULONG mhz_limit;
        ULONG max_idle;
        ULONG current_idle;
    };
    using Information = LONG(WINAPI*)(int, PVOID, ULONG, PVOID, ULONG);
    const auto information =
        Find<Information>(LoadLibraryW(L"powrprof.dll"), "CallNtPowerInformation");
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    std::vector<Processor> processors(system.dwNumberOfProcessors);
    constexpr int ProcessorInformationLevel = 11;
    if (information != nullptr && !processors.empty() &&
        information(ProcessorInformationLevel, nullptr, 0, processors.data(),
                    static_cast<ULONG>(processors.size() * sizeof(Processor))) == 0) {
        ULONG lowest = ~0ul;
        ULONG highest = 0;
        ULONG limit = ~0ul;
        for (const Processor& processor : processors) {
            lowest = std::min(lowest, processor.current_mhz);
            highest = std::max(highest, processor.current_mhz);
            limit = std::min(limit, processor.mhz_limit);
        }
        line += fmt::format("; processors at {} to {} MHz of {}, the lowest limit {} MHz", lowest,
                            highest, processors.front().max_mhz, limit);
    }
    return line;
}

} // namespace

std::vector<std::string> AtStart() {
    std::vector<std::string> lines = Settings();
    std::vector<std::string> records = AllRecords(24 * 60);
    lines.insert(lines.end(), std::make_move_iterator(records.begin()),
                 std::make_move_iterator(records.end()));
    return lines;
}

void StartSampling() {
    if (g_sampling.exchange(true)) {
        return;
    }
    g_nvml.Load();
    g_thread = std::thread{Run};
    g_thread.detach();
}

std::string LoadNow() {
    const std::scoped_lock lock{g_mutex};
    if (g_count == 0) {
        return "no sample of the GPUs' load yet";
    }
    return Describe(g_samples[(g_next + Samples - 1) % Samples], Clock::now());
}

std::vector<std::string> AtEvent(u32 minutes) {
    std::vector<std::string> lines;
    {
        const std::scoped_lock lock{g_mutex};
        std::string names = "the GPUs in the samples, as Windows numbers them:";
        for (size_t i = 0; i < g_gpu_names.size(); i++) {
            names += fmt::format(" GPU {} = {}", i, g_gpu_names[i]);
        }
        lines.push_back(names + fmt::format("; the NVIDIA driver's own figures are {}",
                                            g_nvml.gpu != nullptr ? "read" : "not there"));
        // The last ten samples each, and before them every tenth.
        const Clock::time_point now = Clock::now();
        for (size_t back = g_count; back > 0; back--) {
            if (back <= 10 || back % 10 == 0) {
                lines.push_back(Describe(g_samples[(g_next + Samples - back) % Samples], now));
            }
        }
    }
    lines.push_back(Power());
    std::vector<std::string> records = AllRecords(minutes);
    lines.insert(lines.end(), std::make_move_iterator(records.begin()),
                 std::make_move_iterator(records.end()));
    return lines;
}

#else

std::vector<std::string> AtStart() {
    return {"the system's own GPU records are not read on this system"};
}

void StartSampling() {}

std::string LoadNow() {
    return "the GPUs' load is not read on this system";
}

std::vector<std::string> AtEvent(u32) {
    return {};
}

#endif

} // namespace Gr2Port::Catcher::System
