/*
 * \brief   TLB driver for core
 * \author  Martin Stein
 * \date    2012-02-22
 */

/*
 * Copyright (C) 2012-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _TLB__ARM_V7_H_
#define _TLB__ARM_V7_H_

/* core includes */
#include <tlb/arm.h>
#include <processor_driver.h>

namespace Arm_v7
{
	/**
	 * First level translation table
	 */
	class Section_table;
}

class Arm_v7::Section_table : public Arm::Section_table
{
	private:

		typedef Arm::Section_table Base;
		typedef Genode::addr_t     addr_t;
		typedef Genode::size_t     size_t;

	public:

		/**
		 * Link to second level translation-table
		 */
		struct Page_table_descriptor : Base::Page_table_descriptor
		{
			/**
			 * Compose descriptor value
			 */
			static access_t create(Arm::Page_table * const pt,
			                       Section_table * const st)
			{
				return Base::Page_table_descriptor::create(pt);
			}
		};

		/**
		 * Section translation descriptor
		 */
		struct Section : Base::Section
		{
			/**
			 * Compose descriptor value
			 */
			static access_t create(Page_flags const & flags,
			                       addr_t const pa, Section_table * const st)
			{
				return Base::Section::create(flags, pa);
			}
		};

	public:

		/**
		 * Insert one atomic translation into this table
		 *
		 * For details see 'Base::insert_translation'
		 */
		void insert_translation(addr_t const vo, addr_t const pa,
		                          size_t const size_log2,
		                          Page_flags const & flags,
		                          Physical_slab_allocator * slab)
		{
			Base::insert_translation(vo, pa, size_log2, flags, this, slab);
		}
};


template <typename T>
static typename T::access_t
Arm::memory_region_attr(Page_flags const & flags)
{
	typedef typename T::Tex Tex;
	typedef typename T::C C;
	typedef typename T::B B;
	if (flags.device) { return Tex::bits(2); }
	if (flags.cacheable) { return Tex::bits(5) | B::bits(1); }
	return Tex::bits(6) | C::bits(1);
}


#endif /* _TLB__ARM_V7_H_ */
