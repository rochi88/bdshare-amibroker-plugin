// ============================================================================
// DSEParsers.cpp
// String-based HTML extraction for the legacy DSE pages. Page layouts follow
// the parsers in the bdshare Python package (bdshare/stock/*.py).
// ============================================================================

#include "DSEParsers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <functional>
#include <locale>
#include <sstream>

namespace dse {
namespace {

std::string Trim(const std::string &s) {
  static const char *const kSpace = " \t\r\n\xC2\xA0";
  const size_t start = s.find_first_not_of(kSpace);
  if (start == std::string::npos)
    return "";
  return s.substr(start, s.find_last_not_of(kSpace) - start + 1);
}

std::string StripTags(const std::string &html) {
  std::string out;
  bool inTag = false;
  for (char c : html) {
    if (c == '<')
      inTag = true;
    else if (c == '>')
      inTag = false;
    else if (!inTag)
      out += c;
  }
  return out;
}

std::string DecodeEntities(std::string s) {
  static const struct {
    const char *entity, *text;
  } kEntities[] = {{"&nbsp;", " "},  {"&lt;", "<"},   {"&gt;", ">"},
                   {"&quot;", "\""}, {"&#39;", "'"},  {"&#039;", "'"},
                   {"&amp;", "&"}}; // &amp; last, so "&amp;lt;" stays "&lt;"
  for (const auto &e : kEntities) {
    const std::string entity = e.entity;
    size_t pos = 0;
    while ((pos = s.find(entity, pos)) != std::string::npos) {
      s.replace(pos, entity.size(), e.text);
      ++pos;
    }
  }
  return s;
}

std::string CellText(const std::string &html) {
  return Trim(DecodeEntities(StripTags(html)));
}

// Text of every <tag> cell in a row.
std::vector<std::string> ExtractCells(const std::string &row,
                                      const char *tag = "td") {
  std::vector<std::string> cells;
  const std::string open = std::string("<") + tag;
  const std::string close = std::string("</") + tag + ">";
  size_t pos = 0;
  while (true) {
    const size_t start = row.find(open, pos);
    if (start == std::string::npos)
      break;
    const size_t body = row.find('>', start);
    if (body == std::string::npos)
      break;
    const size_t end = row.find(close, body);
    if (end == std::string::npos)
      break;
    cells.push_back(CellText(row.substr(body + 1, end - body - 1)));
    pos = end + close.size();
  }
  return cells;
}

void ForEachRow(const std::string &html,
                const std::function<void(const std::string &)> &fn) {
  size_t pos = 0;
  while (true) {
    const size_t start = html.find("<tr", pos);
    if (start == std::string::npos)
      return;
    const size_t end = html.find("</tr>", start);
    if (end == std::string::npos)
      return;
    fn(html.substr(start, end - start + 5));
    pos = end + 5;
  }
}

// The first <table> whose opening tag contains `cls`.
bool FindTableByClass(const std::string &html, const std::string &cls,
                      std::string &table) {
  size_t pos = 0;
  while (true) {
    const size_t start = html.find("<table", pos);
    if (start == std::string::npos)
      return false;
    const size_t tagEnd = html.find('>', start);
    if (tagEnd == std::string::npos)
      return false;
    if (html.substr(start, tagEnd - start).find(cls) != std::string::npos) {
      const size_t end = html.find("</table>", tagEnd);
      if (end == std::string::npos)
        return false;
      table = html.substr(start, end - start + 8);
      return true;
    }
    pos = tagEnd + 1;
  }
}

bool ParseIsoDate(const std::string &s, int &y, int &m, int &d) {
  if (s.size() < 10 || s[4] != '-' || s[7] != '-')
    return false;
  y = static_cast<int>(ParseNumber(s.substr(0, 4)));
  m = static_cast<int>(ParseNumber(s.substr(5, 2)));
  d = static_cast<int>(ParseNumber(s.substr(8, 2)));
  return y > 1990 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// Levels on one side of the depth page: two-cell rows with a positive price.
void ParseDepthSide(const std::string &section, float *prices, int *volumes,
                    int &levels) {
  levels = 0;
  ForEachRow(section, [&](const std::string &row) {
    const std::vector<std::string> cells = ExtractCells(row);
    if (cells.size() != 2 || levels >= DSEDepth::kMaxLevels)
      return;
    const float price = ParseNumber(cells[0]);
    if (price <= 0)
      return;
    prices[levels] = price;
    volumes[levels] = static_cast<int>(ParseNumber(cells[1]));
    ++levels;
  });
}

// Civil date from a day count since 1970-01-01 (Howard Hinnant's algorithm).
void CivilFromDays(std::int64_t z, int &y, int &m, int &d) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400 + (m <= 2 ? 1 : 0);
}

std::string FormatIso(int y, int m, int d) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
  return buf;
}

} // namespace

float ParseNumber(const std::string &raw) {
  std::string s;
  for (char c : Trim(DecodeEntities(raw)))
    if (c != ',')
      s += c;
  if (s.find_first_not_of('-') == std::string::npos)
    return 0.0f; // empty, "-" or "--"
  std::istringstream in(s);
  in.imbue(std::locale::classic());
  double value = 0.0;
  if (!(in >> value))
    return 0.0f; // "N/A" and similar
  return static_cast<float>(value);
}

std::string UrlEncode(const std::string &value) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (char ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += ch;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

bool ParseLiveTable(const std::string &html, std::vector<DSEQuote> &out) {
  out.clear();
  std::string table;
  if (!FindTableByClass(html, "shares-table", table))
    return false;
  // cols: 0=# 1=code 2=ltp 3=high 4=low 5=close 6=ycp 7=change 8=trade
  //       9=value 10=volume
  ForEachRow(table, [&](const std::string &row) {
    const std::vector<std::string> c = ExtractCells(row);
    if (c.size() < 11 || c[1].empty())
      return;
    DSEQuote q;
    q.symbol = c[1];
    q.ltp = ParseNumber(c[2]);
    q.high = ParseNumber(c[3]);
    q.low = ParseNumber(c[4]);
    q.close = ParseNumber(c[5]);
    q.ycp = ParseNumber(c[6]);
    q.change = ParseNumber(c[7]);
    q.trade = static_cast<int>(ParseNumber(c[8]));
    q.value = ParseNumber(c[9]);
    q.volume = ParseNumber(c[10]);
    out.push_back(q);
  });
  return !out.empty();
}

bool ParseDayEndTable(const std::string &html, std::vector<DSEBar> &out) {
  out.clear();
  std::string table;
  if (!FindTableByClass(html, "shares-table", table))
    return false;
  // cols: 0=# 1=date 2=code 3=ltp 4=high 5=low 6=open 7=close 8=ycp 9=trade
  //       10=value 11=volume
  ForEachRow(table, [&](const std::string &row) {
    const std::vector<std::string> c = ExtractCells(row);
    if (c.size() < 12)
      return;
    DSEBar b;
    if (!ParseIsoDate(c[1], b.year, b.month, b.day))
      return;
    const float ltp = ParseNumber(c[3]);
    b.close = ParseNumber(c[7]);
    if (b.close <= 0)
      b.close = ltp;
    if (b.close <= 0)
      return; // nothing to chart
    // Untraded days report 0 for open/high/low; never let those reach a chart.
    b.open = ParseNumber(c[6]);
    if (b.open <= 0)
      b.open = b.close;
    const float high = ParseNumber(c[4]);
    const float low = ParseNumber(c[5]);
    b.high = std::max({high, b.open, b.close});
    b.low = low > 0 ? std::min({low, b.open, b.close})
                    : std::min(b.open, b.close);
    b.trade = static_cast<int>(ParseNumber(c[9]));
    b.value = ParseNumber(c[10]);
    b.volume = ParseNumber(c[11]);
    out.push_back(b);
  });
  std::sort(out.begin(), out.end(), [](const DSEBar &a, const DSEBar &b) {
    if (a.year != b.year)
      return a.year < b.year;
    if (a.month != b.month)
      return a.month < b.month;
    return a.day < b.day;
  });
  return true;
}

bool ParseDepth(const std::string &html, DSEDepth &out) {
  out = DSEDepth();
  // Layout: a "Buy" table then a "Sell" table, each with a heading row, a
  // "<side> Price / <side> Volume" row and one row per level, followed by a
  // "Price Statistics" table.
  const size_t buy = html.find("Buy Volume");
  const size_t sellPrice = html.find("Sell Price");
  const size_t sell = html.find("Sell Volume");
  if (buy == std::string::npos || sellPrice == std::string::npos ||
      sell == std::string::npos || buy > sellPrice || sellPrice > sell)
    return false;
  size_t end = html.find("Price Statistics", sell);
  if (end == std::string::npos)
    end = html.size();
  ParseDepthSide(html.substr(buy, sellPrice - buy), out.bidPrice, out.bidVolume,
                 out.bidLevels);
  ParseDepthSide(html.substr(sell, end - sell), out.askPrice, out.askVolume,
                 out.askLevels);
  return true;
}

bool ParseNews(const std::string &html, std::vector<DSENewsItem> &out) {
  out.clear();
  std::string table;
  if (!FindTableByClass(html, "table-news", table))
    return false;
  // Each item spans several rows, each a <th> label with a <td> value.
  ForEachRow(table, [&](const std::string &row) {
    const std::vector<std::string> heads = ExtractCells(row, "th");
    const std::vector<std::string> cells = ExtractCells(row, "td");
    if (heads.empty() || cells.empty())
      return;
    if (heads[0] == "Trading Code:") {
      out.push_back(DSENewsItem());
      out.back().code = cells[0];
    } else if (out.empty()) {
      return;
    } else if (heads[0] == "News Title:") {
      out.back().title = cells[0];
    } else if (heads[0] == "Post Date:") {
      out.back().date = cells[0];
    }
  });
  return true;
}

bool ParseMarketStatus(const std::string &html, std::string &status) {
  const std::string label = "Market Status:";
  const size_t pos = html.find(label);
  if (pos == std::string::npos)
    return false;
  const size_t start = pos + label.size();
  const size_t end = html.find("</span>", start);
  if (end == std::string::npos)
    return false;
  status = CellText(html.substr(start, end - start));
  return !status.empty();
}

bool IsMarketOpenStatus(const std::string &status) {
  std::string lower;
  for (char c : status)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower == "open";
}

DhakaTime DhakaFromEpoch(std::int64_t unixSeconds) {
  const std::int64_t local = unixSeconds + 6 * 3600;
  std::int64_t days = local / 86400;
  std::int64_t rem = local % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  DhakaTime t;
  CivilFromDays(days, t.year, t.month, t.day);
  t.hour = static_cast<int>(rem / 3600);
  t.minute = static_cast<int>(rem % 3600 / 60);
  t.second = static_cast<int>(rem % 60);
  t.weekday = static_cast<int>(((days + 4) % 7 + 7) % 7); // 1970-01-01: Thursday
  return t;
}

DhakaTime DhakaNow() {
  return DhakaFromEpoch(static_cast<std::int64_t>(std::time(nullptr)));
}

std::string IsoDate(const DhakaTime &t) {
  return FormatIso(t.year, t.month, t.day);
}

std::string IsoDateTwoYearsBefore(const DhakaTime &t) {
  const int day = (t.month == 2 && t.day == 29) ? 28 : t.day;
  return FormatIso(t.year - 2, t.month, day);
}

bool InTradingWindow(const DhakaTime &t) {
  if (t.weekday > 4) // Friday, Saturday
    return false;
  const int minutes = t.hour * 60 + t.minute;
  return minutes >= 9 * 60 + 50 && minutes <= 14 * 60 + 40;
}

} // namespace dse
