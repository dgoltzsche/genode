/*
 * \brief  Thread implementation for LwIP threads.
 * \author Stefan Kalkowski
 * \author Sebastian Sumpf
 * \date   2009-11-14
 */

/*
 * Copyright (C) 2009-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef __LWIP__INCLUDE__THREAD_H__
#define __LWIP__INCLUDE__THREAD_H__

#include <base/env.h>
#include <base/signal.h>
#include <base/exception.h>
#include <cpu/context.h>
#include <util/list.h>
#include <libc/setjmp.h>

extern "C" {
#include <lwip/sys.h>
}

namespace Lwip {
	class Thread;
	class Semaphore;
	class Timeout_handler;

	class Timeout_exception     : public Genode::Exception { };
	class Nonblocking_exception : public Genode::Exception { };
}


class Lwip::Timeout_handler : public Genode::Alarm_scheduler
{
	private:

		Timer::Connection                          _timer;
		Genode::Signal_receiver                   &_receiver;
		Genode::Signal_dispatcher<Timeout_handler> _dispatcher;
		Genode::Alarm::Time _time;     /* current time    */

		void _handle(unsigned)
		{
			_time = _timer.elapsed_ms();

			/* handle timouts of this point in time */
			handle(_time);

			/* schedule next timeout */
			Genode::Alarm::Time n;
			if (next_deadline(&n))
				_timer.trigger_once((n-_time)*1000);
		}

	public:

		Timeout_handler(Genode::Signal_receiver &recv)
		: _receiver(recv),
		  _dispatcher(recv, *this, &Timeout_handler::_handle) {
			_timer.sigh(_dispatcher); }

		Genode::Alarm::Time time(void) { return _time; }

		void schedule_ms(Genode::Alarm *alarm, Genode::Alarm::Time duration)
		{
			_time = _timer.elapsed_ms();
			schedule_absolute(alarm, _time + duration);

			/* if new alarm is earliest tin time, reprogram the timer */
			Genode::Alarm::Time n;
			if (next_deadline(&n) && n == (_time + duration))
				_timer.trigger_once(duration*1000);
		}

		void wait_for_signals()
		{
			Genode::Signal s = _receiver.wait_for_signal();
			static_cast<Genode::Signal_dispatcher_base *>(s.context())->dispatch(s.num());
		}

		static Timeout_handler *alarm_timer();
};


/**
 * Allows pseudo-parallel execution of functions
 */
class Lwip::Thread : public Genode::List<Thread>::Element
{
	private:

		enum { STACK_SIZE = 0x16384 };

		bool                _started;           /* true if already started */
		jmp_buf             _env;               /* state */
		void              (*_func)(void *);     /* function to call*/
		void               *_arg;               /* argument for function */
		char const         *_name;              /* name of this object */
		char                _stack[STACK_SIZE]; /* stack */

		static Thread      *_current;           /* currently scheduled object */
		static Thread      *_idle;
		static bool         _all;               /* true when all objects must be scheduled */

		static const bool verbose = false;


		/**
		 * List containing all registered objects
		 */
		static Genode::List<Thread> *_list()
		{
			static Genode::List<Thread> _l;
			return &_l;
		}

		/**
		 * Start/restore
		 */
		void _run()
		{
			/* will never return */
			if (!_started) {
				_started = true;

				if (verbose)
					PDBG("Start func %s (%p) sp: %p", _name, _func,
					     (void*)(((Genode::addr_t)&_stack) + STACK_SIZE));

				/* switch stack and call '_func(_arg)' */
				platform_execute((void *)(((Genode::addr_t)&_stack) + STACK_SIZE),
				                 (void *)_func, _arg);
			}

			/* restore old state */
			if (verbose)
				PDBG("Schedule %s (%p)", _name, _func);

			_longjmp(_env, 1);
		}

		Thread() : _started(true), _func(0), _arg(0), _name("main") {}

		static Thread *_next()
		{
			if (_current->next())
				return _current->next();
			return _list()->first() ? _list()->first()
			                        : _idle;
		}

		static void _handle_signal(void*)
		{
			while (true) {
				Timeout_handler::alarm_timer()->wait_for_signals();
				Thread::schedule();
			}
		}

	public:

        Thread(char const *name, void (*func)(void*), void *arg)
		: _started(false), _func(func), _arg(arg), _name(name) { }

		static void schedule() __attribute__((noinline))
		{
			if ((_next() == _current) || _setjmp(_current->_env))
				return;

			_current = _next();
			_current->_run();
		}

		static void initialize()
		{
			static Thread main_thread;
			static Thread idle_thread("idle", Thread::_handle_signal, 0);
			_list()->insert(&main_thread);
			_idle    = &idle_thread;
			_current = &main_thread;
		}

		static Thread *current() { return _current; }
		const char    *name()    { return _name;    }

		void run()
		{
			if (this == _idle)
				PERR("idle thread should never be stopped nor run");
			if (verbose)
				PDBG("run %s (%p)", _name, _func);
			_list()->insert(this);
		}

		void stop()
		{
			if (this == _idle)
				PERR("idle thread should never be stopped nor run");
			if (verbose)
				PDBG("stop %s (%p)", _name, _func);
			_list()->remove(this);
		}
};


class Lwip::Semaphore : Genode::List<Thread>
{
	private:

		int _counter;

	protected:

		void _block()
		{
			Thread *t = Thread::current();
			t->stop();
			insert(t);
			Thread::schedule();
		}

		bool _abort(Thread *th)
		{
			/* potentially, the queue is empty */
			if (++_counter <= 0) {

				/*
				 * Iterate through the queue and find the thread,
				 * with the corresponding timeout.
				 */
				Thread *f = first();
				Thread *e = f;

				while (true) {
					if (e)
						remove(e);

					/*
					 * Wakeup the thread.
					 */
					if (th == e) {
						e->run();
						return true;
					}

					/*
					 * Noninvolved threads are enqueued again.
					 */
					insert(e);
					e = e->next();

					/*
					 * Maybe, the alarm was triggered just after the corresponding
					 * thread was already dequeued, that's why we have to track
					 * whether we processed the whole queue.
					 */
					if (e == f)
						break;
				}
			}

			/* The right element was not found, so decrease counter again */
			--_counter;
			return false;
		}


		/**
		 * Represents a timeout associated with the blocking-
		 * operation on a semaphore.
		 */
		class Timeout : public Genode::Alarm
		{
			private:

				Semaphore *_sem;       /* Semaphore we block on */
				Thread    *_thread;    /* thread timeout belongs to */
				bool       _triggered; /* Timeout expired */
				Time       _start;

			public:

				Timeout(Genode::Alarm::Time duration, Semaphore *s, Thread *th)
				: _sem(s), _thread(th), _triggered(false)
				{
					Timeout_handler::alarm_timer()->schedule_ms(this, duration);
					_start = Timeout_handler::alarm_timer()->time();
				}

				void discard(void) { Timeout_handler::alarm_timer()->discard(this); }
				bool triggered(void) { return _triggered; }
				Time start()         { return _start;     }

			protected:

				bool on_alarm()
				{
					/* Abort blocking operation */
					_triggered = _sem->_abort(_thread);
					return false;
				}
		};

	public:

		Semaphore(int n = 0) : _counter(0) { }

		/**
		 * Decrements semaphore and blocks when it's already zero.
		 *
		 * \param t after t milliseconds of blocking a Timeout_exception is thrown.
		 *          if t is zero do not block, instead raise an
		 *          Nonblocking_exception.
		 * \return  milliseconds the caller was blocked
		 */
		Genode::Alarm::Time down(unsigned long ms)
		{
			if (--_counter < 0) {

				/* If ms==0 we shall not block */
				if (ms == 0) {
					++_counter;
					throw Nonblocking_exception();
				}

				Timeout to(ms, this, Thread::current());
				_block();

				/* Deactivate timeout */
				to.discard();

				/*
				 * When we were only woken up, because of a timeout,
				 * throw an exception.
				 */
				if (to.triggered())
					throw Timeout_exception();

				/* return blocking time */
				return Timeout_handler::alarm_timer()->time() - to.start();
			}
			return 0;
		}

		void down()
		{
			if (--_counter < 0)
				_block();
		}

		void up()
		{
			if (++_counter > 0)
				return;

			Thread *t = first();
			if (t) {
				remove(t);
				t->run();
			}
		}
};


#endif //__LWIP__INCLUDE__THREAD_H__
