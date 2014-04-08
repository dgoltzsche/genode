/*
 * \brief   Singlethreaded minimalistic kernel
 * \author  Martin Stein
 * \date    2013-09-30
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _KERNEL__KERNEL_H_
#define _KERNEL__KERNEL_H_

#include <kernel/pd.h>

namespace Kernel {
	unsigned core_id();
	Pd* core_pd();
	Mode_transition_control* mtc();
}

#endif /* _KERNEL__KERNEL_H_ */
