/*
 * Copyright 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "dumpstate"

#include "DumpstateDevice.h"

#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <cutils/properties.h>
#include <hidl/HidlBinderSupport.h>
#include <hidl/HidlSupport.h>

#include <log/log.h>
#include <pthread.h>
#include <string.h>

#define _SVID_SOURCE
#include <dirent.h>

#include "DumpstateUtil.h"

#define MODEM_LOG_PREFIX_PROPERTY "ro.vendor.radio.log_prefix"
#define MODEM_LOG_LOC_PROPERTY "ro.vendor.radio.log_loc"
#define MODEM_LOGGING_SWITCH "persist.vendor.radio.smlog_switch"

#define DIAG_MDLOG_PERSIST_PROPERTY "persist.vendor.sys.modem.diag.mdlog"
#define DIAG_MDLOG_PROPERTY "vendor.sys.modem.diag.mdlog"
#define DIAG_MDLOG_STATUS_PROPERTY "vendor.sys.modem.diag.mdlog_on"

#define DIAG_MDLOG_NUMBER_BUGREPORT "persist.vendor.sys.modem.diag.mdlog_br_num"

#define UFS_BOOTDEVICE "ro.boot.bootdevice"

#define TCPDUMP_NUMBER_BUGREPORT "persist.vendor.tcpdump.log.br_num"
#define TCPDUMP_PERSIST_PROPERTY "persist.vendor.tcpdump.log.alwayson"

#define MODEM_EFS_DUMP_PROPERTY "vendor.sys.modem.diag.efsdump"

#define VENDOR_VERBOSE_LOGGING_ENABLED_PROPERTY "persist.vendor.verbose_logging_enabled"

using android::os::dumpstate::CommandOptions;
using android::os::dumpstate::DumpFileToFd;
using android::os::dumpstate::PropertiesHelper;
using android::os::dumpstate::RunCommandToFd;

namespace android {
namespace hardware {
namespace dumpstate {
namespace V1_1 {
namespace implementation {

#define DIAG_LOG_PREFIX "diag_log_"
#define TCPDUMP_LOG_PREFIX "tcpdump"
#define EXTENDED_LOG_PREFIX "extended_log_"

static void dumpLogs(int fd, std::string srcDir, std::string destDir,
                     int maxFileNum, const char *logPrefix) {
    struct dirent **dirent_list = NULL;
    int num_entries = scandir(srcDir.c_str(),
                              &dirent_list,
                              0,
                              (int (*)(const struct dirent **, const struct dirent **)) alphasort);
    if (!dirent_list) {
        return;
    } else if (num_entries <= 0) {
        return;
    }

    int copiedFiles = 0;

    for (int i = num_entries - 1; i >= 0; i--) {
        ALOGD("Found %s\n", dirent_list[i]->d_name);

        if (0 != strncmp(dirent_list[i]->d_name, logPrefix, strlen(logPrefix))) {
            continue;
        }

        if ((copiedFiles >= maxFileNum) && (maxFileNum != -1)) {
            ALOGD("Skipped %s\n", dirent_list[i]->d_name);
            continue;
        }

        copiedFiles++;

        CommandOptions options = CommandOptions::WithTimeout(120).Build();
        std::string srcLogFile = srcDir + "/" + dirent_list[i]->d_name;
        std::string destLogFile = destDir + "/" + dirent_list[i]->d_name;

        std::string copyCmd = "/vendor/bin/cp " + srcLogFile + " " + destLogFile;

        ALOGD("Copying %s to %s\n", srcLogFile.c_str(), destLogFile.c_str());
        RunCommandToFd(fd, "CP DIAG LOGS", { "/vendor/bin/sh", "-c", copyCmd.c_str() }, options);
    }

    while (num_entries--) {
        free(dirent_list[num_entries]);
    }

    free(dirent_list);
}

static void *dumpModemThread(void *data)
{
    long fdModem = (long)data;

    ALOGD("dumpModemThread started\n");

    std::string modemLogDir = android::base::GetProperty(MODEM_LOG_LOC_PROPERTY, "");
    if (modemLogDir.empty()) {
        ALOGD("No modem log place is set");
        return NULL;
    }

    std::string filePrefix = android::base::GetProperty(MODEM_LOG_PREFIX_PROPERTY, "");

    if (filePrefix.empty()) {
        ALOGD("Modem log prefix is not set");
        return NULL;
    }

    sleep(1);
    ALOGD("Waited modem for 1 second to flush logs");

    const std::string modemLogCombined = modemLogDir + "/" + filePrefix + "all.tar";
    const std::string modemLogAllDir = modemLogDir + "/modem_log";

    RunCommandToFd(STDOUT_FILENO, "MKDIR MODEM LOG", {"/vendor/bin/mkdir", "-p", modemLogAllDir.c_str()}, CommandOptions::WithTimeout(2).Build());

    const std::string diagLogDir = "/data/vendor/radio/diag_logs/logs";
    const std::string diagPoweronLogPath = "/data/vendor/radio/diag_logs/logs/diag_poweron_log.qmdl";

    bool diagLogEnabled = android::base::GetBoolProperty(DIAG_MDLOG_PERSIST_PROPERTY, false);

    if (diagLogEnabled) {
        bool diagLogStarted = android::base::GetBoolProperty( DIAG_MDLOG_STATUS_PROPERTY, false);

        if (diagLogStarted) {
            android::base::SetProperty(DIAG_MDLOG_PROPERTY, "false");
            ALOGD("Stopping diag_mdlog...\n");
            if (android::base::WaitForProperty(DIAG_MDLOG_STATUS_PROPERTY, "false", std::chrono::seconds(10))) {
                ALOGD("diag_mdlog exited");
            } else {
                ALOGE("Waited mdlog timeout after 10 second");
            }
        } else {
            ALOGD("diag_mdlog is not running");
        }

        dumpLogs(STDOUT_FILENO, diagLogDir, modemLogAllDir, android::base::GetIntProperty(DIAG_MDLOG_NUMBER_BUGREPORT, 100), DIAG_LOG_PREFIX);

        if (diagLogStarted) {
            ALOGD("Restarting diag_mdlog...");
            android::base::SetProperty(DIAG_MDLOG_PROPERTY, "true");
        }
    }
    RunCommandToFd(STDOUT_FILENO, "CP MODEM POWERON LOG", {"/vendor/bin/cp", diagPoweronLogPath.c_str(), modemLogAllDir.c_str()}, CommandOptions::WithTimeout(2).Build());

    if (!PropertiesHelper::IsUserBuild()) {
        android::base::SetProperty(MODEM_EFS_DUMP_PROPERTY, "true");

        const std::string tcpdumpLogDir = "/data/vendor/tcpdump_logger/logs";
        const std::string extendedLogDir = "/data/vendor/radio/extended_logs";
        const std::vector <std::string> rilAndNetmgrLogs
            {
                "/data/vendor/radio/haldebug_ril0",
                "/data/vendor/radio/haldebug_ril1",
                "/data/vendor/radio/ril_log0",
                "/data/vendor/radio/ril_log0_old",
                "/data/vendor/radio/ril_log1",
                "/data/vendor/radio/ril_log1_old",
                "/data/vendor/radio/qmi_fw_log",
                "/data/vendor/radio/qmi_fw_log_old",
                "/data/vendor/radio/imsdatadaemon_log",
                "/data/vendor/radio/imsdatadaemon_log_old",
                "/data/vendor/netmgr/netmgr_log",
                "/data/vendor/netmgr/netmgr_log_old",
                "/data/vendor/radio/power_anomaly_data.txt",
                "/data/vendor/radio/diag_logs/diag_trace.txt",
                "/data/vendor/radio/diag_logs/diag_trace_old.txt",
                "/data/vendor/radio/metrics_data",
                "/data/vendor/ssrlog/ssr_log.txt",
                "/data/vendor/ssrlog/ssr_log_old.txt",
                "/data/vendor/rfs/mpss/modem_efs"
            };

       bool tcpdumpEnabled = android::base::GetBoolProperty(TCPDUMP_PERSIST_PROPERTY, false);
       if (tcpdumpEnabled) {
            dumpLogs(STDOUT_FILENO, tcpdumpLogDir, modemLogAllDir, android::base::GetIntProperty(TCPDUMP_NUMBER_BUGREPORT, 5), TCPDUMP_LOG_PREFIX);
        }

        for (const auto& logFile : rilAndNetmgrLogs) {
            RunCommandToFd(STDOUT_FILENO, "CP MODEM LOG", {"/vendor/bin/cp", logFile.c_str(), modemLogAllDir.c_str()}, CommandOptions::WithTimeout(2).Build());
        }

        dumpLogs(STDOUT_FILENO, extendedLogDir, modemLogAllDir, 100, EXTENDED_LOG_PREFIX);
        android::base::SetProperty(MODEM_EFS_DUMP_PROPERTY, "false");
    }

    RunCommandToFd(STDOUT_FILENO, "TAR LOG", {"/vendor/bin/tar", "cvf", modemLogCombined.c_str(), "-C", modemLogAllDir.c_str(), "."}, CommandOptions::WithTimeout(20).Build());
    RunCommandToFd(STDOUT_FILENO, "CHG PERM", {"/vendor/bin/chmod", "a+w", modemLogCombined.c_str()}, CommandOptions::WithTimeout(2).Build());

    std::vector<uint8_t> buffer(65536);
    android::base::unique_fd fdLog(TEMP_FAILURE_RETRY(open(modemLogCombined.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)));

    if (fdLog >= 0) {
        while (1) {
            ssize_t bytes_read = TEMP_FAILURE_RETRY(read(fdLog, buffer.data(), buffer.size()));

            if (bytes_read == 0) {
                break;
            } else if (bytes_read < 0) {
                ALOGD("read(%s): %s\n", modemLogCombined.c_str(), strerror(errno));
                break;
            }

            ssize_t result = TEMP_FAILURE_RETRY(write(fdModem, buffer.data(), bytes_read));

            if (result != bytes_read) {
                ALOGD("Failed to write %ld bytes, actually written: %ld", bytes_read, result);
                break;
            }
        }
    }

    RunCommandToFd(STDOUT_FILENO, "RM MODEM DIR", { "/vendor/bin/rm", "-r", modemLogAllDir.c_str()}, CommandOptions::WithTimeout(2).Build());
    RunCommandToFd(STDOUT_FILENO, "RM LOG", { "/vendor/bin/rm", modemLogCombined.c_str()}, CommandOptions::WithTimeout(2).Build());

    ALOGD("dumpModemThread finished\n");

    return NULL;
}

static void DumpF2FS(int fd) {
    DumpFileToFd(fd, "F2FS", "/sys/kernel/debug/f2fs/status");
    RunCommandToFd(fd, "F2FS - fragmentation", {"/vendor/bin/sh", "-c",
                       "for d in $(ls /proc/fs/f2fs/); do "
                       "echo $d: /dev/block/mapper/`ls -l /dev/block/mapper | grep $d | awk '{print $8,$9,$10}'`; "
                       "cat /proc/fs/f2fs/$d/segment_info; done"});
    RunCommandToFd(fd, "F2FS - fsck time (ms)", {"/vendor/bin/sh", "-c", "getprop ro.boottime.init.fsck.data"});
    RunCommandToFd(fd, "F2FS - checkpoint=disable time (ms)", {"/vendor/bin/sh", "-c", "getprop ro.boottime.init.mount.data"});
}

static void DumpUFS(int fd) {
    DumpFileToFd(fd, "UFS model", "/sys/block/sda/device/model");
    DumpFileToFd(fd, "UFS rev", "/sys/block/sda/device/rev");
    DumpFileToFd(fd, "UFS size", "/sys/block/sda/size");
    DumpFileToFd(fd, "UFS show_hba", "/sys/kernel/debug/ufshcd0/show_hba");

    std::string bootdev = android::base::GetProperty(UFS_BOOTDEVICE, "");
    if (!bootdev.empty()) {
        DumpFileToFd(fd, "UFS Slow IO Read", "/sys/devices/platform/soc/" + bootdev + "/slowio_read_cnt");
        DumpFileToFd(fd, "UFS Slow IO Write", "/sys/devices/platform/soc/" + bootdev + "/slowio_write_cnt");
        DumpFileToFd(fd, "UFS Slow IO Unmap", "/sys/devices/platform/soc/" + bootdev + "/slowio_unmap_cnt");
        DumpFileToFd(fd, "UFS Slow IO Sync", "/sys/devices/platform/soc/" + bootdev + "/slowio_sync_cnt");

        RunCommandToFd(fd, "UFS err_stats", {"/vendor/bin/sh", "-c",
                           "path=\"/sys/devices/platform/soc/" + bootdev + "/err_stats\"; "
                           "for node in `ls $path/err_*`; do "
                           "printf \"%s:%d\\n\" $(basename $node) $(cat $node); done;"});

        RunCommandToFd(fd, "UFS io_stats", {"/vendor/bin/sh", "-c",
                           "path=\"/sys/devices/platform/soc/" + bootdev + "/io_stats\"; "
                           "printf \"\\t\\t%-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "ReadCnt ReadBytes WriteCnt WriteBytes RWCnt RWBytes; "
                           "str=$(cat $path/*_start); arr=($str); "
                           "printf \"Started: \\t%-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "${arr[1]} ${arr[0]} ${arr[5]} ${arr[4]} ${arr[3]} ${arr[2]}; "
                           "str=$(cat $path/*_complete); arr=($str); "
                           "printf \"Completed: \\t%-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "${arr[1]} ${arr[0]} ${arr[5]} ${arr[4]} ${arr[3]} ${arr[2]}; "
                           "str=$(cat $path/*_maxdiff); arr=($str); "
                           "printf \"MaxDiff: \\t%-10s %-10s %-10s %-10s %-10s %-10s\\n\\n\" "
                           "${arr[1]} ${arr[0]} ${arr[5]} ${arr[4]} ${arr[3]} ${arr[2]}; "});

        RunCommandToFd(fd, "UFS req_stats", {"/vendor/bin/sh", "-c",
                           "path=\"/sys/devices/platform/soc/" + bootdev + "/req_stats\"; "
                           "printf \"\\t%-10s %-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "All Write Read Read\\(urg\\) Write\\(urg\\) Flush Discard; "
                           "str=$(cat $path/*_min); arr=($str); "
                           "printf \"Min:\\t%-10s %-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "${arr[0]} ${arr[3]} ${arr[6]} ${arr[4]} ${arr[5]} ${arr[2]} ${arr[1]}; "
                           "str=$(cat $path/*_max); arr=($str); "
                           "printf \"Max:\\t%-10s %-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "${arr[0]} ${arr[3]} ${arr[6]} ${arr[4]} ${arr[5]} ${arr[2]} ${arr[1]}; "
                           "str=$(cat $path/*_avg); arr=($str); "
                           "printf \"Avg.:\\t%-10s %-10s %-10s %-10s %-10s %-10s %-10s\\n\" "
                           "${arr[0]} ${arr[3]} ${arr[6]} ${arr[4]} ${arr[5]} ${arr[2]} ${arr[1]}; "
                           "str=$(cat $path/*_sum); arr=($str); "
                           "printf \"Count:\\t%-10s %-10s %-10s %-10s %-10s %-10s %-10s\\n\\n\" "
                           "${arr[0]} ${arr[3]} ${arr[6]} ${arr[4]} ${arr[5]} ${arr[2]} ${arr[1]};"});

        std::string ufs_health = "for f in $(find /sys/devices/platform/soc/" + bootdev + "/health -type f); do if [[ -r $f && -f $f ]]; then echo --- $f; cat $f; echo ''; fi; done";
        RunCommandToFd(fd, "UFS health", {"/vendor/bin/sh", "-c", ufs_health.c_str()});
    }
}

// Methods from ::android::hardware::dumpstate::V1_0::IDumpstateDevice follow.
Return<void> DumpstateDevice::dumpstateBoard(const hidl_handle& handle) {
    // Ignore return value, just return an empty status.
    dumpstateBoard_1_1(handle, DumpstateMode::DEFAULT, 30 * 1000 /* timeoutMillis */);
    return Void();
}

// Methods from ::android::hardware::dumpstate::V1_1::IDumpstateDevice follow.
Return<DumpstateStatus> DumpstateDevice::dumpstateBoard_1_1(const hidl_handle& handle,
                                                            const DumpstateMode mode,
                                                            const uint64_t timeoutMillis) {
    // Unused arguments.
    (void) timeoutMillis;

    // Exit when dump is completed since this is a lazy HAL.
    addPostCommandTask([]() {
        exit(0);
    });

    if (handle == nullptr || handle->numFds < 1) {
        ALOGE("no FDs\n");
        return DumpstateStatus::ILLEGAL_ARGUMENT;
    }

    int fd = handle->data[0];
    if (fd < 0) {
        ALOGE("invalid FD: %d\n", handle->data[0]);
        return DumpstateStatus::ILLEGAL_ARGUMENT;
    }

    bool isModeValid = false;
    for (const auto dumpstateMode : hidl_enum_range<DumpstateMode>()) {
        if (mode == dumpstateMode) {
            isModeValid = true;
            break;
        }
    }
    if (!isModeValid) {
        ALOGE("Invalid mode: %d\n", mode);
        return DumpstateStatus::ILLEGAL_ARGUMENT;
    } else if (mode == DumpstateMode::WEAR) {
        // We aren't a Wear device.
        ALOGE("Unsupported mode: %d\n", mode);
        return DumpstateStatus::UNSUPPORTED_MODE;
    }

    RunCommandToFd(fd, "Notify modem", {"/vendor/bin/modem_svc", "-s"}, CommandOptions::WithTimeout(1).Build());

    pthread_t modemThreadHandle = 0;
    if (getVerboseLoggingEnabled()) {
        ALOGD("Verbose logging is enabled.\n");
        if (handle->numFds < 2) {
            ALOGE("no FD for modem\n");
        } else {
            int fdModem = handle->data[1];
            if (pthread_create(&modemThreadHandle, NULL, dumpModemThread, (void *)((long)fdModem)) != 0) {
                ALOGE("could not create thread for dumpModem\n");
            }
        }
    }

    RunCommandToFd(fd, "VENDOR PROPERTIES", {"/vendor/bin/getprop"});
    DumpFileToFd(fd, "SoC serial number", "/sys/devices/soc0/serial_number");
    DumpFileToFd(fd, "CPU present", "/sys/devices/system/cpu/present");
    DumpFileToFd(fd, "CPU online", "/sys/devices/system/cpu/online");

    DumpF2FS(fd);
    DumpUFS(fd);

    DumpFileToFd(fd, "INTERRUPTS", "/proc/interrupts");
    DumpFileToFd(fd, "Sleep Stats", "/sys/power/system_sleep/stats");
    DumpFileToFd(fd, "Power Management Stats", "/sys/power/rpmh_stats/master_stats");
    DumpFileToFd(fd, "WLAN Power Stats", "/sys/kernel/wlan/power_stats");
    DumpFileToFd(fd, "LL-Stats", "/d/wlan0/ll_stats");
    DumpFileToFd(fd, "WLAN Connect Info", "/d/wlan0/connect_info");
    DumpFileToFd(fd, "WLAN Offload Info", "/d/wlan0/offload_info");
    DumpFileToFd(fd, "WLAN Roaming Stats", "/d/wlan0/roam_stats");
    DumpFileToFd(fd, "ICNSS Stats", "/d/icnss/stats");
    DumpFileToFd(fd, "SMD Log", "/d/ipc_logging/smd/log");
    RunCommandToFd(fd, "ION HEAPS", {"/vendor/bin/sh", "-c", "for d in $(ls -d /d/ion/*); do for f in $(ls $d); do echo --- $d/$f; cat $d/$f; done; done"});
    DumpFileToFd(fd, "dmabuf info", "/d/dma_buf/bufinfo");
    DumpFileToFd(fd, "dmabuf process info", "/d/dma_buf/dmaprocs");
    RunCommandToFd(fd, "Temperatures", {"/vendor/bin/sh", "-c", "for f in /sys/class/thermal/thermal* ; do type=`cat $f/type` ; temp=`cat $f/temp` ; echo \"$type: $temp\" ; done"});
    RunCommandToFd(fd, "Cooling Device Current State", {"/vendor/bin/sh", "-c", "for f in /sys/class/thermal/cooling* ; do type=`cat $f/type` ; temp=`cat $f/cur_state` ; echo \"$type: $temp\" ; done"});
    RunCommandToFd(
        fd, "LMH info",
        {"/vendor/bin/sh", "-c",
         "for f in /sys/bus/platform/drivers/msm_lmh_dcvs/*qcom,limits-dcvs@*/lmh_freq_limit; do "
         "state=`cat $f` ; echo \"$f: $state\" ; done"});
    RunCommandToFd(fd, "CPU time-in-state", {"/vendor/bin/sh", "-c", "for cpu in /sys/devices/system/cpu/cpu*; do f=$cpu/cpufreq/stats/time_in_state; if [ ! -f $f ]; then continue; fi; echo $f:; cat $f; done"});
    RunCommandToFd(fd, "CPU cpuidle", {"/vendor/bin/sh", "-c", "for cpu in /sys/devices/system/cpu/cpu*; do for d in $cpu/cpuidle/state*; do if [ ! -d $d ]; then continue; fi; echo \"$d: `cat $d/name` `cat $d/desc` `cat $d/time` `cat $d/usage`\"; done; done"});
    DumpFileToFd(fd, "MDP xlogs", "/data/vendor/display/mdp_xlog");
    RunCommandToFd(fd, "USB Device Descriptors", {"/vendor/bin/sh", "-c", "cd /sys/bus/usb/devices/1-1 && cat product && cat bcdDevice; cat descriptors | od -t x1 -w16 -N96"});
    RunCommandToFd(fd, "Power supply properties", {"/vendor/bin/sh", "-c", "for f in `ls /sys/class/power_supply/*/uevent` ; do echo \"------ $f\\n`cat $f`\\n\" ; done"});
    RunCommandToFd(fd, "PMIC Votables", {"/vendor/bin/sh", "-c", "cat /sys/kernel/debug/pmic-votable/*/status"});
    RunCommandToFd(fd, "QCOM FG SRAM", {"/vendor/bin/sh", "-c", "echo 0 > /d/fg/sram/address ; echo 500 > /d/fg/sram/count ; cat /d/fg/sram/data"});

    DumpFileToFd(fd, "Modem Stat", "/data/vendor/modem_stat/debug.txt");
    DumpFileToFd(fd, "Pixel trace", "/d/tracing/instances/pixel-trace/trace");

    // Slower dump put later in case stuck the rest of dump
    // Timeout after 3s as TZ log missing EOF
    RunCommandToFd(fd, "QSEE logs", {"/vendor/bin/sh", "-c", "/vendor/bin/timeout 3 cat /d/tzdbg/qsee_log"});
    RunCommandToFd(fd, "TZ logs", {"/vendor/bin/sh", "-c", "/vendor/bin/timeout 3 cat /d/tzdbg/log"});
    RunCommandToFd(fd, "HYP logs", {"/vendor/bin/sh", "-c", "/vendor/bin/timeout 3 cat /d/tzdbg/hyp_log"});

    if (modemThreadHandle) {
        pthread_join(modemThreadHandle, NULL);
    }

    return DumpstateStatus::OK;
}

Return<void> DumpstateDevice::setVerboseLoggingEnabled(const bool enable) {
    android::base::SetProperty(VENDOR_VERBOSE_LOGGING_ENABLED_PROPERTY, enable ? "true" : "false");
    return Void();
}

Return<bool> DumpstateDevice::getVerboseLoggingEnabled() {
    return android::base::GetBoolProperty(VENDOR_VERBOSE_LOGGING_ENABLED_PROPERTY, false);
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace dumpstate
}  // namespace hardware
}  // namespace android
