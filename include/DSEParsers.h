#pragma once
// ============================================================================
// DSEParsers.h
// Platform-neutral parsing of the legacy DSE site (old.dsebd.org), plus Dhaka
// time helpers. Nothing here touches Windows or the network, so it is unit
// tested on any platform against saved pages in tests/fixtures/.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

// One row of the latest-share-price page. The page has no open column.
struct DSEQuote {
  std::string symbol;
  float ltp = 0, high = 0, low = 0, close = 0, ycp = 0, change = 0;
  float volume = 0, value = 0;
  int trade = 0;
};

// One normalised end-of-day bar: every price is > 0 and low <= open, close <=
// high. A day with no trades is a flat bar at the close with zero volume.
struct DSEBar {
  int year = 0, month = 0, day = 0;
  float open = 0, high = 0, low = 0, close = 0, volume = 0, value = 0;
  int trade = 0;
};

// The two sides of the book have independent lengths; either may be empty.
struct DSEDepth {
  static constexpr int kMaxLevels = 10;
  float bidPrice[kMaxLevels] = {}, askPrice[kMaxLevels] = {};
  int bidVolume[kMaxLevels] = {}, askVolume[kMaxLevels] = {};
  int bidLevels = 0, askLevels = 0;
};

struct DSENewsItem {
  std::string code, title, date;
};

// Wall-clock time in Dhaka. weekday: 0 = Sunday ... 6 = Saturday.
struct DhakaTime {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  int weekday = 0;
};

namespace dse {

// Each parser returns false when the page does not have the expected layout, so
// callers can tell "the site changed or failed" from "no data".
bool ParseLiveTable(const std::string &html, std::vector<DSEQuote> &out);
// Rows are returned oldest first. A page with no rows for the range is true.
bool ParseDayEndTable(const std::string &html, std::vector<DSEBar> &out);
bool ParseDepth(const std::string &html, DSEDepth &out);
bool ParseNews(const std::string &html, std::vector<DSENewsItem> &out);
// Extracts the status text from the home page header, e.g. "Open" or "Closed".
bool ParseMarketStatus(const std::string &html, std::string &status);
bool IsMarketOpenStatus(const std::string &status);

// Number in a scraped cell: handles thousands separators and returns 0 for
// placeholders such as "--" or "N/A". Independent of the process locale.
float ParseNumber(const std::string &raw);
// Percent-encodes a query value; DSE codes include names like AMCL(PRAN).
std::string UrlEncode(const std::string &value);

// Bangladesh has no DST, so a fixed UTC+6 offset is exact.
DhakaTime DhakaFromEpoch(std::int64_t unixSeconds);
DhakaTime DhakaNow();
std::string IsoDate(const DhakaTime &t);
// The same date two years earlier (29 Feb becomes 28 Feb).
std::string IsoDateTwoYearsBefore(const DhakaTime &t);
// Sunday to Thursday, around the session (pre-open to shortly after the close).
// Holidays still count, so this only decides how soon to re-check the status.
bool InTradingWindow(const DhakaTime &t);

} // namespace dse
