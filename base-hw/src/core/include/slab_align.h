/*
 * \brief  Slab allocator with aligned slab entries
 * \author Stefan Kalkowski
 * \date   2014-03-04
 */

/*
 * Copyright (C) 2014 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _CORE__INCLUDE__SLAB_ALIGN_H_
#define _CORE__INCLUDE__SLAB_ALIGN_H_

#include <base/allocator.h>
#include <base/stdint.h>
#include <util/list.h>
#include <util/bit_allocator.h>

#include <core_mem_alloc.h>

namespace Genode {
	class Physical_slab_allocator;

	template <unsigned SLAB_SIZE, unsigned SLABS_PER_BLOCK, unsigned ALIGN_LOG2,
	          typename ALLOCATOR> class Aligned_slab;
}


class Genode::Physical_slab_allocator : public Genode::Allocator
{
	public:

		virtual void * alloc()               = 0;
		virtual void   free(void *addr)      = 0;
		virtual void * phys_addr(void *addr) = 0;
		virtual void * virt_addr(void *addr) = 0;
};


/**
 * Slab allocator returning aligned slab entries.
 */
template <unsigned SLAB_SIZE, unsigned SLABS_PER_BLOCK, unsigned ALIGN_LOG2,
          typename ALLOCATOR = Genode::Core_mem_allocator>
class Genode::Aligned_slab : public Genode::Physical_slab_allocator
{
	private:

		struct Slab_block
		{
			uint8_t                        data[SLAB_SIZE*SLABS_PER_BLOCK];
			Bit_allocator<SLABS_PER_BLOCK> indices;
			List_element<Slab_block>       list_elem;
			size_t                         ref_counter;

			Slab_block() : list_elem(this), ref_counter(0) {}

			void* alloc()
			{
				ref_counter++;
				size_t off = indices.alloc() * SLAB_SIZE;
				return (void*)((Genode::addr_t)&data + off);
			}

			bool free(void *addr)
			{
				if (addr < &data || addr > &indices) return false;
				ref_counter--;
				size_t off = (addr_t)addr - (addr_t)&data;
				indices.free(off / SLAB_SIZE);
				return true;
			}

			void * operator new (size_t, void * p) { return p; }
		};

		Slab_block _initial_sb __attribute__((aligned(1 << ALIGN_LOG2)));

		List<List_element<Slab_block> > _b_list;
		ALLOCATOR                      *_backing_store;
		size_t                          _free_slab_entries;

		void _alloc_slab_block()
		{
			void *p;
			if (!_backing_store->alloc_aligned(sizeof(Slab_block), &p,
			                                   ALIGN_LOG2).is_ok())
				throw Out_of_memory();
			Slab_block *b = new (p) Slab_block();
			_b_list.insert(&b->list_elem);
			_free_slab_entries += SLABS_PER_BLOCK;
		}

		void _free_slab_block(Slab_block * b)
		{
			if (b == &_initial_sb) return;

			_b_list.remove(&b->list_elem);
			destroy(_backing_store, b);
			_free_slab_entries -= SLABS_PER_BLOCK;
		}

		size_t _slab_blocks_in_use()
		{
			size_t cnt = 0;
			for (List_element<Slab_block> *le = _b_list.first();
			     le; le = le->next(), cnt++) ;
			return cnt;
		}

	public:

		static constexpr size_t SLAB_BLOCK_SIZE = sizeof(Slab_block);

		Aligned_slab(ALLOCATOR *backing_store)
		: _backing_store(backing_store), _free_slab_entries(SLABS_PER_BLOCK) {
			_b_list.insert(&_initial_sb.list_elem); }

		~Aligned_slab()
		{
			while (_b_list.first())
				_free_slab_block(_b_list.first()->object());
		}

		void *alloc()
		{
			if (!_free_slab_entries) _alloc_slab_block();

			for (List_element<Slab_block> *le = _b_list.first();
			     le; le = le->next()) {
				if (le->object()->ref_counter == SLABS_PER_BLOCK)
					continue;

				_free_slab_entries--;
				return le->object()->alloc();
			}

			return 0;
		}

		void free(void *addr)
		{
			for (List_element<Slab_block> *le = _b_list.first();
			     le; le = le->next()) {
				if (!le->object()->free(addr)) continue;

				_free_slab_entries++;
				if (!le->object()->ref_counter)
					_free_slab_block(le->object());
			}
		}

		void * phys_addr(void * addr) {
			return _backing_store->phys_addr(addr); }
		void * virt_addr(void * addr) {
			return _backing_store->virt_addr(addr); }

		/**
		 * Allocator interface
		 */
		bool   alloc(size_t, void **addr) { return (*addr = alloc()); }
		void   free(void *addr, size_t) { free(addr); }
		size_t consumed() { return SLAB_BLOCK_SIZE * _slab_blocks_in_use(); }
		size_t overhead(size_t) { return SLAB_BLOCK_SIZE/SLABS_PER_BLOCK; }
		bool   need_size_for_free() const override { return false; }
};


#endif /* _CORE__INCLUDE__SLAB_ALIGN_H_ */
