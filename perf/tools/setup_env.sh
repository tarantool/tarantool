#!/bin/sh

# The script setting up a Linux operating system before running
# Tarantool benchmarks.
# See https://github.com/tarantool/tarantool/wiki/Benchmarking.

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

###
# Kernel command line parameters.
###

echo -n "Disable AMD SMT or Intel Hyperthreading "
sysfs_path="/sys/devices/system/cpu/smt/active"
if [ -f "$sysfs_path" ]; then
  is_set=$(cat $sysfs_path)
  [ "$is_set" = 1 ] && echo "SUCCESS" || echo "FAILED (hint: set 'nosmt' kernel parameter)"
else
  echo "SKIPPED"
fi

echo -n "Isolate CPUs for benchmarking "
sysfs_path="/sys/devices/system/cpu/isolated"
if [ -f "$sysfs_path" ]; then
  isolcpus=$(cat "$sysfs_path")
  [ -n "$isolcpus" ] && echo "SUCCESS" || echo "FAILED (hint: set 'isolcpus' kernel parameter)"
else
  echo "SKIPPED"
fi

echo -n "Offload interrupts from the isolated CPUs "
sysfs_path="cat /proc/irq/default_smp_affinity"
if [ -f "$sysfs_path" ]; then
  smp_affinity=$(cat "$sysfs_path")
  [ -n "$smp_affinity" ] && echo "SUCCESS" || echo "FAILED (hint: set 'irqaffinity' kernel parameter)"
else
  echo "SKIPPED"
fi

echo -n "Disable scheduling on single-task isolated CPUs "
sysfs_path="/sys/devices/system/cpu/nohz_full"
if [ -f "$sysfs_path" ]; then
  nohz=$(cat "$sysfs_path")
  [ -n "$nohz" ] && echo "SUCCESS" || echo "FAILED (hint: set 'nohz_full' kernel parameter)"
else
  echo "FAILED (hint: enable CONFIG_NO_HZ_FULL in a kernel config)"
fi

echo -n "Disable transparent_hugepage "
sysfs_path="/sys/kernel/mm/transparent_hugepage/enabled"
if [ -f "$sysfs_path" ]; then
  sudo sh -c "echo never > $sysfs_path"
  echo "SUCCESS"
else
  echo "SKIPPED"
fi

echo -n "Disable transparent_hugepage "
sysfs_path="/sys/kernel/mm/transparent_hugepage/defrag"
if [ -f "$sysfs_path" ]; then
  sudo sh -c "echo never > $sysfs_path"
  echo "SUCCESS"
else
  echo "SKIPPED"
fi

###
# System tuning.
###

echo -n "Disable TurboBoost "
if [ "$cpu_vendor" = "amd" ]; then
  sysfs_path="/sys/devices/system/cpu/cpufreq/boost"
  if [ -f "$sysfs_path" ]; then
    sudo sh -c "echo 0 > $sysfs_path"
    echo "SUCCESS"
  else
    echo "SKIPPED"
  fi
elif [ "$cpu_vendor" = "intel" ]; then
  sysfs_path="/sys/devices/system/cpu/intel_pstate/no_turbo"
  if [ -f "$sysfs_path" ]; then
    sudo sh -c "echo 1 > $sysfs_path"
    echo "SUCCESS"
  else
    echo "SKIPPED"
  fi
fi

echo -n "Stabilize the CPU frequency "
sys_path="/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
if [ -f "$sysfs_path" ]; then
  sh -c 'echo performance | sudo tee $sysfs_path'
  echo "SUCCESS"
else
  echo "SKIPPED"
fi

echo -n "Workload-specific: drop disk caches "
proc_path="/proc/sys/vm/drop_caches"
if [ -f "$sysfs_path" ]; then
  sudo sh -c "echo 1 > $proc_path"
  echo "SUCCESS"
else
  echo "SKIPPED"
fi

###
# Prepare the OS environment.
###

echo -n "Workload-specific: Swappiness "
sudo sh -c "sysctl -w vm.swappiness=10 2>/dev/null"
