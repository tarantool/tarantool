#! /usr/bin/env python
# Append 'pm_' prefix (used to generate pmatomic.h from pmatomic.h.proto)

subst=r"""
_Bool bool
&\(object\)->__val object
\(object\)->__val *(object)
&\(__o\)->__val __o
__CLANG_ATOMICS __PM_CLANG_ATOMICS
__GNUC_ATOMICS __PM_GNUC_ATOMICS
__SYNC_ATOMICS __PM_SYNC_ATOMICS
__atomic_apply_stride __pm_atomic_apply_stride
atomic_compare_exchange_strong pm_atomic_compare_exchange_strong
atomic_compare_exchange_strong_explicit pm_atomic_compare_exchange_strong_explicit
atomic_compare_exchange_weak pm_atomic_compare_exchange_weak
atomic_compare_exchange_weak_explicit pm_atomic_compare_exchange_weak_explicit
atomic_exchange pm_atomic_exchange
atomic_exchange_explicit pm_atomic_exchange_explicit
atomic_fetch_add pm_atomic_fetch_add
atomic_fetch_add_explicit pm_atomic_fetch_add_explicit
atomic_fetch_and pm_atomic_fetch_and
atomic_fetch_and_explicit pm_atomic_fetch_and_explicit
atomic_fetch_or pm_atomic_fetch_or
atomic_fetch_or_explicit pm_atomic_fetch_or_explicit
atomic_fetch_sub pm_atomic_fetch_sub
atomic_fetch_sub_explicit pm_atomic_fetch_sub_explicit
atomic_fetch_xor pm_atomic_fetch_xor
atomic_fetch_xor_explicit pm_atomic_fetch_xor_explicit
atomic_load pm_atomic_load
atomic_load_explicit pm_atomic_load_explicit
atomic_signal_fence pm_atomic_signal_fence
atomic_store pm_atomic_store
atomic_store_explicit pm_atomic_store_explicit
atomic_thread_fence pm_atomic_thread_fence
memory_order pm_memory_order
memory_order_acq_rel pm_memory_order_acq_rel
memory_order_acquire pm_memory_order_acquire
memory_order_consume pm_memory_order_consume
memory_order_relaxed pm_memory_order_relaxed
memory_order_release pm_memory_order_release
memory_order_seq_cst pm_memory_order_seq_cst"""

import sys
import re
data = sys.stdin.read()
for pattern, repl in (ln.split(' ',2) for ln in subst.splitlines() if ln):
    data = re.sub(r'(?<=\W)'+pattern, repl, data)
sys.stdout.write(data)

