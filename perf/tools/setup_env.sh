#!/bin/sh

# The script setting up a Linux operating system before running
# Tarantool benchmarks, see [1].
#
# 1. https://github.com/tarantool/tarantool/wiki/Benchmarking

set -eu

uid=$(id -u)
if [ "$uid" -ne 0 ]
  then echo "Please run as root."
  exit 1
fi

###
# Helpers.
###

cpu_vendor="unknown"
cpuinfo_vendor=$(awk '/vendor_id/{ print $3; exit }' < /proc/cpuinfo)
if [ "$cpuinfo_vendor" = "GenuineIntel" ]; then
   cpu_vendor="intel"
elif [ "$cpuinfo_vendor" = "AuthenticAMD" ]; then
   cpu_vendor="amd"
else
   echo "Unknown CPU vendor '$cpuinfo_vendor'"
   exit 1
fi

FAILURE_MSG="WARNING"
SUCCESS_MSG="CHECKED"
SKIPPED_MSG="SKIPPED"

set_kernel_setting() {
  desc_msg="$1"
  file_path="$2"
  value="$3"

  if [ -f "$file_path" ]; then
    sh -c "echo $value > $file_path" && status="$SUCCESS_MSG" || status="$FAILURE_MSG"
  else
    status="$SKIPPED_MSG"
  fi
  echo "$desc_msg $status"
}

kernel_setting_is_nonzero() {
  desc_msg="$1"
  file_path="$2"
  hint_msg="$3"

  if [ -f "$file_path" ]; then
    value=$(cat "$file_path")
    if [ -n "$value" ]; then
      status="$SUCCESS_MSG"
    else
      status="$FAILURE_MSG (hint: $hint_msg)"
    fi
  else
    status="$SKIPPED_MSG"
  fi
  echo "$desc_msg $status"
}

###
# Kernel command line parameters.
###

desc_msg="Disable AMD SMT or Intel Hyperthreading "
sysfs_path="/sys/devices/system/cpu/smt/active"
if [ -f "$sysfs_path" ]; then
  is_set=$(cat $sysfs_path)
  err_msg="$FAILURE_MSG (hint: set 'nosmt' kernel parameter)"
  [ "$is_set" = 1 ] && status="$SUCCESS_MSG" || status="$err_msg"
else
  status="$SKIPPED_MSG"
fi
echo "$desc_msg $status"

kernel_setting_is_nonzero \
  "Isolate CPUs for benchmarking" \
  "/sys/devices/system/cpu/isolated" \
  "set 'isolcpus' kernel parameter"

kernel_setting_is_nonzero \
  "Offload interrupts from the isolated CPUs" \
  "/proc/irq/default_smp_affinity" \
  "set 'irqaffinity' kernel parameter"

kernel_setting_is_nonzero \
  "Disable scheduling on single-task isolated CPUs" \
  "/sys/devices/system/cpu/nohz_full" \
  "set 'nohz_full' kernel parameter"

set_kernel_setting \
  "Disable transparent huge pages" \
  "/sys/kernel/mm/transparent_hugepage/enabled" \
  "never"

set_kernel_setting \
  "Disable direct compaction of transparent huge pages" \
  "/sys/kernel/mm/transparent_hugepage/defrag" \
  "never"

###
# System tuning.
###

if [ "$cpu_vendor" = "amd" ]; then
  sysfs_path="/sys/devices/system/cpu/cpufreq/boost"
  value=0
elif [ "$cpu_vendor" = "intel" ]; then
  sysfs_path="/sys/devices/system/cpu/intel_pstate/no_turbo"
  value=1
fi
set_kernel_setting \
  "Disable TurboBoost" \
  "$sysfs_path" \
  "$value"

ncpu=$(getconf _NPROCESSORS_ONLN)
for cpu_id in $(seq 0 1 $((ncpu-1))); do
  sysfs_path_cpu="/sys/devices/system/cpu/cpu$cpu_id/cpufreq/scaling_governor"
  set_kernel_setting \
    "Stabilize the frequency of CPU $cpu_id" \
    "$sysfs_path_cpu" \
    "performance"
done
