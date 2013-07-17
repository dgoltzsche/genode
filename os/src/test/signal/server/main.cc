/*
 * \brief  Test for signalling perf (server-side)
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
#include <cap_session/connection.h>
#include <signal_perf.h>
#include <base/rpc_server.h>
#include <root/component.h>

using namespace Genode;

class Signal_perf_component : public Rpc_object<Signal_perf>,
                              public Thread<8192>
{
	private:

		Signal_context            _context;
		Signal_receiver           _receiver;
		Signal_context_capability _context_cap;

	public:

		Signal_perf_component()
		: _context_cap(_receiver.manage(&_context)) { start(); }

		Signal_context_capability context() { return _context_cap; }

		void entry()
		{
			for (;;)
				_receiver.wait_for_signal();
		}
};


class Signal_perf_root : public Root_component<Signal_perf_component>
{
	protected:

		Signal_perf_component *_create_session(const char *args)
		{
			return new (md_alloc()) Signal_perf_component();
		}

	public:

		Signal_perf_root(Rpc_entrypoint *e, Allocator *md_alloc)
		: Root_component<Signal_perf_component>(e, md_alloc) { }
};


/**
 * Main program
 */
int main(int, char **)
{
	static Cap_connection   cap;
	static Rpc_entrypoint   ep(&cap, 8192, "sig_perf_ep");
	static Signal_perf_root root(&ep, env()->heap());
	env()->parent()->announce(ep.manage(&root));
	sleep_forever();
	return 0;
}
