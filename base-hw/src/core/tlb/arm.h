/*
 * \brief   TLB driver for core
 * \author  Martin Stein
 * \author  Stefan Kalkowski
 * \date    2012-02-22
 */

/*
 * Copyright (C) 2012-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _TLB__ARM_H_
#define _TLB__ARM_H_

/* Genode includes */
#include <util/register.h>
#include <base/printf.h>

/* base-hw includes */
#include <assert.h>
#include <tlb/page_flags.h>
#include <slab_align.h>

namespace Arm
{
	using namespace Genode;

	/**
	 * Check if 'p' is aligned to 1 << 'alignm_log2'
	 */
	inline bool aligned(addr_t const a, size_t const alignm_log2) {
		return a == ((a >> alignm_log2) << alignm_log2); }

	/**
	 * Return permission configuration according to given mapping flags
	 *
	 * \param T      targeted translation-table-descriptor type
	 * \param flags  mapping flags
	 *
	 * \return  descriptor value with AP and XN set and the rest left zero
	 */
	template <typename T>
	static typename T::access_t
	access_permission_bits(Page_flags const & flags)
	{
		typedef typename T::Xn Xn;
		typedef typename T::Ap Ap;
		typedef typename T::access_t access_t;
		bool const w = flags.writeable;
		bool const p = flags.privileged;
		access_t ap;
		if (w) { if (p) { ap = Ap::bits(0b001); }
		         else   { ap = Ap::bits(0b011); }
		} else { if (p) { ap = Ap::bits(0b101); }
		         else   { ap = Ap::bits(0b010); }
		}
		return Xn::bits(!flags.executable) | ap;
	}

	/**
	 * Memory region attributes for the translation descriptor 'T'
	 */
	template <typename T>
	static typename T::access_t
	memory_region_attr(Page_flags const & flags);

	class Double_insertion {};

	/**
	 * Second level translation table
	 */
	class Page_table
	{
		public:

			enum {
				_1KB_LOG2   = 10,
				SIZE_LOG2   = _1KB_LOG2,
				SIZE        = 1 << SIZE_LOG2,
				ALIGNM_LOG2 = SIZE_LOG2,
			};

		protected:

			/**
			 * Common descriptor structure
			 */
			struct Descriptor : Register<32>
			{
				/**
				 * Descriptor types
				 */
				enum Type { FAULT, SMALL_PAGE };

				enum {
					_4KB_LOG2        = 12,
					VIRT_SIZE_LOG2   = _4KB_LOG2,
					VIRT_SIZE        = 1 << VIRT_SIZE_LOG2,
					VIRT_OFFSET_MASK = (1 << VIRT_SIZE_LOG2) - 1,
					VIRT_BASE_MASK   = ~(VIRT_OFFSET_MASK),
				};

				struct Type_0 : Bitfield<0, 2> { };
				struct Type_1 : Bitfield<1, 1> { };

				/**
				 * Get descriptor type of 'v'
				 */
				static Type type(access_t const v)
				{
					access_t const t0 = Type_0::get(v);
					if (t0 == 0) { return FAULT; }
					access_t const t1 = Type_1::get(v);
					if (t1 == 1) return SMALL_PAGE;
					return FAULT;
				}

				/**
				 * Set descriptor type of 'v'
				 */
				static void type(access_t & v, Type const t)
				{
					switch (t) {
					case FAULT:
						Type_0::set(v, 0);
						return;
					case SMALL_PAGE:
						Type_1::set(v, 1);
						return;
					}
				}

				/**
				 * Invalidate descriptor 'v'
				 */
				static void invalidate(access_t & v) { type(v, FAULT); }

				/**
				 * Return if descriptor 'v' is valid
				 */
				static bool valid(access_t & v) { return type(v) != FAULT; }
			};

			/**
			 * Small page descriptor structure
			 */
			struct Small_page : Descriptor
			{
				struct Xn       : Bitfield<0, 1> { };   /* execute never */
				struct B        : Bitfield<2, 1> { };   /* mem region attr. */
				struct C        : Bitfield<3, 1> { };   /* mem region attr. */
				struct Ap_0     : Bitfield<4, 2> { };   /* access permission */
				struct Tex      : Bitfield<6, 3> { };   /* mem region attr. */
				struct Ap_1     : Bitfield<9, 1> { };   /* access permission */
				struct S        : Bitfield<10, 1> { };  /* shareable bit */
				struct Ng       : Bitfield<11, 1> { };  /* not global bit */
				struct Pa_31_12 : Bitfield<12, 20> { }; /* physical base */

				struct Ap : Bitset_2<Ap_0, Ap_1> { }; /* access permission */

				/**
				 * Compose descriptor value
				 */
				static access_t create(Page_flags const & flags,
				                       addr_t const pa)
				{
					access_t v = access_permission_bits<Small_page>(flags);
					v |= memory_region_attr<Small_page>(flags);
					v |= Ng::bits(!flags.global);
					v |= S::bits(1);
					v |= Pa_31_12::masked(pa);
					Descriptor::type(v, Descriptor::SMALL_PAGE);
					return v;
				}
			};

			/*
			 * Table payload
			 *
			 * Must be the only member of this class
			 */
			Descriptor::access_t _entries[SIZE/sizeof(Descriptor::access_t)];

			enum { MAX_INDEX = sizeof(_entries) / sizeof(_entries[0]) - 1 };

			/**
			 * Get entry index by virtual offset
			 *
			 * \param i   is overridden with the index if call returns 0
			 * \param vo  virtual offset relative to the virtual table base
			 *
			 * \retval  0  on success
			 * \retval <0  translation failed
			 */
			bool _index_by_vo (unsigned & i, addr_t const vo) const
			{
				if (vo > max_virt_offset()) return false;
				i = vo >> Descriptor::VIRT_SIZE_LOG2;
				return true;
			}

		public:

			/**
			 * Constructor
			 */
			Page_table()
			{
				/* check table alignment */
				assert(aligned((addr_t)this, ALIGNM_LOG2));

				/* start with an empty table */
				memset(&_entries, 0, sizeof(_entries));
			}

			/**
			 * Maximum virtual offset that can be translated by this table
			 */
			static addr_t max_virt_offset()
			{
				return (MAX_INDEX << Descriptor::VIRT_SIZE_LOG2)
				       + (Descriptor::VIRT_SIZE - 1);
			}

			/**
			 * Insert one atomic translation into this table
			 *
			 * \param vo         offset of the virtual region represented
			 *                   by the translation within the virtual
			 *                   region represented by this table
			 * \param pa         base of the physical backing store
			 * \param size_log2  log2(Size of the translated region),
			 *                   must be supported by this table
			 * \param flags      mapping flags
			 *
			 * This method overrides an existing translation in case
			 * that it spans the the same virtual range and is not
			 * a link to another table level.
			 */
			void insert_translation(addr_t const vo, addr_t const pa,
			                        size_t const size_log2,
			                        Page_flags const & flags)
			{
				/* validate virtual address */
				unsigned i;
				assert(_index_by_vo(i, vo));
				assert(size_log2 == Descriptor::VIRT_SIZE_LOG2);

				if (Descriptor::valid(_entries[i]) &&
				    _entries[i] != Small_page::create(flags, pa))
						throw Double_insertion();

				/* compose new descriptor value */
				_entries[i] = Small_page::create(flags, pa);
			}

			/**
			 * Remove translations that overlap with a given virtual region
			 *
			 * \param vo    region offset within the tables virtual region
			 * \param size  region size
			 */
			void remove_region(addr_t vo, size_t const size)
			{
				assert (vo < (vo + size));

				addr_t const ve = vo + size;
				for (unsigned i = 0; (vo < ve) && _index_by_vo(i, vo);
					 vo = (vo + Descriptor::VIRT_SIZE)
					      & Descriptor::VIRT_BASE_MASK) {

					switch (Descriptor::type(_entries[i])) {

					case Descriptor::SMALL_PAGE:
						{
							Descriptor::invalidate(_entries[i]);
						}

					default: ;
					}
				}
			}

			/**
			 * Does this table solely contain invalid entries
			 */
			bool empty()
			{
				for (unsigned i = 0; i <= MAX_INDEX; i++)
					if (Descriptor::valid(_entries[i])) return false;
				return true;
			}

	} __attribute__((aligned(1<<Page_table::ALIGNM_LOG2)));

	/**
	 * First level translation table
	 */
	class Section_table
	{
		enum { DOMAIN = 0 };

		public:

			enum {
				_16KB_LOG2  = 14,
				SIZE_LOG2   = _16KB_LOG2,
				SIZE        = 1 << SIZE_LOG2,
				ALIGNM_LOG2 = SIZE_LOG2,

				MAX_COSTS_PER_TRANSLATION = sizeof(Page_table),

				MAX_PAGE_SIZE_LOG2 = 20,
				MIN_PAGE_SIZE_LOG2 = 12,
			};

			/**
			 * A first level translation descriptor
			 */
			struct Descriptor : Register<32>
			{
				/**
				 * Descriptor types
				 */
				enum Type { FAULT, PAGE_TABLE, SECTION };

				enum {
					_1MB_LOG2        = 20,
					VIRT_SIZE_LOG2   = _1MB_LOG2,
					VIRT_SIZE        = 1 << VIRT_SIZE_LOG2,
					VIRT_OFFSET_MASK = (1 << VIRT_SIZE_LOG2) - 1,
					VIRT_BASE_MASK   = ~VIRT_OFFSET_MASK,
				};

				struct Type_0   : Bitfield<0, 2> { };
				struct Type_1_0 : Bitfield<1, 1> { };
				struct Type_1_1 : Bitfield<18, 1> { };
				struct Type_1   : Bitset_2<Type_1_0, Type_1_1> { };

				/**
				 * Get descriptor type of 'v'
				 */
				static Type type(access_t const v)
				{
					access_t const t0 = Type_0::get(v);
					if (t0 == 0) { return FAULT; }
					if (t0 == 1) { return PAGE_TABLE; }
					access_t const t1 = Type_1::get(v);
					if (t1 == 1) { return SECTION; }
					return FAULT;
				}

				/**
				 * Set descriptor type of 'v'
				 */
				static void type(access_t & v, Type const t)
				{
					switch (t) {
					case FAULT:
						Type_0::set(v, 0);
						return;
					case PAGE_TABLE:
						Type_0::set(v, 1);
						return;
					case SECTION:
						Type_1::set(v, 1);
						return;
					}
				}

				/**
				 * Invalidate descriptor 'v'
				 */
				static void invalidate(access_t & v) { type(v, FAULT); }

				/**
				 * Return if descriptor 'v' is valid
				 */
				static bool valid(access_t & v) { return type(v) != FAULT; }
			};

			/**
			 * Link to a second level translation table
			 */
			struct Page_table_descriptor : Descriptor
			{
				struct Domain   : Bitfield<5, 4> { };   /* domain */
				struct Pa_31_10 : Bitfield<10, 22> { }; /* physical base */

				/**
				 * Compose descriptor value
				 */
				static access_t create(Page_table * const pt)
				{
						access_t v = Domain::bits(DOMAIN) |
						             Pa_31_10::masked((addr_t)pt);
						Descriptor::type(v, Descriptor::PAGE_TABLE);
						return v;
				}
			};

			/**
			 * Section translation descriptor
			 */
			struct Section : Descriptor
			{
				struct B        : Bitfield<2, 1> { };   /* mem. region attr. */
				struct C        : Bitfield<3, 1> { };   /* mem. region attr. */
				struct Xn       : Bitfield<4, 1> { };   /* execute never bit */
				struct Domain   : Bitfield<5, 4> { };   /* domain */
				struct Ap_0     : Bitfield<10, 2> { };  /* access permission */
				struct Tex      : Bitfield<12, 3> { };  /* mem. region attr. */
				struct Ap_1     : Bitfield<15, 1> { };  /* access permission */
				struct S        : Bitfield<16, 1> { };  /* shared */
				struct Ng       : Bitfield<17, 1> { };  /* not global */
				struct Pa_31_20 : Bitfield<20, 12> { }; /* physical base */

				struct Ap : Bitset_2<Ap_0, Ap_1> { }; /* access permission */

				/**
				 * Compose descriptor value
				 */
				static access_t create(Page_flags const & flags,
				                       addr_t const pa)
				{
					access_t v = access_permission_bits<Section>(flags);
					v |= memory_region_attr<Section>(flags);
					v |= Domain::bits(DOMAIN);
					v |= S::bits(1);
					v |= Ng::bits(!flags.global);
					v |= Pa_31_20::masked(pa);
					Descriptor::type(v, Descriptor::SECTION);
					return v;
				}
			};

		protected:

			/* table payload, must be the first member of this class */
			Descriptor::access_t _entries[SIZE/sizeof(Descriptor::access_t)];

			enum { MAX_INDEX = sizeof(_entries) / sizeof(_entries[0]) - 1 };

			/**
			 * Get entry index by virtual offset
			 *
			 * \param i    is overridden with the resulting index
			 * \param vo   offset within the virtual region represented
			 *             by this table
			 *
			 * \retval  0  on success
			 * \retval <0  if virtual offset couldn't be resolved,
			 *             in this case 'i' reside invalid
			 */
			bool _index_by_vo(unsigned & i, addr_t const vo) const
			{
				if (vo > max_virt_offset()) return false;
				i = vo >> Descriptor::VIRT_SIZE_LOG2;
				return true;
			}

		public:

			/**
			 * Placement new
			 */
			void * operator new (size_t, void * p) { return p; }

			/**
			 * Constructor
			 */
			Section_table()
			{
				/* check for appropriate positioning of the table */
				assert(aligned((addr_t)this, ALIGNM_LOG2));

				/* start with an empty table */
				memset(&_entries, 0, sizeof(_entries));
			}

			/**
			 * Maximum virtual offset that can be translated by this table
			 */
			static addr_t max_virt_offset()
			{
				return (MAX_INDEX << Descriptor::VIRT_SIZE_LOG2)
				       + (Descriptor::VIRT_SIZE - 1);
			}

			/**
			 * Insert one atomic translation into this table
			 *
			 * \param ST           platform specific section-table type
			 * \param st           platform specific section table
			 * \param vo           offset of the virtual region represented
			 *                     by the translation within the virtual
			 *                     region represented by this table
			 * \param pa           base of the physical backing store
			 * \param size_log2    size log2 of the translated region
			 * \param flags        mapping flags
			 * \param extra_space  If > 0, it must point to a portion of
			 *                     size-aligned memory space wich may be used
			 *                     furthermore by the table for the incurring
			 *                     administrative costs of the  translation.
			 *                     To determine the amount of additionally
			 *                     needed memory one can instrument this
			 *                     method with 'extra_space' set to 0.
			 *                     The so donated memory may be regained by
			 *                     using the method 'regain_memory'.
			 *
			 * \retval  0  translation successfully inserted
			 * \retval >0  Translation not inserted, the return value
			 *             is the size log2 of additional size-aligned
			 *             space that is needed to do the translation.
			 *             This occurs solely when 'extra_space' is 0.
			 *
			 * This method overrides an existing translation in case that it
			 * spans the the same virtual range and is not a link to another
			 * table level.
			 */
			template <typename ST>
			void insert_translation(addr_t const vo, addr_t const pa,
			                        size_t const size_log2,
			                        Page_flags const & flags,
			                        ST * const st,
			                        Physical_slab_allocator * slab)
			{
				typedef typename ST::Section Section;
				typedef typename ST::Page_table_descriptor Page_table_descriptor;

				/* sanity checks */
				unsigned i;
				assert(_index_by_vo (i, vo));
				assert(size_log2 <= Descriptor::VIRT_SIZE_LOG2);

				/* select descriptor type by translation size */
				if (size_log2 < Descriptor::VIRT_SIZE_LOG2) {
					Page_table * pt = 0;
					switch (Descriptor::type(_entries[i])) {

					case Descriptor::FAULT:
						{
							if (!slab) throw Allocator::Out_of_memory();

							/* create and link page table */
							pt = new (slab) Page_table();
							Page_table * pt_phys = (Page_table*) slab->phys_addr(pt);
							pt_phys = pt_phys ? pt_phys : pt; /* hack for core */
							_entries[i] = Page_table_descriptor::create(pt_phys, st);
							break;
						}

					case Descriptor::PAGE_TABLE:
						{
							void * pt_phys = (void*)
								Page_table_descriptor::Pa_31_10::masked(_entries[i]);
							pt = (Page_table *) slab->virt_addr(pt_phys);
							pt = pt ? pt : (Page_table *)pt_phys ; /* hack for core */
							break;
						}

					default:
						PERR("insert vo=%lx pa=%lx size=%zx into section impossible",
						     vo, pa, 1 << size_log2);
						assert(false);
					};

					/* insert translation */
					pt->insert_translation(vo - Section::Pa_31_20::masked(vo),
					                       pa, size_log2, flags);
				} else {
					if (Descriptor::valid(_entries[i]) &&
						_entries[i] != Section::create(flags, pa, st))
						throw Double_insertion();

					_entries[i] = Section::create(flags, pa, st);
				}
			}

			/**
			 * Remove translations that overlap with a given virtual region
			 *
			 * \param vo    region offset within the tables virtual region
			 * \param size  region size
			 */
			void remove_region(addr_t vo, size_t const size,
			                   Physical_slab_allocator * slab)
			{
				assert(vo < (vo + size));

				addr_t const ve = vo + size;
				for (unsigned i = 0; (vo < ve) && _index_by_vo(i, vo);
				     vo = (vo + Descriptor::VIRT_SIZE)
				          & Descriptor::VIRT_BASE_MASK) {

					switch (Descriptor::type(_entries[i])) {

					case Descriptor::PAGE_TABLE:
						{
							typedef Page_table_descriptor Ptd;
							typedef Page_table            Pt;

							Pt * pt_phys = (Pt *) Ptd::Pa_31_10::masked(_entries[i]);
							Pt * pt      = (Pt *) slab->virt_addr(pt_phys);
							pt = pt ? pt : pt_phys; // TODO hack for core

							addr_t const pt_vo = vo - Section::Pa_31_20::masked(vo);
							pt->remove_region(pt_vo, ve - vo);

							if (pt->empty()) {
								Descriptor::invalidate(_entries[i]);
								destroy(slab, pt);
							}
							break;
						}

					case Descriptor::SECTION:
						{
							Descriptor::invalidate(_entries[i]);
						}

					default: ;
					}
				}
			}
	} __attribute__((aligned(1<<Section_table::ALIGNM_LOG2)));
}

#endif /* _TLB__ARM_H_ */

