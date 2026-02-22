// ============================================================================
// DSEFetcher.cpp
// WinHTTP-based fetcher: all network I/O and HTML table parsing lives here.
// No external dependencies beyond Windows SDK.
// ============================================================================

#include "DSEFetcher.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DSEFetcher::DSEFetcher() : m_hSession(nullptr) {}

DSEFetcher::~DSEFetcher() { Shutdown(); }

bool DSEFetcher::Init() {
  m_hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!m_hSession) {
    m_lastError = "WinHttpOpen failed";
    return false;
  }
  // Set sensible timeouts (ms): resolve, connect, send, receive
  WinHttpSetTimeouts(m_hSession, 10000, 10000, 15000, 15000);
  return true;
}

void DSEFetcher::Shutdown() {
  if (m_hSession) {
    WinHttpCloseHandle(m_hSession);
    m_hSession = nullptr;
  }
}

// ---------------------------------------------------------------------------
// HTTP GET
// ---------------------------------------------------------------------------
bool DSEFetcher::HttpGet(const std::wstring &host, const std::wstring &path,
                         const std::wstring &query, std::string &body) {
  body.clear();

  HINTERNET hConn =
      WinHttpConnect(m_hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConn) {
    m_lastError = "Connect failed";
    return false;
  }

  std::wstring fullPath = path + (query.empty() ? L"" : L"?" + query);
  HINTERNET hReq = WinHttpOpenRequest(
      hConn, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hReq) {
    WinHttpCloseHandle(hConn);
    m_lastError = "OpenRequest failed";
    return false;
  }

  bool ok = false;
  if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(hReq, nullptr)) {
    DWORD statusCode = 0;
    DWORD sz = sizeof(DWORD);
    WinHttpQueryHeaders(hReq,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        nullptr, &statusCode, &sz, nullptr);

    if (statusCode == 200) {
      DWORD bytesAvail = 0;
      while (WinHttpQueryDataAvailable(hReq, &bytesAvail) && bytesAvail > 0) {
        std::vector<char> buf(bytesAvail + 1, 0);
        DWORD bytesRead = 0;
        WinHttpReadData(hReq, buf.data(), bytesAvail, &bytesRead);
        body.append(buf.data(), bytesRead);
      }
      ok = true;
    } else {
      m_lastError = "HTTP " + std::to_string(statusCode);
    }
  } else {
    m_lastError = "SendRequest/ReceiveResponse failed";
  }

  WinHttpCloseHandle(hReq);
  WinHttpCloseHandle(hConn);
  return ok;
}

// ---------------------------------------------------------------------------
// HTTP POST
// ---------------------------------------------------------------------------
bool DSEFetcher::HttpPost(const std::wstring &host, const std::wstring &path,
                          const std::string &postBody, std::string &body) {
  body.clear();

  HINTERNET hConn =
      WinHttpConnect(m_hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConn) {
    m_lastError = "Connect failed";
    return false;
  }

  HINTERNET hReq = WinHttpOpenRequest(
      hConn, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hReq) {
    WinHttpCloseHandle(hConn);
    m_lastError = "OpenRequest failed";
    return false;
  }

  static const wchar_t *headers =
      L"Content-Type: application/x-www-form-urlencoded\r\n";

  bool ok = false;
  if (WinHttpSendRequest(hReq, headers, (DWORD)-1L, (LPVOID)postBody.c_str(),
                         (DWORD)postBody.size(), (DWORD)postBody.size(), 0) &&
      WinHttpReceiveResponse(hReq, nullptr)) {
    DWORD statusCode = 0, sz = sizeof(DWORD);
    WinHttpQueryHeaders(hReq,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        nullptr, &statusCode, &sz, nullptr);

    if (statusCode == 200) {
      DWORD bytesAvail = 0;
      while (WinHttpQueryDataAvailable(hReq, &bytesAvail) && bytesAvail > 0) {
        std::vector<char> buf(bytesAvail + 1, 0);
        DWORD bytesRead = 0;
        WinHttpReadData(hReq, buf.data(), bytesAvail, &bytesRead);
        body.append(buf.data(), bytesRead);
      }
      ok = true;
    } else {
      m_lastError = "HTTP " + std::to_string(statusCode);
    }
  }

  WinHttpCloseHandle(hReq);
  WinHttpCloseHandle(hConn);
  return ok;
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

std::string DSEFetcher::Trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n\xC2\xA0");
  if (start == std::string::npos)
    return "";
  size_t end = s.find_last_not_of(" \t\r\n\xC2\xA0");
  return s.substr(start, end - start + 1);
}

std::string DSEFetcher::StripTags(const std::string &html) {
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

float DSEFetcher::ParseFloat(const std::string &raw) {
  std::string s;
  for (char c : raw)
    if (c != ',')
      s += c;
  s = Trim(s);
  if (s.empty() || s == "-" || s == "N/A")
    return 0.0f;
  try {
    return std::stof(s);
  } catch (...) {
    return 0.0f;
  }
}

int DSEFetcher::ParseInt(const std::string &raw) {
  std::string s;
  for (char c : raw)
    if (c != ',')
      s += c;
  s = Trim(s);
  if (s.empty() || s == "-")
    return 0;
  try {
    return std::stoi(s);
  } catch (...) {
    return 0;
  }
}

bool DSEFetcher::SplitDate(const std::string &s, int &y, int &m, int &d) {
  // Accepts: YYYY-MM-DD  or  DD-MMM-YYYY  or  DD/MM/YYYY
  if (s.size() >= 10) {
    if (s[4] == '-') {
      y = ParseInt(s.substr(0, 4));
      m = ParseInt(s.substr(5, 2));
      d = ParseInt(s.substr(8, 2));
      return y > 1990;
    }
    if (s[2] == '-' || s[2] == '/') {
      // Try DD-MM-YYYY
      d = ParseInt(s.substr(0, 2));
      m = ParseInt(s.substr(3, 2));
      y = ParseInt(s.substr(6, 4));
      return y > 1990;
    }
  }
  return false;
}

bool DSEFetcher::ExtractCells(const std::string &row,
                              std::vector<std::string> &cells) {
  cells.clear();
  size_t pos = 0;
  while (pos < row.size()) {
    // Find <td ...>
    size_t tdStart = row.find("<td", pos);
    if (tdStart == std::string::npos)
      break;
    size_t cellOpen = row.find('>', tdStart);
    if (cellOpen == std::string::npos)
      break;
    // Find </td>
    size_t tdEnd = row.find("</td>", cellOpen);
    if (tdEnd == std::string::npos)
      tdEnd = row.find("</TD>", cellOpen);
    if (tdEnd == std::string::npos)
      break;
    std::string cellHtml = row.substr(cellOpen + 1, tdEnd - cellOpen - 1);
    cells.push_back(Trim(StripTags(cellHtml)));
    pos = tdEnd + 5;
  }
  return !cells.empty();
}

bool DSEFetcher::FindTableByClass(const std::string &html,
                                  const std::string &cls,
                                  std::string &tableHtml) {
  size_t pos = 0;
  while (pos < html.size()) {
    size_t tStart = html.find("<table", pos);
    if (tStart == std::string::npos)
      return false;
    size_t tClose = html.find('>', tStart);
    if (tClose == std::string::npos)
      return false;
    std::string tag = html.substr(tStart, tClose - tStart);
    if (tag.find(cls) != std::string::npos) {
      size_t tEnd = html.find("</table>", tClose);
      if (tEnd == std::string::npos)
        tEnd = html.find("</TABLE>", tClose);
      if (tEnd == std::string::npos)
        return false;
      tableHtml = html.substr(tStart, tEnd - tStart + 8);
      return true;
    }
    pos = tClose + 1;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Fetch live data
// ---------------------------------------------------------------------------
bool DSEFetcher::FetchLive(std::vector<DSEQuote> &out) {
  std::string html;
  if (!HttpGet(DSE_HOST, PATH_LIVE, L"", html))
    if (!HttpGet(DSE_ALT_HOST, PATH_LIVE, L"", html))
      return false;
  return ParseLiveTable(html, out);
}

bool DSEFetcher::ParseLiveTable(const std::string &html,
                                std::vector<DSEQuote> &out) {
  out.clear();
  std::string tableHtml;
  if (!FindTableByClass(html, "shares-table fixedHeader", tableHtml))
    if (!FindTableByClass(html, "shares-table", tableHtml))
      return false;

  size_t pos = 0;
  bool header = true;
  SYSTEMTIME st = {};
  GetLocalTime(&st);

  while (pos < tableHtml.size()) {
    size_t rowStart = tableHtml.find("<tr", pos);
    if (rowStart == std::string::npos)
      break;
    size_t rowEnd = tableHtml.find("</tr>", rowStart);
    if (rowEnd == std::string::npos)
      break;
    std::string row = tableHtml.substr(rowStart, rowEnd - rowStart + 5);
    pos = rowEnd + 5;

    if (header) {
      header = false;
      continue;
    }

    std::vector<std::string> cells;
    if (!ExtractCells(row, cells) || cells.size() < 11)
      continue;

    DSEQuote q = {};
    strncpy_s(q.symbol, cells[1].c_str(), 31);
    q.ltp = ParseFloat(cells[2]);
    q.high = ParseFloat(cells[3]);
    q.low = ParseFloat(cells[4]);
    q.close = ParseFloat(cells[5]);
    q.open = q.close; // DSE live table doesn't expose open directly
    q.ycp = ParseFloat(cells[6]);
    q.change = ParseFloat(cells[7]);
    q.trade = ParseInt(cells[8]);
    q.value = ParseFloat(cells[9]);
    q.volume = ParseFloat(cells[10]);
    q.year = st.wYear;
    q.month = st.wMonth;
    q.day = st.wDay;
    q.hour = st.wHour;
    q.minute = st.wMinute;
    q.second = st.wSecond;

    if (q.symbol[0] != '\0')
      out.push_back(q);
  }
  return !out.empty();
}

// ---------------------------------------------------------------------------
// Fetch historical data
// ---------------------------------------------------------------------------
bool DSEFetcher::FetchHistorical(const std::string &symbol,
                                 const std::string &startDate,
                                 const std::string &endDate,
                                 std::vector<DSEQuote> &out) {
  std::string qstr = "startDate=" + startDate + "&endDate=" + endDate +
                     "&inst=" + symbol + "&archive=data";
  std::wstring query(qstr.begin(), qstr.end());

  std::string html;
  if (!HttpGet(DSE_HOST, PATH_HIST, query, html)) {
    if (!HttpGet(DSE_ALT_HOST, PATH_HIST, query, html)) {
      m_lastError = "GET failed: " + m_lastError;
      return false;
    }
  }
  return ParseHistTable(html, out);
}

bool DSEFetcher::ParseHistTable(const std::string &html,
                                std::vector<DSEQuote> &out) {
  out.clear();
  std::string tableHtml;
  if (!FindTableByClass(html, "shares-table fixedHeader", tableHtml))
    if (!FindTableByClass(html, "shares-table", tableHtml))
      return false;

  size_t pos = 0;
  bool header = true;

  while (pos < tableHtml.size()) {
    size_t rowStart = tableHtml.find("<tr", pos);
    if (rowStart == std::string::npos)
      break;
    size_t rowEnd = tableHtml.find("</tr>", rowStart);
    if (rowEnd == std::string::npos)
      break;
    std::string row = tableHtml.substr(rowStart, rowEnd - rowStart + 5);
    pos = rowEnd + 5;

    if (header) {
      header = false;
      continue;
    }

    std::vector<std::string> cells;
    if (!ExtractCells(row, cells) || cells.size() < 12)
      continue;

    // cols: 0=idx 1=date 2=symbol 3=ltp 4=high 5=low 6=open 7=close
    //       8=ycp 9=trade 10=value 11=volume
    DSEQuote q = {};
    int y = 0, m = 0, d = 0;
    if (!SplitDate(cells[1], y, m, d))
      continue;
    strncpy_s(q.symbol, cells[2].c_str(), 31);
    q.year = y;
    q.month = m;
    q.day = d;
    q.ltp = ParseFloat(cells[3]);
    q.high = ParseFloat(cells[4]);
    q.low = ParseFloat(cells[5]);
    q.open = ParseFloat(cells[6]);
    q.close = ParseFloat(cells[7]);
    q.ycp = ParseFloat(cells[8]);
    q.trade = ParseInt(cells[9]);
    q.value = ParseFloat(cells[10]);
    q.volume = ParseFloat(cells[11]);

    if (q.symbol[0] != '\0')
      out.push_back(q);
  }
  return !out.empty();
}

// ---------------------------------------------------------------------------
// Fetch market depth
// ---------------------------------------------------------------------------
bool DSEFetcher::FetchDepth(const std::string &symbol, DSEDepth &out) {
  memset(&out, 0, sizeof(out));
  std::string body = "inst=" + symbol;
  std::string html;
  if (!HttpPost(DSE_HOST, PATH_DEPTH, body, html))
    if (!HttpPost(DSE_ALT_HOST, PATH_DEPTH, body, html))
      return false;
  return ParseDepthTable(html, out);
}

bool DSEFetcher::ParseDepthTable(const std::string &html, DSEDepth &out) {
  // DSE depth page returns two columns of rows: buy side / sell side
  // Parse all <tr> rows, split each into bid/ask pair
  out.levels = 0;
  size_t pos = 0;
  while (pos < html.size() && out.levels < 10) {
    size_t rowStart = html.find("<tr", pos);
    if (rowStart == std::string::npos)
      break;
    size_t rowEnd = html.find("</tr>", rowStart);
    if (rowEnd == std::string::npos)
      break;
    std::string row = html.substr(rowStart, rowEnd - rowStart + 5);
    pos = rowEnd + 5;

    std::vector<std::string> cells;
    if (!ExtractCells(row, cells) || cells.size() < 4)
      continue;

    float bp = ParseFloat(cells[0]);
    int bv = ParseInt(cells[1]);
    float sp = ParseFloat(cells[2]);
    int sv = ParseInt(cells[3]);

    if (bp > 0 || sp > 0) {
      int i = out.levels;
      out.buyPrice[i] = bp;
      out.buyVolume[i] = bv;
      out.sellPrice[i] = sp;
      out.sellVolume[i] = sv;
      out.levels++;
    }
  }
  return out.levels > 0;
}

// ---------------------------------------------------------------------------
// Fetch sector performance
// ---------------------------------------------------------------------------
bool DSEFetcher::FetchSectorPerformance(std::vector<DSESectorEntry> &out) {
  out.clear();
  std::string html;
  if (!HttpGet(DSE_HOST, PATH_SECTOR, L"", html))
    if (!HttpGet(DSE_ALT_HOST, PATH_SECTOR, L"", html))
      return false;
  return ParseSectorTable(html, out);
}

bool DSEFetcher::ParseSectorTable(const std::string &html,
                                  std::vector<DSESectorEntry> &out) {
  std::string tableHtml;
  // Sector table often has a different class — fall back to first table
  if (!FindTableByClass(html, "table-bordered", tableHtml))
    tableHtml = html;

  bool header = true;
  size_t pos = 0;
  while (pos < tableHtml.size()) {
    size_t rowStart = tableHtml.find("<tr", pos);
    if (rowStart == std::string::npos)
      break;
    size_t rowEnd = tableHtml.find("</tr>", rowStart);
    if (rowEnd == std::string::npos)
      break;
    std::string row = tableHtml.substr(rowStart, rowEnd - rowStart + 5);
    pos = rowEnd + 5;

    if (header) {
      header = false;
      continue;
    }

    std::vector<std::string> cells;
    if (!ExtractCells(row, cells) || cells.size() < 3)
      continue;

    DSESectorEntry e = {};
    strncpy_s(e.sector, cells[0].c_str(), 63);
    e.change = ParseFloat(cells[1]);
    e.volume = ParseFloat(cells[2]);
    if (cells.size() >= 4)
      e.value = ParseFloat(cells[3]);
    out.push_back(e);
  }
  return !out.empty();
}

// ---------------------------------------------------------------------------
// Fetch news
// ---------------------------------------------------------------------------
bool DSEFetcher::FetchNews(std::vector<DSENewsItem> &out) {
  out.clear();
  std::string html;
  if (!HttpGet(DSE_HOST, PATH_NEWS, L"", html))
    if (!HttpGet(DSE_ALT_HOST, PATH_NEWS, L"", html))
      return false;
  return ParseNewsTable(html, out);
}

bool DSEFetcher::ParseNewsTable(const std::string &html,
                                std::vector<DSENewsItem> &out) {
  bool header = true;
  size_t pos = 0;
  while (pos < html.size()) {
    size_t rowStart = html.find("<tr", pos);
    if (rowStart == std::string::npos)
      break;
    size_t rowEnd = html.find("</tr>", rowStart);
    if (rowEnd == std::string::npos)
      break;
    std::string row = html.substr(rowStart, rowEnd - rowStart + 5);
    pos = rowEnd + 5;

    if (header) {
      header = false;
      continue;
    }

    std::vector<std::string> cells;
    if (!ExtractCells(row, cells) || cells.size() < 3)
      continue;

    DSENewsItem n = {};
    strncpy_s(n.date, cells[0].c_str(), 15);
    strncpy_s(n.company, cells[1].c_str(), 63);
    strncpy_s(n.headline, cells[2].c_str(), 255);
    out.push_back(n);
  }
  return !out.empty();
}
