/*
 * Copyright (c) 2013, 2019, Red Hat, Inc. All rights reserved.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHBARRIERSETCLONE_INLINE_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHBARRIERSETCLONE_INLINE_HPP

#include "gc_implementation/shenandoah/shenandoahBarrierSet.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectionSet.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahEvacOOMHandler.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "memory/iterator.hpp"
#include "oops/oop.inline.hpp"

template <bool EVAC, bool ENQUEUE>
class ShenandoahUpdateRefsForOopClosure: public ExtendedOopClosure {
private:
  ShenandoahHeap* const _heap;
  ShenandoahBarrierSet* const _bs;
  const ShenandoahCollectionSet* const _cset;
  Thread* const _thread;

  template <class T>
  inline void do_oop_work(T* p) {
    T o = oopDesc::load_heap_oop(p);
    if (!oopDesc::is_null(o)) {
      oop obj = oopDesc::decode_heap_oop_not_null(o);
      if (_cset->is_in(obj)) {
        oop fwd = _bs->resolve_forwarded_not_null(obj);
        if (EVAC && obj == fwd) {
          fwd = _heap->evacuate_object(obj, _thread);
        }
        if (ENQUEUE) {
          _bs->enqueue(fwd);
        }
        assert(obj != fwd || _heap->cancelled_gc(), "must be forwarded");
        ShenandoahHeap::cas_oop(fwd, p, o);
      }

    }
  }
public:
  ShenandoahUpdateRefsForOopClosure() :
          _heap(ShenandoahHeap::heap()),
          _bs(ShenandoahBarrierSet::barrier_set()),
          _cset(_heap->collection_set()),
          _thread(Thread::current()) {
   }
 
  virtual void do_oop(oop* p)       { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p); }
};

void ShenandoahBarrierSet::clone_barrier(oop obj) {
  assert(ShenandoahCloneBarrier, "only get here with clone barriers enabled");
  if (!_heap->has_forwarded_objects()) return;

  // This is called for cloning an object (see jvm.cpp) after the clone
  // has been made. We are not interested in any 'previous value' because
  // it would be NULL in any case. But we *are* interested in any oop*
  // that potentially need to be updated.
 
  shenandoah_assert_correct(NULL, obj);
  if (skip_bulk_update(cast_from_oop<HeapWord*>(obj))) return;
  if (_heap->is_evacuation_in_progress()) {
    ShenandoahEvacOOMScope evac_scope;
    ShenandoahUpdateRefsForOopClosure</* evac = */ true, /* enqueue */ false> cl;
    obj->oop_iterate(&cl);
  } else if (_heap->is_concurrent_traversal_in_progress()) {
    ShenandoahEvacOOMScope evac_scope;
    ShenandoahUpdateRefsForOopClosure</* evac = */ true, /* enqueue */ true> cl;
    obj->oop_iterate(&cl);
  } else {
    ShenandoahUpdateRefsForOopClosure</* evac = */ false, /* enqueue */ false> cl;
    obj->oop_iterate(&cl);
  }
}

#endif // SHARE_GC_SHENANDOAH_SHENANDOAHBARRIERSETCLONE_INLINE_HPP
