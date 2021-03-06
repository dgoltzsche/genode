/*
 * \brief  Exynos5-specific implementation of the Block::Driver interface
 * \author Sebastian Sumpf
 * \date   2013-03-22
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <util/mmio.h>
#include <os/attached_io_mem_dataspace.h>
#include <base/printf.h>
#include <timer_session/connection.h>
#include <block/component.h>

/* local includes */
#include <dwmmc.h>

namespace Block {
	using namespace Genode;
	class Exynos5_driver;
}


class Block::Exynos5_driver : public Block::Driver
{
	private:

		struct Timer_delayer : Timer::Connection, Mmio::Delayer
		{
			/**
			 * Implementation of 'Delayer' interface
			 */
			void usleep(unsigned us)
			{
				/* polling */
				if (us == 0)
					return;

				Timer::Connection::usleep(us);
			}
		} _delayer;

		enum {
			MSH_BASE = 0x12200000, /* host controller base for eMMC */
			MSH_SIZE = 0x10000,
		};


		/* display sub system registers */
		Attached_io_mem_dataspace _mmio;

		/* mobile storage host controller instance */
		Exynos5_msh_controller _controller;

		bool const _use_dma;

	public:

		Exynos5_driver(bool use_dma)
		:
			_mmio(MSH_BASE, MSH_SIZE),
			_controller((addr_t)_mmio.local_addr<void>(),
			            _delayer, use_dma),
			_use_dma(use_dma)
		{
			Sd_card::Card_info const card_info = _controller.card_info();

			PLOG("SD/MMC card detected");
			PLOG("capacity: %zd MiB", card_info.capacity_mb());
		}


		/*****************************
		 ** Block::Driver interface **
		 *****************************/

		Genode::size_t block_size() { return 512; }

		virtual Block::sector_t block_count()
		{
			return _controller.card_info().capacity_mb() * 1024 * 2;
		}

		Block::Session::Operations ops()
		{
			Block::Session::Operations o;
			o.set_operation(Block::Packet_descriptor::READ);
			o.set_operation(Block::Packet_descriptor::WRITE);
			return o;
		}

		void read_dma(Block::sector_t   block_number,
		              Genode::size_t    block_count,
		              Genode::addr_t    phys,
		              Packet_descriptor &packet)
		{
			if (!_controller.read_blocks_dma(block_number, block_count, phys))
				throw Io_error();
			session->ack_packet(packet);
		}

		void write_dma(Block::sector_t    block_number,
		               Genode::size_t     block_count,
		               Genode::addr_t     phys,
		               Packet_descriptor &packet)
		{
			if (!_controller.write_blocks_dma(block_number, block_count, phys))
				throw Io_error();
			session->ack_packet(packet);
		}

		bool dma_enabled() { return _use_dma; }
};

#endif /* _DRIVER_H_ */
