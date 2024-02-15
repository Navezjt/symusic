//
// Created by lyk on 23-12-13.
//
#pragma once

#ifndef LIBSYMUSIC_BATCH_OPS_H
#define LIBSYMUSIC_BATCH_OPS_H

#include "symusic/event.h"
#include "symusic/score.h"
#include "pdqsort.h"

#include <stdexcept>

namespace symusic::ops {

template<typename T>
void sort_by_time(vec<T> & data, const bool reverse = false) {
    if (reverse) {
        pdqsort_branchless(data.begin(), data.end(), [](const T & a, const T & b) {return (a.time) > (b.time);});
    } else {
        pdqsort_branchless(data.begin(), data.end(), [](const T & a, const T & b) {return (a.time) < (b.time);});
    }
}

template<TType T>
void sort_notes(vec<Note<T>> & notes, const bool reverse = false) {
    #define KEY(NOTE) std::tie((NOTE).time, (NOTE).duration, (NOTE).pitch, (NOTE).velocity)
    if (reverse) {
        pdqsort_branchless(notes.begin(), notes.end(), [](const Note<T> & a, const Note<T> & b) {return KEY(a) > KEY(b);});
    } else {
        pdqsort_branchless(notes.begin(), notes.end(), [](const Note<T> & a, const Note<T> & b) {return KEY(a) < KEY(b);});
    }
    #undef KEY
}

template<TType T>
void sort_pedals(vec<Pedal<T>> & pedals, const bool reverse = false) {
    #define KEY(PEDAL) std::tie((PEDAL).time, (PEDAL).duration)
    if (reverse) {
        pdqsort_branchless(pedals.begin(), pedals.end(), [](const Pedal<T> & a, const Pedal<T> & b) {return KEY(a) > KEY(b);});
    } else {
        pdqsort_branchless(pedals.begin(), pedals.end(), [](const Pedal<T> & a, const Pedal<T> & b) {return KEY(a) < KEY(b);});
    }
    #undef KEY
}

template<TType T>
void sort_tracks(vec<Track<T>> & tracks, const bool reverse = false) {
    #define KEY(TRACK) std::make_tuple((TRACK).is_drum, (TRACK).program, (TRACK).name, (TRACK).note_num())
    if (reverse) {
        pdqsort_branchless(tracks.begin(), tracks.end(), [](const Track<T> & a, const Track<T> & b) {return KEY(a) > KEY(b);});
    } else {
        pdqsort_branchless(tracks.begin(), tracks.end(), [](const Track<T> & a, const Track<T> & b) {return KEY(a) < KEY(b);});
    }
    #undef KEY
}


template<class Iter, class Compare>
void sort(Iter begin, Iter end, Compare cmp) {
    pdqsort_branchless(begin, end, cmp);
}

template<typename T, class Compare>
void sort(vec<T> & data, Compare cmp){
    pdqsort_branchless(data.begin(), data.end(), cmp);
}



template<class T, class FILTER>
vec<T> filter(const vec<T>& data, FILTER t_fiter) {
    if (data.empty()) return {};
    vec<T> new_data;
    new_data.reserve(data.size());
    std::copy_if(
        data.begin(), data.end(),
        std::back_inserter(new_data), t_fiter
    );
    new_data.shrink_to_fit();
    return new_data;
}

template<TimeEvent T> // used for general events, with time
vec<T> clip(const vec<T>& events, typename T::unit start, typename T::unit end) {
    auto func = [start, end](const T &event) {
        return ((event.time) >= start) && ((event.time) < end);
    };  return filter(events, func);
}

template<TimeEvent T> // used for events with duration (e.g. Note and Pedal)
vec<T> clip(const vec<T>& events, typename T::unit start, typename T::unit end, const bool clip_end) {
    if (clip_end) {
        auto func = [start, end](const T &event) {
            return (event.time >= start) && (event.time + event.duration <= end);
        };  return filter(events, func);
    }   return clip(events, start, end);
}

// make sure T has time and duration fields using requires
template<TimeEvent T>
requires requires(T t) {{ t.duration } -> std::convertible_to<typename T::unit>;}
vec<T>& clamp_dur_inplace(vec<T>& events, typename T::unit min_dur, typename T::unit max_dur) {
    for(auto &event : events) {
        event.duration = std::clamp(event.duration, min_dur, max_dur);
    }   return events;
}

template<TimeEvent T>
requires requires(T t) {{ t.duration } -> std::convertible_to<typename T::unit>;}
vec<T> clamp_dur(const vec<T>& events, typename T::unit min_dur, typename T::unit max_dur) {
    vec<T> new_events(events);
    return clamp_dur_inplace(new_events, min_dur, max_dur);
}

template<TimeEvent T>
vec<T> adjust_time_sorted(
    const vec<T>& events, const vec<typename T::unit>& original_times, const vec<typename T::unit>& new_times) {
    // check if the events have duration
    constexpr bool has_dur = HashDuration<T>;
    auto get_end = [has_dur](const T &event) {
        if constexpr (has_dur) {
            return event.time + event.duration;
        }   return event.time;
    };
    // assume that all the events, original_times and new_times are sorted
    if(original_times.size() != new_times.size()) {
        throw std::invalid_argument("symusic::ops::adjust_time: original_times and new_times should have the same size");
    }
    // assume that original_times and new_times have at least 2 elements
    if(original_times.size() < 2) {
        throw std::invalid_argument("symusic::ops::adjust_time: original_times and new_times should have at least 2 elements");
    }
    // return empty vector if events is empty
    if(events.empty()) return {};
    // find the first event that is after(>=) the first original time
    auto begin_event = events[0].time >= original_times[0] ? events.begin() :
        std::lower_bound(events.begin(), events.end(), original_times[0],
        [](const T &event, const typename T::unit &time) {return event.time < time;});
    // find the first event that is after(>) the last original time
    auto end_event = get_end(events.back()) <= original_times.back() ? events.end() :
        std::upper_bound(begin_event, events.end(), original_times.back(),
        [get_end](const typename T::unit &time, const T &event) {return time < get_end(event);});
    // check if end_event > begin_event
    if (end_event <= begin_event) return {};
    // create new events
    vec<T> new_events{begin_event, end_event};
    // iterate through the new events and adjust the time
    size_t pivot = 1;
    auto get_factor = [&original_times, &new_times](const size_t x) {
        return static_cast<f64>(new_times[x] - new_times[x - 1]) /
            static_cast<f64>(original_times[x] - original_times[x - 1]);
    };
    f64 cur_factor = get_factor(pivot);

    if constexpr (has_dur) {
        pdqsort_detail::insertion_sort(new_events.begin(), new_events.end(),
            [get_end](const T & a, const T & b) {return get_end(a) < get_end(b);});

        for(auto &event : new_events) {
            while(get_end(event) > original_times[pivot]) {
                pivot++;
                cur_factor = get_factor(pivot);
            }
            // just store the end time in duration first
            // then adjust the duration when processing event.time
            event.duration = static_cast<typename T::unit>(
                static_cast<f64>(new_times[pivot - 1]) +
                cur_factor * static_cast<f64>(get_end(event) - original_times[pivot - 1])
            );
        }
        pdqsort_detail::insertion_sort(new_events.begin(), new_events.end(),
            [](const T & a, const T & b) {return std::tie(a.time, a.duration) < std::tie(b.time, b.duration);});
        pivot = 1;
        cur_factor = get_factor(pivot);
    }

    for(auto &event : new_events) {
        while(event.time > original_times[pivot]) {
            pivot++;
            cur_factor = get_factor(pivot);
        }
        event.time = static_cast<typename T::unit>(
            static_cast<f64>(new_times[pivot - 1]) +
            cur_factor * static_cast<f64>(event.time - original_times[pivot - 1])
        );
        if constexpr (has_dur) {
            event.duration -= event.time;
        }
    }

    return new_events;
}

template<TimeEvent T>
vec<T> adjust_time(
    const vec<T>& events, const vec<typename T::unit>& original_times,
    const vec<typename T::unit>& new_times, const bool sorted = false) {
    if (sorted) { return adjust_time_sorted(events, original_times, new_times); }

    vec<T> _events(events);
    sort_by_time(_events);
    vec<typename T::unit> _original_times(original_times);
    sort(_original_times, std::less<typename T::unit>());
    vec<typename T::unit> _new_times(new_times);
    sort(_new_times, std::less<typename T::unit>());

    return adjust_time_sorted(_events, _original_times, _new_times);
}

template<TType T>
Track<T> adjust_time(
    const Track<T> & track, const vec<typename T::unit>& original_times,
    const vec<typename T::unit>& new_times, const bool sorted = false) {
    Track<T> new_track{track.name, track.program, track.is_drum};
    new_track.notes = std::move(adjust_time(track.notes, original_times, new_times, sorted));
    new_track.controls = std::move(adjust_time(track.controls, original_times, new_times, sorted));
    new_track.pitch_bends = std::move(adjust_time(track.pitch_bends, original_times, new_times, sorted));
    new_track.pedals = std::move(adjust_time(track.pedals, original_times, new_times, sorted));
    return new_track;
}

template<TType T>
Score<T> adjust_time(
    const Score<T> & score, const vec<typename T::unit>& original_times,
    const vec<typename T::unit>& new_times, const bool sorted = false) {
    Score<T> new_score{score.ticks_per_quarter};
    new_score.tracks.reserve(score.tracks.size());
    for(const auto & track : score.tracks) {
        new_score.tracks.push_back(adjust_time(track, original_times, new_times, sorted));
    }
    new_score.time_signatures = adjust_time(score.time_signatures, original_times, new_times, sorted);
    new_score.key_signatures = adjust_time(score.key_signatures, original_times, new_times, sorted);
    new_score.tempos = adjust_time(score.tempos, original_times, new_times, sorted);
    new_score.lyrics = adjust_time(score.lyrics, original_times, new_times, sorted);
    new_score.markers = adjust_time(score.markers, original_times, new_times, sorted);
    return new_score;
}

template<TimeEvent T>
typename T::unit start(const vec<T>& events) {
    if (events.empty()) return 0;
    typename T::unit ans = std::numeric_limits<typename T::unit>::max();
    for (const auto & event: events) {
        ans = std::min(ans, event.time);
    }
    return ans;
}

template<TimeEvent T>
typename T::unit end(const vec<T>& events) {
    if (events.empty()) return 0;
    typename T::unit ans = std::numeric_limits<typename T::unit>::min();
    auto get_end = [](const T &event) {
        if constexpr (HashDuration<T>) {
            return event.time + event.duration;
        }   return event.time;
    };
    for (const auto & event: events) {
        ans = std::max(ans, get_end(event));
    }
    return ans;
}

} // namespace symusic::ops

#endif //LIBSYMUSIC_BATCH_OPS_H
