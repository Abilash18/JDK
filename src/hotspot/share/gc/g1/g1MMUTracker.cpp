/*
 * Copyright (c) 2001, 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
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

#include "precompiled.hpp"
#include "gc/g1/g1MMUTracker.hpp"
#include "gc/g1/g1Trace.hpp"
#include "logging/log.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/ostream.hpp"

// can't rely on comparing doubles with tolerating a small margin for error
#define SMALL_MARGIN 0.0000001
#define is_double_leq_0(_value) ( (_value) < SMALL_MARGIN )
#define is_double_leq(_val1, _val2) is_double_leq_0((_val1) - (_val2))
#define is_double_geq(_val1, _val2) is_double_leq_0((_val2) - (_val1))

/***** ALL TIMES ARE IN SECS!!!!!!! *****/

G1MMUTracker::G1MMUTracker(double time_slice, double max_gc_time) :
  _time_slice(time_slice),
  _max_gc_time(max_gc_time),
  _head_index(0),
  _tail_index(trim_index(_head_index+1)),
  _no_entries(0) { }

void G1MMUTracker::remove_expired_entries(double current_time) {
  double limit = current_time - _time_slice;
  while (_no_entries > 0) {
    if (is_double_geq(limit, _array[_tail_index].end_time())) {
      _tail_index = trim_index(_tail_index + 1);
      --_no_entries;
    } else
      return;
  }
  guarantee(_no_entries == 0, "should have no entries in the array");
}

double G1MMUTracker::calculate_gc_time(double current_timestamp) {
  double gc_time = 0.0;
  double limit = current_timestamp - _time_slice;
  for (int i = 0; i < _no_entries; ++i) {
    int index = trim_index(_tail_index + i);
    G1MMUTrackerElem *elem = &_array[index];
    if (elem->end_time() > limit) {
      if (elem->start_time() > limit)
        gc_time += elem->duration();
      else
        gc_time += elem->end_time() - limit;
    }
  }
  return gc_time;
}

//                                          current_timestamp
//                   GC events             /  pause_time
//                 /     |     \          | /  /
// -------------[----]-[---]--[--]--------|[--]-----> Time
//              |         |                   |
//              |         |                   |
//              |<- limit |                   |
//              |         |<- deficit         |
//              |         ^                   |
//              |                             |
//              |<------- _time_slice   ----->|
//
// Focusing on GC events that are inside [limit, current_timestamp], we iterate
// over them from the newest to the oldest (right-to-left in the diagram) and
// try to locate the timestamp annotated with ^, so that the accumulated GC
// time inside [deficit, current_timestamp], is equal to gc_budget. Next,
// return `deficit - limit`.
//
// When there are no enough GC events, i.e. gc_budget has a surplus, return 0.
double G1MMUTracker::time_till_next_gc_sec(double current_timestamp, double pause_time) {
  assert(pause_time > 0.0, "precondition");

  // Clamp it by max
  pause_time = MIN2(pause_time, max_gc_time());

  double gc_budget = max_gc_time() - pause_time;

  double limit = current_timestamp + pause_time - _time_slice;
  // Iterate from newest to oldest
  for (int i = 0; i < _no_entries; ++i) {
    int index = trim_index(_head_index + i);
    G1MMUTrackerElem *elem = &_array[index];
    // Outside the window
    if (elem->end_time() <= limit) {
      break;
    }

    double duration = (elem->end_time() - MAX2(elem->start_time(), limit));
    // Would exceed the budget; strictly greater than
    if (duration > gc_budget) {
      // The timestamp where a budget deficit occurs.
      double deficit_timestamp = elem->end_time() - gc_budget;
      assert(deficit_timestamp >= limit, "inv");
      return deficit_timestamp - limit;
    }

    gc_budget -= duration;
  }

  // No enough gc time inside the window; a budget surplus
  return 0;
}

void G1MMUTracker::add_pause(double start, double end) {
  remove_expired_entries(end);
  if (_no_entries == QueueLength) {
    // OK, we've filled up the queue. There are a few ways
    // of dealing with this "gracefully"
    //   increase the array size (:-)
    //   remove the oldest entry (this might allow more GC time for
    //     the time slice than what's allowed) - this is what we
    //     currently do
    //   consolidate the two entries with the minimum gap between them
    //     (this might allow less GC time than what's allowed)

    // In the case where ScavengeALot is true, such overflow is not
    // uncommon; in such cases, we can, without much loss of precision
    // or performance (we are GC'ing most of the time anyway!),
    // simply overwrite the oldest entry in the tracker.

    _head_index = trim_index(_head_index + 1);
    assert(_head_index == _tail_index, "Because we have a full circular buffer");
    _tail_index = trim_index(_tail_index + 1);
  } else {
    _head_index = trim_index(_head_index + 1);
    ++_no_entries;
  }
  _array[_head_index] = G1MMUTrackerElem(start, end);

  // Current entry needs to be added before calculating the value
  double slice_time = calculate_gc_time(end);
  G1MMUTracer::report_mmu(_time_slice, slice_time, _max_gc_time);

  if (slice_time < _max_gc_time) {
    log_debug(gc, mmu)("MMU: %.1lfms (%.1lfms/%.1lfms)",
                       slice_time * 1000.0, _max_gc_time * 1000.0, _time_slice * 1000);
  } else {
    log_info(gc, mmu)("MMU target violated: %.1lfms (%.1lfms/%.1lfms)",
                      slice_time * 1000.0, _max_gc_time * 1000.0, _time_slice * 1000);
  }
}

double G1MMUTracker::when_sec(double current_timestamp, double pause_time) {
  double old_v = old_when_sec(current_timestamp, pause_time);
  double new_v = time_till_next_gc_sec(current_timestamp, pause_time);

  assert(is_double_leq(old_v, new_v), "%f vs %f", old_v, new_v);
  assert(is_double_geq(old_v, new_v), "%f vs %f", old_v, new_v);
  return old_v;
}

double G1MMUTracker::old_when_sec(double current_timestamp, double pause_time) {
  // If the pause is over the maximum, just assume that it's the maximum.
  double adjusted_pause_time =
    (pause_time > max_gc_time()) ? max_gc_time() : pause_time;

  // Earliest end time of a hypothetical pause starting now, taking pause_time.
  double earliest_end_time = current_timestamp + adjusted_pause_time;
  double gc_time_in_recent_time_slice = calculate_gc_time(earliest_end_time) + adjusted_pause_time;

  // How much gc time is needed to pass within the MMU window to fit the given pause into the MMU.
  double gc_time_to_pass = gc_time_in_recent_time_slice - max_gc_time();

  // If that time to pass is zero or negative we could start the pause immediately.
  if (is_double_leq_0(gc_time_to_pass)) {
    return 0.0;
  }

  // Trivially, if the pause is of maximum pause time, the required delay is what the MMU dictates by
  // the time slice and maximum gc pause, counted from the end of the last pause.
  if (adjusted_pause_time == max_gc_time()) {
    G1MMUTrackerElem *elem = &_array[_head_index];
    return (elem->end_time() + (_time_slice - max_gc_time())) - current_timestamp;
  }

  // Now go through the recent pause time events,
  double limit = earliest_end_time - _time_slice;
  int index = _tail_index;
  while ( 1 ) {
    G1MMUTrackerElem *elem = &_array[index];
    if (elem->end_time() > limit) {
      if (elem->start_time() > limit) {
        gc_time_to_pass -= elem->duration();
      } else {
        gc_time_to_pass -= elem->end_time() - limit;
      }
      if (is_double_leq_0(gc_time_to_pass)) {
        return elem->end_time() + (_time_slice + gc_time_to_pass) - earliest_end_time;
      }
    }
    index = trim_index(index+1);
    guarantee(index != trim_index(_head_index + 1), "should not go past head");
  }
}
