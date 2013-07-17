/*
 * \brief  Test for signalling performance
 * \author Stefan Kalkowski
 * \date   2013-07-17
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include <base/printf.h>
#include <base/signal.h>
#include <base/sleep.h>
#include <signal_perf.h>

namespace Fiasco {
#include <l4/sys/thread.h>
}

using namespace Genode;

/**
 * Main program
 */
int main(int, char **)
{
	static Signal_perf_connection perf;
	Signal_transmitter transmitter(perf.context());

	printf("--- signalling perf test ---\n");

	for (unsigned i = 0; i < SIGNAL_COUNT; i++) {
		transmitter.submit();
		 Fiasco::l4_thread_yield();
	}

	printf("--- signalling perf test finished ---\n");
	sleep_forever();
	return 0;
}
