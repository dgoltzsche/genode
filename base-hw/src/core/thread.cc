/*
 * \brief  Implementation of Thread API interface for core
 * \author Martin Stein
 * \date   2012-01-25
 */

/*
 * Copyright (C) 2012-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* Genode includes */
#include <base/thread.h>
#include <base/env.h>
#include <kernel/log.h>

/* core includes */
#include <platform.h>
#include <platform_thread.h>
#include <kernel/kernel.h>

using namespace Genode;

extern Genode::Native_utcb * _main_thread_utcb;

namespace Kernel { unsigned core_id(); }


Native_utcb * Thread_base::utcb()
{
	if (this) { return _tid.platform_thread->utcb_virt(); }
	return _main_thread_utcb;
}


Thread_base * Thread_base::myself()
{
	/* get thread ident from the aligned base of the stack */
	int dummy = 0;
	addr_t sp = (addr_t)(&dummy);
	enum { SP_MASK = ~((1 << CORE_STACK_ALIGNM_LOG2) - 1) };
	Core_thread_id id = *(Core_thread_id *)((addr_t)sp & SP_MASK);
	return (Thread_base *)id;
}


void Thread_base::_thread_start()
{
	/* this is never called by the main thread */
	Thread_base::myself()->_thread_bootstrap();
	Thread_base::myself()->entry();
}


Thread_base::Thread_base(const char * const label, size_t const stack_size, Type)
{
	_tid.platform_thread = new (platform()->core_mem_alloc())
		Platform_thread(stack_size, label);
}


Thread_base::~Thread_base()
{
	Kernel::log() << __PRETTY_FUNCTION__ << "not implemented\n";
	while (1) ;
}


void Thread_base::start()
{
	/* allocate stack memory that fullfills the constraints for core stacks */
	size_t const size = _tid.platform_thread->stack_size();
	if (size > (1 << CORE_STACK_ALIGNM_LOG2) - sizeof(Core_thread_id)) {
		PERR("stack size does not fit stack alignment of core");
		return;
	}
	void * base;
	Platform * const p = static_cast<Platform *>(platform());
	Range_allocator * const alloc = p->core_mem_alloc();
	if (alloc->alloc_aligned(size, &base, CORE_STACK_ALIGNM_LOG2).is_error()) {
		PERR("failed to allocate stack memory");
		return;
	}
	/* provide thread ident at the aligned base of the stack */
	*(Core_thread_id *)base = (Core_thread_id)this;

	/* set affinity of thread */
	Platform_thread * const platform_thread = _tid.platform_thread;
	unsigned const processor_id = utcb()->core_start_info()->processor_id();
	Affinity::Location location(processor_id, 0, 1, 1);
	platform_thread->affinity(location);

	/* start thread with stack pointer at the top of stack */
	void * sp = (void *)((addr_t)base + size);
	void * ip = (void *)&_thread_start;
	if (platform_thread->start(ip, sp)) {
		PERR("failed to start thread");
		alloc->free(base, size);
		return;
	}
}


void Thread_base::join()
{
	_join_lock.lock();
}


void Thread_base::cancel_blocking()
{
	_tid.platform_thread->cancel_blocking();
}

