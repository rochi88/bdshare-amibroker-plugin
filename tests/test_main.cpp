// Unit tests for the parsers and bar logic. Fixtures in tests/fixtures/ are real
// old.dsebd.org pages, trimmed to a few rows. Run with ctest.

#include "BarMerge.h"
#include "DSEParsers.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                          \
  } while (0)

#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 0.001)

static std::string Fixture(const char *name) {
  std::ifstream in(std::string(FIXTURE_DIR) + name, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  if (ss.str().empty())
    std::printf("Missing fixture: %s\n", name);
  return ss.str();
}

static const DSEQuote *FindQuote(const std::vector<DSEQuote> &v,
                                 const std::string &symbol) {
  for (const DSEQuote &q : v)
    if (q.symbol == symbol)
      return &q;
  return nullptr;
}

static void TestParseNumber() {
  CHECK_NEAR(dse::ParseNumber("1,234.5"), 1234.5);
  CHECK_NEAR(dse::ParseNumber("  3,058,540 "), 3058540.0);
  CHECK_NEAR(dse::ParseNumber("-1.20"), -1.2); // the sign is kept
  CHECK_NEAR(dse::ParseNumber("--"), 0.0);
  CHECK_NEAR(dse::ParseNumber("-"), 0.0);
  CHECK_NEAR(dse::ParseNumber(""), 0.0);
  CHECK_NEAR(dse::ParseNumber("N/A"), 0.0);
  CHECK_NEAR(dse::ParseNumber("&nbsp;7.5"), 7.5);
}

static void TestUrlEncode() {
  CHECK(dse::UrlEncode("GP") == "GP");
  CHECK(dse::UrlEncode("AL-HAJTEX") == "AL-HAJTEX");
  CHECK(dse::UrlEncode("AMCL(PRAN)") == "AMCL%28PRAN%29");
  CHECK(dse::UrlEncode("A&B=C D") == "A%26B%3DC%20D");
}

static void TestLiveTable() {
  std::vector<DSEQuote> q;
  CHECK(dse::ParseLiveTable(Fixture("legacy_live.html"), q));
  CHECK(q.size() == 4);

  const DSEQuote *prime = FindQuote(q, "1STPRIMFMF");
  CHECK(prime != nullptr);
  if (prime) {
    CHECK_NEAR(prime->ltp, 42.3);
    CHECK_NEAR(prime->high, 42.6);
    CHECK_NEAR(prime->low, 40.1);
    CHECK_NEAR(prime->close, 42.3);
    CHECK_NEAR(prime->ycp, 40.6);
    CHECK_NEAR(prime->change, 1.7);
    CHECK(prime->trade == 2565);
    CHECK_NEAR(prime->value, 127.348);
    CHECK_NEAR(prime->volume, 3058540.0);
  }

  const DSEQuote *untraded = FindQuote(q, "BDSERVICE");
  CHECK(untraded != nullptr);
  if (untraded) {
    CHECK_NEAR(untraded->ltp, 0.0);
    CHECK_NEAR(untraded->change, 0.0); // the page shows "--"
    CHECK_NEAR(untraded->close, 5.2);
  }

  CHECK(!dse::ParseLiveTable("<html>redesigned page</html>", q));
}

static void TestDayEndTable() {
  std::vector<DSEBar> b;
  CHECK(dse::ParseDayEndTable(Fixture("legacy_day_end.html"), b));
  CHECK(b.size() == 10);
  if (b.size() == 10) {
    CHECK(b[0].year * 10000 + b[0].month * 100 + b[0].day <
          b[1].year * 10000 + b[1].month * 100 + b[1].day); // oldest first
    const DSEBar &last = b.back();
    CHECK(last.year == 2026 && last.month == 11 && last.day == 5);
    CHECK_NEAR(last.open, 230.2);
    CHECK_NEAR(last.high, 231.0);
    CHECK_NEAR(last.low, 226.6);
    CHECK_NEAR(last.close, 226.9);
    CHECK(last.trade == 3370);
    CHECK_NEAR(last.value, 57.281);
    CHECK_NEAR(last.volume, 251543.0);
  }

  // Untraded days report zeros: must become a flat bar at the close.
  CHECK(dse::ParseDayEndTable(Fixture("legacy_day_end_untraded.html"), b));
  CHECK(b.size() == 2);
  for (const DSEBar &bar : b) {
    CHECK_NEAR(bar.close, 5.2);
    CHECK_NEAR(bar.open, 5.2);
    CHECK_NEAR(bar.high, 5.2);
    CHECK_NEAR(bar.low, 5.2);
    CHECK_NEAR(bar.volume, 0.0);
  }

  CHECK(dse::ParseDayEndTable(Fixture("legacy_day_end_paren.html"), b));
  CHECK(b.size() == 2);

  // "No Day End Data" is success with no rows, not a layout failure.
  CHECK(dse::ParseDayEndTable(Fixture("legacy_day_end_empty.html"), b));
  CHECK(b.empty());
  CHECK(!dse::ParseDayEndTable("<html>Not found</html>", b));
}

static void TestDepth() {
  // Captured while the market was closed: two bids, no asks.
  const std::string html = Fixture("legacy_depth.html");
  DSEDepth d;
  CHECK(dse::ParseDepth(html, d));
  CHECK(d.bidLevels == 2);
  CHECK(d.askLevels == 0);
  CHECK_NEAR(d.bidPrice[0], 226.6);
  CHECK(d.bidVolume[0] == 10);
  CHECK_NEAR(d.bidPrice[1], 226.5);
  CHECK(d.bidVolume[1] == 89);

  // The same page with sell rows added, in the same markup as the buy rows.
  const std::string level =
      "<tr><td bgcolor=\"#CCCCCC\"><div align=\"center\">227.10</div></td>"
      "<td bgcolor=\"#CCCCCC\"><div align=\"center\">1,500</div></td></tr>";
  std::string withAsks = html;
  const size_t sell = withAsks.find("Sell Volume");
  CHECK(sell != std::string::npos);
  if (sell != std::string::npos) {
    const size_t rowEnd = withAsks.find("</tr>", sell) + 5;
    withAsks.insert(rowEnd, level);
    CHECK(dse::ParseDepth(withAsks, d));
    CHECK(d.bidLevels == 2);
    CHECK(d.askLevels == 1);
    CHECK_NEAR(d.askPrice[0], 227.1);
    CHECK(d.askVolume[0] == 1500);
  }

  CHECK(!dse::ParseDepth("<html>no book here</html>", d));
}

static void TestNews() {
  std::vector<DSENewsItem> n;
  CHECK(dse::ParseNews(Fixture("legacy_news.html"), n));
  CHECK(n.size() == 3);
  if (n.size() == 3) {
    CHECK(n[0].code == "EXCH");
    CHECK(n[0].title == "DSE NEWS: Daily Turnover of Main Board");
    CHECK(n[0].date == "2026-11-05");
    CHECK(n[1].code == "ACMEPL");
  }
  CHECK(!dse::ParseNews("<html></html>", n));
}

static void TestMarketStatus() {
  std::string s;
  CHECK(dse::ParseMarketStatus(Fixture("legacy_home.html"), s));
  CHECK(s == "Closed");
  CHECK(!dse::IsMarketOpenStatus(s));
  CHECK(dse::ParseMarketStatus(
      "Market Status: <span class=\"green\"><b>Open</b></span></span>", s));
  CHECK(dse::IsMarketOpenStatus(s));
  CHECK(dse::IsMarketOpenStatus("OPEN"));
  CHECK(!dse::IsMarketOpenStatus("Pre-Open"));
  CHECK(!dse::ParseMarketStatus("<html>nothing</html>", s));
}

static void TestDhakaTime() {
  // 2026-11-05 08:30:15 UTC, a Thursday.
  const DhakaTime t = dse::DhakaFromEpoch(1793867415LL);
  CHECK(t.year == 2026 && t.month == 11 && t.day == 5);
  CHECK(t.hour == 14 && t.minute == 30 && t.second == 15);
  CHECK(t.weekday == 4);
  CHECK(dse::IsoDate(t) == "2026-11-05");
  CHECK(dse::IsoDateTwoYearsBefore(t) == "2024-11-05");
  CHECK(dse::InTradingWindow(t));

  // 19:00 UTC on Thursday is already 01:00 on Friday in Dhaka.
  const DhakaTime friday = dse::DhakaFromEpoch(1793905200LL);
  CHECK(friday.day == 6 && friday.hour == 1 && friday.weekday == 5);
  CHECK(!dse::InTradingWindow(friday));

  DhakaTime leap;
  leap.year = 2028;
  leap.month = 2;
  leap.day = 29;
  CHECK(dse::IsoDateTwoYearsBefore(leap) == "2026-02-28");
}

static DSEBar MakeBar(int y, int m, int d, float close) {
  DSEBar b;
  b.year = y;
  b.month = m;
  b.day = d;
  b.open = b.high = b.low = b.close = close;
  b.volume = 100;
  b.value = 5;
  b.trade = 7;
  return b;
}

static void TestMergeEod() {
  std::vector<Quotation> q(5);
  std::vector<DSEBar> bars = {MakeBar(2026, 1, 1, 1), MakeBar(2026, 1, 2, 2)};

  // Empty array.
  CHECK(barmerge::MergeEodBars(q.data(), -1, 5, bars) == 2);
  CHECK_NEAR(q[1].Price, 2.0);
  CHECK(q[1].DateTime.PackDate.Hour == DATE_EOD_HOURS);
  CHECK_NEAR(q[1].AuxData1, 5.0);
  CHECK_NEAR(q[1].AuxData2, 7.0);

  // Idempotent when the same data is merged again.
  CHECK(barmerge::MergeEodBars(q.data(), 1, 5, bars) == 2);

  // Stored bars older than the fetched range survive.
  q[0].DateTime = MakeAmiDate(2020, 5, 5);
  q[0].Price = 99;
  q[1].DateTime = MakeAmiDate(2026, 1, 1);
  CHECK(barmerge::MergeEodBars(q.data(), 1, 5, bars) == 3);
  CHECK_NEAR(q[0].Price, 99.0);
  CHECK_NEAR(q[1].Price, 1.0);
  CHECK_NEAR(q[2].Price, 2.0);

  // Nothing fetched: keep what is stored, never report more than the array.
  CHECK(barmerge::MergeEodBars(q.data(), 2, 5, {}) == 3);
  CHECK(barmerge::MergeEodBars(q.data(), -1, 5, {}) == 0);

  // More bars than fit: the newest are kept.
  std::vector<Quotation> small(2);
  std::vector<DSEBar> many = {MakeBar(2026, 1, 1, 1), MakeBar(2026, 1, 2, 2),
                              MakeBar(2026, 1, 3, 3)};
  CHECK(barmerge::MergeEodBars(small.data(), -1, 2, many) == 2);
  CHECK_NEAR(small[0].Price, 2.0);
  CHECK_NEAR(small[1].Price, 3.0);

  // Full array with old bars: the oldest are dropped to make room.
  std::vector<Quotation> full(3);
  full[0].DateTime = MakeAmiDate(2020, 1, 1);
  full[0].Price = 10;
  full[1].DateTime = MakeAmiDate(2020, 1, 2);
  full[1].Price = 11;
  full[2].DateTime = MakeAmiDate(2020, 1, 3);
  full[2].Price = 12;
  CHECK(barmerge::MergeEodBars(full.data(), 2, 3, bars) == 3);
  CHECK_NEAR(full[0].Price, 12.0);
  CHECK_NEAR(full[1].Price, 1.0);
  CHECK_NEAR(full[2].Price, 2.0);
}

static DSEQuote MakeQuote(float ltp, float volume) {
  DSEQuote q;
  q.symbol = "GP";
  q.ltp = ltp;
  q.high = ltp + 1;
  q.low = ltp - 1;
  q.close = 0;
  q.ycp = ltp - 0.5f;
  q.volume = volume;
  q.value = 3;
  q.trade = 9;
  return q;
}

static DhakaTime At(int hour, int minute, int second) {
  DhakaTime t;
  t.year = 2026;
  t.month = 11;
  t.day = 8;
  t.hour = hour;
  t.minute = minute;
  t.second = second;
  return t;
}

static void TestAppendLiveBar() {
  DhakaTime today = At(11, 0, 0);
  std::vector<DSEBar> bars = {MakeBar(2026, 11, 5, 100)};

  DSEQuote q = MakeQuote(105, 1000);
  barmerge::AppendLiveBar(bars, q, today, true);
  CHECK(bars.size() == 2);
  CHECK(bars[1].day == 8);
  CHECK_NEAR(bars[1].close, 105.0);
  CHECK_NEAR(bars[1].open, 104.5); // yesterday's close stands in for the open
  CHECK_NEAR(bars[1].volume, 1000.0);

  // After the close, the page's close price is the final one.
  std::vector<DSEBar> afterClose = {MakeBar(2026, 11, 5, 100)};
  q.close = 104.8f;
  barmerge::AppendLiveBar(afterClose, q, today, false);
  CHECK_NEAR(afterClose[1].close, 104.8);

  // The archive already has today: its row wins.
  std::vector<DSEBar> archived = {MakeBar(2026, 11, 8, 101)};
  barmerge::AppendLiveBar(archived, q, today, true);
  CHECK(archived.size() == 1);
  CHECK_NEAR(archived[0].close, 101.0);

  // Not traded today: no bar.
  std::vector<DSEBar> none = {MakeBar(2026, 11, 5, 100)};
  DSEQuote untraded;
  untraded.close = 5.2f;
  barmerge::AppendLiveBar(none, untraded, today, true);
  CHECK(none.size() == 1);

  // A gap-up stays inside the day's range: open is clamped to the low.
  std::vector<DSEBar> gap;
  DSEQuote up = MakeQuote(110, 10);
  up.low = 108;
  up.high = 112;
  up.ycp = 100;
  barmerge::AppendLiveBar(gap, up, today, true);
  CHECK_NEAR(gap[0].open, 108.0);
}

static void TestMergeTick() {
  std::vector<Quotation> q(3);

  // First bar: the day's cumulative volume so far.
  int n = barmerge::MergeTick(q.data(), -1, 3, MakeQuote(100, 1000), At(10, 0, 20),
                              60);
  CHECK(n == 1);
  CHECK(q[0].DateTime.PackDate.Hour == 10);
  CHECK(q[0].DateTime.PackDate.Minute == 0);
  CHECK(q[0].DateTime.PackDate.Second == 0);
  CHECK_NEAR(q[0].Open, 100.0);
  CHECK_NEAR(q[0].High, 100.0); // the bar's range, not the day's
  CHECK_NEAR(q[0].Low, 100.0);
  CHECK_NEAR(q[0].Volume, 1000.0);

  // Same minute: the bar is updated, not duplicated.
  n = barmerge::MergeTick(q.data(), 0, 3, MakeQuote(102, 1200), At(10, 0, 40), 60);
  CHECK(n == 1);
  CHECK_NEAR(q[0].Open, 100.0);
  CHECK_NEAR(q[0].High, 102.0);
  CHECK_NEAR(q[0].Price, 102.0);
  CHECK_NEAR(q[0].Volume, 1200.0);

  // Next minute: a new bar holding only the volume traded since.
  n = barmerge::MergeTick(q.data(), 0, 3, MakeQuote(99, 1500), At(10, 1, 5), 60);
  CHECK(n == 2);
  CHECK_NEAR(q[1].Open, 99.0);
  CHECK_NEAR(q[1].Volume, 300.0);
  CHECK_NEAR(q[0].Volume, 1200.0);

  // Repeated calls with an unchanged snapshot change nothing.
  n = barmerge::MergeTick(q.data(), 1, 3, MakeQuote(99, 1500), At(10, 1, 5), 60);
  CHECK(n == 2);
  CHECK_NEAR(q[1].Volume, 300.0);

  // An older timestamp is ignored.
  n = barmerge::MergeTick(q.data(), 1, 3, MakeQuote(1, 1), At(9, 0, 0), 60);
  CHECK(n == 2);
  CHECK_NEAR(q[1].Price, 99.0);

  // Full array: shift out the oldest.
  n = barmerge::MergeTick(q.data(), 1, 3, MakeQuote(98, 1600), At(10, 2, 0), 60);
  CHECK(n == 3);
  n = barmerge::MergeTick(q.data(), 2, 3, MakeQuote(97, 1700), At(10, 3, 0), 60);
  CHECK(n == 3);
  CHECK(q[0].DateTime.PackDate.Minute == 1);
  CHECK_NEAR(q[2].Price, 97.0);

  // Volume resets on a new day.
  DhakaTime nextDay = At(10, 0, 0);
  nextDay.day = 9;
  n = barmerge::MergeTick(q.data(), 2, 3, MakeQuote(97, 50), nextDay, 60);
  CHECK(n == 3);
  CHECK_NEAR(q[2].Volume, 50.0);

  // An untraded symbol adds no bar.
  n = barmerge::MergeTick(q.data(), -1, 3, MakeQuote(0, 0), At(10, 0, 0), 60);
  CHECK(n == 0);

  // Five-minute bars floor to the period.
  std::vector<Quotation> five(1);
  barmerge::MergeTick(five.data(), -1, 1, MakeQuote(1, 1), At(10, 7, 59), 300);
  CHECK(five[0].DateTime.PackDate.Minute == 5);
}

int main() {
  TestParseNumber();
  TestUrlEncode();
  TestLiveTable();
  TestDayEndTable();
  TestDepth();
  TestNews();
  TestMarketStatus();
  TestDhakaTime();
  TestMergeEod();
  TestAppendLiveBar();
  TestMergeTick();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
