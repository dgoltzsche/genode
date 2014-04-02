/*
 * \brief  Translation lookaside buffer
 * \author Martin Stein
 * \date   2012-04-23
 */

/*
 * Copyright (C) 2012 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _EXYNOS5__TLB_H_
#define _EXYNOS5__TLB_H_

/* core includes */
#include <board.h>
#include <tlb/arm_v7.h>

namespace Genode
{
	class Tlb : public Arm_v7::Section_table
	{
	public:

		void * operator new (Genode::size_t, void * p) { return p; }
	};
}

#endif /* _EXYNOS5__TLB_H_ */

