/*
 *  arch/arm/include/asm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <linux/compiler.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/proc-fns.h>
#include <asm/smp_plat.h>
#include <asm-generic/mm_hooks.h>

void __check_vmalloc_seq(struct mm_struct *mm);

#ifdef CONFIG_CPU_HAS_ASID

void check_and_switch_context(struct mm_struct *mm, struct task_struct *tsk);
#define init_new_context(tsk,mm)	({ atomic64_set(&mm->context.id, 0); 0; })

#ifdef CONFIG_ARM_ERRATA_798181
void a15_erratum_get_cpumask(int this_cpu, struct mm_struct *mm,
			     cpumask_t *mask);
#else  /* !CONFIG_ARM_ERRATA_798181 */
static inline void a15_erratum_get_cpumask(int this_cpu, struct mm_struct *mm,
					   cpumask_t *mask)
{
}
#endif /* CONFIG_ARM_ERRATA_798181 */

#else	/* !CONFIG_CPU_HAS_ASID */

#ifdef CONFIG_MMU

/* IAMROOT-12:
 * -------------
 *  CONFIG_CPU_HAS_ASID 옵션이 없는 경우 아래 함수를 사용하지만,
 * armv7 아키텍처는 디폴트로 CONFIG_CPU_HAS_ASID 커널 옵션을 사용하므로
 * arch/arm/mm/context.c 파일에 있는 함수를 참고해야 한다.
 */
static inline void check_and_switch_context(struct mm_struct *mm,
					    struct task_struct *tsk)
{

	if (unlikely(mm->context.vmalloc_seq != init_mm.context.vmalloc_seq))
		__check_vmalloc_seq(mm);

	if (irqs_disabled())
		/*
		 * cpu_switch_mm() needs to flush the VIVT caches. To avoid
		 * high interrupt latencies, defer the call and continue
		 * running with the old mm. Since we only support UP systems
		 * on non-ASID CPUs, the old mm will remain valid until the
		 * finish_arch_post_lock_switch() call.
		 */
		mm->context.switch_pending = 1;
	else
		cpu_switch_mm(mm->pgd, mm);
}

#define finish_arch_post_lock_switch \
	finish_arch_post_lock_switch
static inline void finish_arch_post_lock_switch(void)
{
	struct mm_struct *mm = current->mm;

	if (mm && mm->context.switch_pending) {
		/*
		 * Preemption must be disabled during cpu_switch_mm() as we
		 * have some stateful cache flush implementations. Check
		 * switch_pending again in case we were preempted and the
		 * switch to this mm was already done.
		 */
		preempt_disable();
		if (mm->context.switch_pending) {
			mm->context.switch_pending = 0;
			cpu_switch_mm(mm->pgd, mm);
		}
		preempt_enable_no_resched();
	}
}

#endif	/* CONFIG_MMU */

#define init_new_context(tsk,mm)	0

#endif	/* CONFIG_CPU_HAS_ASID */

#define destroy_context(mm)		do { } while(0)
#define activate_mm(prev,next)		switch_mm(prev, next, NULL)

/*
 * This is called when "tsk" is about to enter lazy TLB mode.
 *
 * mm:  describes the currently active mm context
 * tsk: task which is entering lazy tlb
 * cpu: cpu number which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
#ifdef CONFIG_MMU
	unsigned int cpu = smp_processor_id();

	/*
	 * __sync_icache_dcache doesn't broadcast the I-cache invalidation,
	 * so check for possible thread migration and invalidate the I-cache
	 * if we're new to this CPU.
	 */

/* IAMROOT-12:
 * -------------
 * armv7 이상 아키텍처들은 캐시 조작에 브로드캐스트 방법이 필요 없다.
 */
	if (cache_ops_need_broadcast() &&
	    !cpumask_empty(mm_cpumask(next)) &&
	    !cpumask_test_cpu(cpu, mm_cpumask(next)))
		__flush_icache_all();

/* IAMROOT-12:
 * -------------
 * 같은 mm을 사용하지 않는 경우에만(프로세스 내부의 스레드들과 mm을 공유한다)
 * 즉 프로세스가 다른 경우에만 mm이 다르므로 mm 스위칭을 수행한다. 
 *
 * (CONTEXTIDR, TTBR0 갱신)
 */
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next) {
		check_and_switch_context(next, tsk);
		if (cache_is_vivt())
			cpumask_clear_cpu(cpu, mm_cpumask(prev));
	}
#endif
}

#define deactivate_mm(tsk,mm)	do { } while (0)

#endif
