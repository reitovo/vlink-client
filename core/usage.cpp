#include "usage.h"
#include <memory>
#include <Windows.h>

namespace usage {
    namespace {
        union time_data {
            FILETIME ft;
            unsigned long long val;
        };

        struct os_cpu_usage_info {
            time_data last_time, last_sys_time, last_user_time;
            DWORD core_count;
        };

        static std::unique_ptr<os_cpu_usage_info> cpuUsage;

        static void initCpuUsage() {
            SYSTEM_INFO si;
            FILETIME dummy;

            cpuUsage = std::make_unique<os_cpu_usage_info>();

            GetSystemInfo(&si);
            GetSystemTimeAsFileTime(&cpuUsage->last_time.ft);
            GetProcessTimes(GetCurrentProcess(), &dummy, &dummy,
                            &cpuUsage->last_sys_time.ft, &cpuUsage->last_user_time.ft);
            cpuUsage->core_count = si.dwNumberOfProcessors;
        }
    }

    double getCpuUsage() {
        time_data cur_time, cur_sys_time, cur_user_time;
        FILETIME dummy;
        double percent;

        if (!cpuUsage) {
            initCpuUsage();
            return 0.0;
        }

        GetSystemTimeAsFileTime(&cur_time.ft);
        GetProcessTimes(GetCurrentProcess(), &dummy, &dummy, &cur_sys_time.ft,
                        &cur_user_time.ft);

        percent = (double)(cur_sys_time.val - cpuUsage->last_sys_time.val +
                           (cur_user_time.val - cpuUsage->last_user_time.val));
        percent /= (double)(cur_time.val - cpuUsage->last_time.val);
        percent /= (double)cpuUsage->core_count;

        cpuUsage->last_time.val = cur_time.val;
        cpuUsage->last_sys_time.val = cur_sys_time.val;
        cpuUsage->last_user_time.val = cur_user_time.val;

        return percent * 100.0;
    }
}
