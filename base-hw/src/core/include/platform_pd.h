/*
 * \brief   Platform specific part of a Genode protection domain
 * \author  Martin Stein
 * \date    2012-02-12
 */

/*
 * Copyright (C) 2009-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _CORE__INCLUDE__PLATFORM_PD_H_
#define _CORE__INCLUDE__PLATFORM_PD_H_

/* Genode includes */
#include <base/printf.h>
#include <root/root.h>

/* Core includes */
#include <tlb.h>
#include <platform.h>
#include <platform_thread.h>
#include <address_space.h>
#include <slab_align.h>
#include <kernel/kernel.h>

namespace Genode
{
	class Platform_thread;

	/**
	 * Platform specific part of a Genode protection domain
	 */
	class Platform_pd : public Address_space
	{
		protected:

			unsigned                  _id;
			Native_capability         _parent;
			Native_thread_id          _main_thread;
			char const * const        _label;
			Tlb                     * _tlb;
			uint8_t                   _kernel_pd[sizeof(Kernel::Pd)];
			Physical_slab_allocator * _pslab;

		public:

			/**
			 * Constructor for core pd
			 */
			Platform_pd(Tlb * tlb, Physical_slab_allocator * slab)
			: _main_thread(0), _label("core"), _tlb(tlb), _pslab(slab) { }

			/**
			 * Constructor for non-core pd
			 */
			Platform_pd(char const *label) : _main_thread(0), _label(label)
			{
				Core_mem_allocator * cma =
					static_cast<Core_mem_allocator*>(platform()->core_mem_alloc());
				void *tlb;

				/* get some aligned space for the kernel object */
				if (!cma->alloc_aligned(sizeof(Tlb), (void**)&tlb,
				                        Tlb::ALIGNM_LOG2).is_ok()) {
					PERR("failed to allocate kernel object");
					throw Root::Quota_exceeded();
				}

				_tlb = new (tlb) Tlb();
				_pslab = new (cma) Aligned_slab<1<<10, 32, 10>(cma);
				Kernel::mtc()->map(_tlb, _pslab);

				/* create kernel object */
				_id = Kernel::new_pd(&_kernel_pd, this);
				if (!_id) {
					PERR("failed to create kernel object");
					throw Root::Unavailable();
				}
			}

			/**
			 * Destructor
			 */
			~Platform_pd();

			/**
			 * Bind thread 't' to protection domain
			 *
			 * \return  0  on success or
			 *         -1  if failed
			 */
			int bind_thread(Platform_thread * t)
			{
				/* is this the first and therefore main thread in this PD? */
				if (!_main_thread)
				{
					/* annotate that we've got a main thread from now on */
					_main_thread = t->id();
					return t->join_pd(this, 1, Address_space::weak_ptr());
				}
				return t->join_pd(this, 0, Address_space::weak_ptr());
			}

			/**
			 * Assign parent interface to protection domain
			 */
			int assign_parent(Native_capability parent)
			{
				if (!parent.valid()) {
					PERR("parent invalid");
					return -1;
				}
				_parent = parent;
				return 0;
			}


			/***************
			 ** Accessors **
			 ***************/

			unsigned const            id()        { return _id; }
			char const * const        label()     { return _label; }
			Tlb *                     tlb()       { return _tlb; }
			Physical_slab_allocator * page_slab() { return _pslab; };
			void page_slab(Physical_slab_allocator *pslab) { _pslab = pslab; };

			void * tlb_phys_addr() const
			{
				Core_mem_allocator * cma =
					static_cast<Core_mem_allocator*>(platform()->core_mem_alloc());
				return cma->phys_addr(_tlb);
			}


			/*****************************
			 ** Address-space interface **
			 *****************************/

			void flush(addr_t, size_t) { PDBG("not implemented"); }
	};
}

#endif /* _CORE__INCLUDE__PLATFORM_PD_H_ */

