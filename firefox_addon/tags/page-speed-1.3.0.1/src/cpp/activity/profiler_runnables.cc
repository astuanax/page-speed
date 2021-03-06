/**
 * Copyright 2008-2009 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Author: Bryan McQuade

#include "profiler_runnables.h"

#include <stdio.h>

#include "call_graph_profile_snapshot.h"
#include "call_graph_timeline_event.h"
#include "call_graph_timeline_event_set.h"
#include "call_graph_util.h"
#include "check.h"
#include "profiler_event.h"

#include "nsComponentManagerUtils.h"
#include "nsIEventTarget.h"
#include "nsIThread.h"

// nsIMutableArray was moved from nsIArray.h to nsIMutableArray.h in
// v1.9.
#ifdef MOZILLA_1_8_BRANCH
#include "nsIArray.h"
#else
#include "nsIMutableArray.h"
#endif

NS_IMPL_THREADSAFE_ISUPPORTS1(
    activity::GetTimelineEventsRunnable, nsIRunnable)
NS_IMPL_THREADSAFE_ISUPPORTS1(
    activity::InvokeTimelineEventsCallbackRunnable, nsIRunnable)

namespace {
const char* kArrayContractStr = "@mozilla.org/array;1";
}  // namespace

namespace activity {

GetTimelineEventsRunnable::GetTimelineEventsRunnable(
    nsIThread *main_thread,
    IActivityProfilerTimelineEventCallback *callback,
    CallGraphProfileSnapshot *snapshot,
    PRInt64 start_time_usec,
    PRInt64 end_time_usec,
    PRInt64 resolution_usec)
    : main_thread_(main_thread),
      callback_(callback),
      snapshot_(snapshot),
      start_time_usec_(start_time_usec),
      end_time_usec_(end_time_usec),
      resolution_usec_(resolution_usec) {
}

GetTimelineEventsRunnable::~GetTimelineEventsRunnable() {}

NS_IMETHODIMP GetTimelineEventsRunnable::Run() {
  // Initialize the snapshot now that we're running in the background
  // thread.
  snapshot_->Init(start_time_usec_, end_time_usec_);

  // We would like to use an nsIMutableArray to store the events, but
  // there is a bug in Firefox 3.5 (see
  // https://bugzilla.mozilla.org/show_bug.cgi?id=471296) that can
  // cause memory corruption when an nsIArray is instantiated on a
  // background thread. So, instead, we store our events in a
  // vector<>, proxy the vector over to the main thread, and then copy
  // the events from the vector<> into an nsIMutableArray.
  scoped_ptr<EventVector> events(new EventVector());
  nsresult rv = PopulateEventArray(events.get());
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = main_thread_->Dispatch(
      new InvokeTimelineEventsCallbackRunnable(callback_, events.release()),
      nsIEventTarget::DISPATCH_NORMAL);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

nsresult GetTimelineEventsRunnable::PopulateEventArray(EventVector *events) {
  if (end_time_usec_ <= start_time_usec_) {
    return NS_OK;
  }

  CallGraphTimelineEventSet event_set(resolution_usec_);

  util::PopulateExecutionTimes(
      *snapshot_.get(),
      &event_set,
      start_time_usec_,
      end_time_usec_);
  util::PopulateFunctionInitCounts(
      *snapshot_.get(),
      &event_set,
      start_time_usec_,
      end_time_usec_);

  // Copy the events into XPCOM objects.
  const CallGraphTimelineEventSet::EventMap *event_map =
      event_set.event_map();
  for (CallGraphTimelineEventSet::EventMap::const_iterator it =
           event_map->begin(), end = event_map->end();
       it != end;
       ++it) {
    const CallGraphTimelineEvent *event = it->second;
    PRInt16 event_type;
    switch (event->type) {
      case CallGraphTimelineEvent::JS_PARSE:
        event_type = IActivityProfilerEvent::JS_PARSE;
        break;
      case CallGraphTimelineEvent::JS_EXECUTE:
        event_type = IActivityProfilerEvent::JS_EXECUTE;
        break;
      default:
        GCHECK_NE(event->type, event->type);
        event_type = -1;
        break;
    }
    events->push_back(new ProfilerEvent(
        event->start_time_usec,
        event_set.event_duration_usec(),
        event->intensity,
        event_type,
        event->identifier));
  }
  return NS_OK;
}

InvokeTimelineEventsCallbackRunnable::InvokeTimelineEventsCallbackRunnable(
    IActivityProfilerTimelineEventCallback *callback,
    EventVector *events)
    : callback_(callback),
      events_(events) {
}

InvokeTimelineEventsCallbackRunnable::
~InvokeTimelineEventsCallbackRunnable() {}

NS_IMETHODIMP InvokeTimelineEventsCallbackRunnable::Run() {
  nsresult rv;
  nsCOMPtr<nsIMutableArray> ns_array =
      do_CreateInstance(kArrayContractStr, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Copy the events from the vector<> to the nsIMutableArray.
  for (EventVector::const_iterator it = events_->begin(), end = events_->end();
       it != end;
       ++it) {
    IActivityProfilerEvent *event = *it;
    rv = ns_array->AppendElement(event, PR_FALSE);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Release the references held by the vector.
  events_->clear();

  // Dispatch the events, wrapped in an nsIArray, to the client.
  rv = callback_->ProcessTimelineEvents(ns_array);

  // Release the references held by the nsIMutableArray. This should
  // free the event objects.
  ns_array->Clear();

  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

}  // namespace activity
