/*
 * \brief  LwIP ethernet interface
 * \author Stefan Kalkowski
 * \date   2009-11-05
 */

/*
 * Copyright (C) 2009-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* LwIP includes */
extern "C" {
#include <lwip/opt.h>
#include <lwip/def.h>
#include <lwip/mem.h>
#include <lwip/pbuf.h>
#include <lwip/sys.h>
#include <lwip/stats.h>
#include <lwip/snmp.h>
#include <netif/etharp.h>
#include <netif/ppp_oe.h>
#include <nic.h>
#include <verbose.h>
}

/* Genode includes */
#include <base/thread.h>
#include <base/printf.h>
#include <nic/packet_allocator.h>
#include <nic_session/connection.h>

namespace Lwip {
	class Nic;
}


extern "C" {
	static void genode_netif_input(struct netif *netif);
}

class Lwip::Nic
{
	private:

		enum {
			PACKET_SIZE = ::Nic::Packet_allocator::DEFAULT_PACKET_SIZE,
			BUF_SIZE    = ::Nic::Session::QUEUE_SIZE * PACKET_SIZE,
		};

		::Nic::Packet_allocator _tx_block_alloc;
		::Nic::Connection       _nic;       /* nic-session */
		Packet_descriptor       _rx_packet; /* actual packet received */
		struct netif           *_netif;     /* LwIP network interface structure */

		Genode::Signal_dispatcher<Nic> _sink_ack;
		Genode::Signal_dispatcher<Nic> _sink_submit;
		Genode::Signal_dispatcher<Nic> _source_ack;
		Genode::Signal_dispatcher<Nic> _source_submit;

		void _dummy(unsigned) {}

		void _ready_to_submit(unsigned)
		{
			/* as long as packets are available, and we can ack them */
			while (_nic.rx()->packet_avail()) {
				_rx_packet = _nic.rx()->get_packet();

				if (!_rx_packet.valid()) continue;
				genode_netif_input(_netif);

				if (!_nic.rx()->ready_to_ack()) {
					if (verbose)
						PWRN("ack state FULL");
					return;
				}

				/* Acknowledge the packet */
				_nic.rx()->acknowledge_packet(_rx_packet);
			}
		}

		void _ready_to_ack(unsigned)
		{
			/* check for acknowledgements */
			while (_nic.tx()->ack_avail()) {
				Packet_descriptor acked_packet = _nic.tx()->get_acked_packet();
				_nic.tx()->release_packet(acked_packet);
			}
		}

	public:

		Nic(struct netif *netif, Genode::Signal_receiver &recv)
		: _tx_block_alloc(Genode::env()->heap()),
		  _nic(&_tx_block_alloc, BUF_SIZE, BUF_SIZE),
		  _netif(netif),
		  _sink_ack(recv,      *this, &Nic::_dummy),
		  _sink_submit(recv,   *this, &Nic::_ready_to_submit),
		  _source_ack(recv,    *this, &Nic::_ready_to_ack),
		  _source_submit(recv, *this, &Nic::_dummy)
		{
			_nic.rx_channel()->sigh_ready_to_ack(_sink_ack);
			_nic.rx_channel()->sigh_packet_avail(_sink_submit);
			_nic.tx_channel()->sigh_ack_avail(_source_ack);
			_nic.tx_channel()->sigh_ready_to_submit(_source_submit);
		}

		::Nic::Connection  *nic()     { return &_nic;      };
		Packet_descriptor rx_packet() { return _rx_packet; };
};

extern Genode::Signal_receiver receiver;

/*
 * C-interface
 */
extern "C" {

	/**
	 * This function should do the actual transmission of the packet. The packet is
	 * contained in the pbuf that is passed to the function. This pbuf
	 * might be chained.
	 *
	 * @param netif the lwip network interface structure for this genode_netif
	 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
	 * @return ERR_OK if the packet could be sent
	 *         an err_t value if the packet couldn't be sent
	 *
	 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
	 *       strange results. You might consider waiting for space in the DMA queue
	 *       to become availale since the stack doesn't retry to send a packet
	 *       dropped because of memory failure (except for the TCP timers).
	 */
	static err_t
	low_level_output(struct netif *netif, struct pbuf *p)
	{
		Lwip::Nic *nic = reinterpret_cast<Lwip::Nic*>(netif->state);

#if ETH_PAD_SIZE
		pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
		try {
			Packet_descriptor tx_packet = nic->nic()->tx()->alloc_packet(p->tot_len);
			char *tx_content            = nic->nic()->tx()->packet_content(tx_packet);

			/*
			 * Iterate through all pbufs and
			 * copy payload into packet's payload
			 */
			for(struct pbuf *q = p; q != NULL; q = q->next) {
				char *src = (char*) q->payload;
				Genode::memcpy(tx_content, src, q->len);
				tx_content += q->len;
			}

			/* Submit packet */
			nic->nic()->tx()->submit_packet(tx_packet);

#if ETH_PAD_SIZE
			pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
			LINK_STATS_INC(link.xmit);

		} catch(::Nic::Session::Tx::Source::Packet_alloc_failed) {
			return ERR_MEM;
		}

		return ERR_OK;
	}


	/**
	 * Should allocate a pbuf and transfer the bytes of the incoming
	 * packet from the interface into the pbuf.
	 *
	 * @param netif the lwip network interface structure for this genode_netif
	 * @return a pbuf filled with the received packet (including MAC header)
	 *         NULL on memory error
	 */
	static struct pbuf *
	low_level_input(struct netif *netif)
	{
		Lwip::Nic        *nic        = reinterpret_cast<Lwip::Nic*>(netif->state);
		Packet_descriptor rx_packet  = nic->rx_packet();
		char             *rx_content = nic->nic()->rx()->packet_content(rx_packet);
		u16_t             len        = rx_packet.size();

#if ETH_PAD_SIZE
		len += ETH_PAD_SIZE; /* allow room for Ethernet padding */
#endif

		/* We allocate a pbuf chain of pbufs from the pool. */
		struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
		if (p) {
#if ETH_PAD_SIZE
			pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif

			/*
			 * We iterate over the pbuf chain until we have read the entire
			 * packet into the pbuf.
			 */
			for(struct pbuf *q = p; q != 0; q = q->next) {
				char *dst = (char*)q->payload;
				Genode::memcpy(dst, rx_content, q->len);
				rx_content += q->len;
			}

#if ETH_PAD_SIZE
			pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
			LINK_STATS_INC(link.recv);
		} else {
			LINK_STATS_INC(link.memerr);
			LINK_STATS_INC(link.drop);
		}
		return p;
	}


	/**
	 * This function should be called when a packet is ready to be read
	 * from the interface. It uses the function low_level_input() that
	 * should handle the actual reception of bytes from the network
	 * interface. Then the type of the received packet is determined and
	 * the appropriate input function is called.
	 *
	 * @param netif the lwip network interface structure for this genode_netif
	 */
	static void genode_netif_input(struct netif *netif)
	{
		/*
		 * Move received packet into a new pbuf,
		 * if something went wrong, return silently
		 */
		struct pbuf *p = low_level_input(netif);

		/* No packet could be read, silently ignore this */
		if (p == NULL) return;

		if (netif->input(p, netif) != ERR_OK) {
			if (verbose)
				PERR("genode_netif_input: input error");
			pbuf_free(p);
			p = 0;
		}
	}


	/**
	 * Should be called at the beginning of the program to set up the
	 * network interface. It calls the function low_level_init() to do the
	 * actual setup of the hardware.
	 *
	 * This function should be passed as a parameter to netif_add().
	 *
	 * @param netif the lwip network interface structure for this genode_netif
	 * @return ERR_OK if the loopif is initialized
	 *         ERR_MEM if private data couldn't be allocated
	 *         any other err_t on error
	 */
	err_t genode_netif_init(struct netif *netif)
	{
		using namespace Genode;
		LWIP_ASSERT("netif != NULL", (netif != NULL));

		try {
			Lwip::Nic *nic = new (env()->heap()) Lwip::Nic(netif, receiver);

			/* Store nic object address in user-defined netif struct part */
			netif->state      = (void*) nic;
#if LWIP_NETIF_HOSTNAME
			netif->hostname   = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */
			netif->name[0]    = 'e';
			netif->name[1]    = 'n';
			netif->output     = etharp_output;
			netif->linkoutput = low_level_output;
			netif->mtu        = 1500;
			netif->hwaddr_len = ETHARP_HWADDR_LEN;
			netif->flags      = NETIF_FLAG_BROADCAST |
			                    NETIF_FLAG_ETHARP    |
			                    NETIF_FLAG_LINK_UP;

			/* Get MAC address from nic-session and set it accordingly */
			Nic::Mac_address _mac = nic->nic()->mac_address();
			for(int i=0; i<6; ++i)
				netif->hwaddr[i] = _mac.addr[i];

			return ERR_OK;
		} catch (...) {
			return ERR_IF;
		}
	}
} /* extern "C" */
