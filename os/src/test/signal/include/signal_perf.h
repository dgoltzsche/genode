/*
 * \brief  Signal session perf interface
 * \author Stefan Kalkowski
 * \date   2013-07-17
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _SIGNAL_PERF_H_
#define _SIGNAL_PERF_H_

#include <base/capability.h>
#include <base/exception.h>
#include <signal_session/signal_session.h>
#include <session/session.h>
#include <base/rpc_client.h>
#include <base/connection.h>

namespace Genode {

	enum { SIGNAL_COUNT = 84000 };

	struct Signal_perf : Session
	{
		static const char *service_name() { return "SIGPERF"; }

		virtual ~Signal_perf() { }

		virtual Signal_context_capability context() = 0;


		/*********************
		 ** RPC declaration **
		 *********************/

		GENODE_RPC(Rpc_context, Signal_context_capability, context);

		GENODE_RPC_INTERFACE(Rpc_context);
	};


	typedef Capability<Signal_perf> Signal_perf_capability;


	struct Signal_perf_client : Rpc_client<Signal_perf>
	{
		explicit Signal_perf_client(Signal_perf_capability session)
		: Rpc_client<Signal_perf>(session) { }

		Signal_context_capability context() {
			return call<Rpc_context>(); }
	};


	struct Signal_perf_connection : Connection<Signal_perf>, Signal_perf_client
	{
		Signal_perf_connection()
		:
			Connection<Signal_perf>(session("ram_quota=16K")),
			Signal_perf_client(cap())
		{ }
	};
}

#endif /* _SIGNAL_PERF_H_ */
