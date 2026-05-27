#include "topo/Platform/Platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <fstream>
#include <string>
#include <unistd.h>
#endif

namespace topo::platform {

uint64_t totalPhysicalMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys;
    return 0;
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memSize = 0;
    size_t len = sizeof(memSize);
    if (sysctl(mib, 2, &memSize, &len, nullptr, 0) == 0) return memSize;
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) return static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize);
    return 0;
#endif
}

uint64_t availableMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) return status.ullAvailPhys;
    return 0;
#elif defined(__APPLE__)
    vm_statistics64_data_t vmStat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmStat), &count) ==
        KERN_SUCCESS) {
        uint64_t pageSize = vm_page_size;
        return (static_cast<uint64_t>(vmStat.free_count) + static_cast<uint64_t>(vmStat.inactive_count)) * pageSize;
    }
    return 0;
#else
    // Linux: read MemAvailable from /proc/meminfo (includes reclaimable)
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.compare(0, 14, "MemAvailable:") == 0) {
            uint64_t kb = 0;
            auto pos = line.find_first_of("0123456789");
            if (pos != std::string::npos) kb = std::stoull(line.substr(pos));
            return kb * 1024;
        }
    }
    return 0;
#endif
}

} // namespace topo::platform
