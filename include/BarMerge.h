#pragma once
// ============================================================================
// BarMerge.h
// How fetched data is written into AmiBroker's quotation array. Kept apart from
// the plugin entry points so it can be unit tested.
// ============================================================================

#include "DSEParsers.h"
#include "Plugin.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace barmerge {

// Adds today's bar from the live page when the archive does not have it yet.
// The live page has no open column, so yesterday's close (clamped to the day's
// range) stands in until the archive replaces this bar. Pass sessionOpen = false
// after the close, when the page's close price is final.
inline void AppendLiveBar(std::vector<DSEBar> &bars, const DSEQuote &q,
                          const DhakaTime &date, bool sessionOpen) {
  if (q.ltp <= 0 && q.trade == 0)
    return; // not traded today
  if (!bars.empty()) {
    const DSEBar &last = bars.back();
    const long lastKey = last.year * 10000L + last.month * 100L + last.day;
    const long dateKey = date.year * 10000L + date.month * 100L + date.day;
    if (lastKey >= dateKey)
      return;
  }
  const float price = (!sessionOpen && q.close > 0) ? q.close : q.ltp;
  if (price <= 0)
    return;
  DSEBar b;
  b.year = date.year;
  b.month = date.month;
  b.day = date.day;
  b.close = price;
  b.high = std::max(q.high, price);
  b.low = q.low > 0 ? std::min(q.low, price) : price;
  b.open = q.ycp > 0 ? std::min(std::max(q.ycp, b.low), b.high) : price;
  b.volume = q.volume;
  b.value = q.value;
  b.trade = q.trade;
  bars.push_back(b);
}

// Replaces the fetched date range in `quotes`, keeping stored bars older than
// it: DSE serves only two years, while AmiBroker's local database may hold more.
// When the array is too small the oldest bars are dropped. `bars` must be sorted
// oldest first. Returns the new bar count.
inline int MergeEodBars(Quotation *quotes, int lastValid, int size,
                        const std::vector<DSEBar> &bars) {
  const int existing = std::min(lastValid + 1, size);
  if (bars.empty())
    return std::max(existing, 0);

  const int fetched = static_cast<int>(bars.size());
  const int used = std::min(fetched, size);
  const int firstUsed = fetched - used;

  int keep = 0;
  if (existing > 0 && used < size) {
    const DSEBar &first = bars[firstUsed];
    const DATE_TIME_INT start =
        MakeAmiDate(first.year, first.month, first.day).Date;
    while (keep < existing && quotes[keep].DateTime.Date < start)
      ++keep;
    const int drop = std::max(keep + used - size, 0);
    if (drop > 0) {
      std::memmove(quotes, quotes + drop, sizeof(Quotation) * (keep - drop));
      keep -= drop;
    }
  }

  for (int i = 0; i < used; ++i) {
    const DSEBar &b = bars[firstUsed + i];
    Quotation &q = quotes[keep + i];
    q.DateTime = MakeAmiDate(b.year, b.month, b.day);
    q.Open = b.open;
    q.High = b.high;
    q.Low = b.low;
    q.Price = b.close;
    q.Volume = b.volume;
    q.OpenInterest = 0.f;
    q.AuxData1 = b.value;
    q.AuxData2 = static_cast<float>(b.trade);
  }
  return keep + used;
}

// Folds a live snapshot into the bar containing `now`: extends the current bar
// or starts a new one, shifting the array when it is full. DSE reports only
// cumulative daily volume, so a bar's volume is that total minus the volume of
// the day's earlier bars; if the array is so small that today's own bars have
// been shifted out, the volume they held lands in the current bar. Returns the
// new bar count.
inline int MergeTick(Quotation *quotes, int lastValid, int size,
                     const DSEQuote &q, const DhakaTime &now, int periodicity) {
  const int existing = std::min(lastValid + 1, size);
  if (q.ltp <= 0 || size <= 0)
    return std::max(existing, 0);
  if (periodicity <= 0)
    periodicity = PERIODICITY_1MIN;

  int secs = now.hour * 3600 + now.minute * 60 + now.second;
  secs -= secs % periodicity;
  const DATE_TIME_INT stamp = MakeAmiDate(now.year, now.month, now.day,
                                          secs / 3600, secs % 3600 / 60,
                                          secs % 60)
                                  .Date;

  int idx = existing;
  if (existing > 0) {
    const DATE_TIME_INT last = quotes[existing - 1].DateTime.Date;
    if (last > stamp)
      return existing; // clock moved backwards; keep what is stored
    if (last == stamp)
      idx = existing - 1;
  }

  if (idx == existing) {
    if (existing == size) {
      std::memmove(quotes, quotes + 1, sizeof(Quotation) * (size - 1));
      idx = size - 1;
    }
    Quotation &fresh = quotes[idx];
    fresh.DateTime.Date = stamp;
    fresh.Open = fresh.High = fresh.Low = q.ltp;
    fresh.OpenInterest = 0.f;
  }

  float earlierVolume = 0.f;
  for (int i = idx - 1; i >= 0; --i) {
    const PackedDate &d = quotes[i].DateTime.PackDate;
    if (static_cast<int>(d.Year) != now.year ||
        static_cast<int>(d.Month) != now.month ||
        static_cast<int>(d.Day) != now.day)
      break;
    earlierVolume += quotes[i].Volume;
  }

  Quotation &bar = quotes[idx];
  bar.High = std::max(bar.High, q.ltp);
  bar.Low = std::min(bar.Low, q.ltp);
  bar.Price = q.ltp;
  bar.Volume = std::max(q.volume - earlierVolume, 0.f);
  bar.AuxData1 = q.ycp;
  bar.AuxData2 = q.change;
  return idx + 1;
}

} // namespace barmerge
