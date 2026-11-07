// ============================================================================
// DSEFetcher.cpp
// WinHTTP transport for the legacy DSE site. Parsing lives in DSEParsers.cpp.
// ============================================================================

#include "DSEFetcher.h"

namespace {

const wchar_t kHost[] = L"old.dsebd.org";
// Identify the plugin honestly; see the note in DSEFetcher.h.
const wchar_t kUserAgent[] =
    L"BDShare-AmiBroker/" BDSHARE_VERSION_WSTRING
    L" (+https://github.com/rochi88/bdshare-amibroker-plugin)";

const int kAttempts = 3;
const DWORD kBaseBackoffMs = 200; // doubled on each retry

thread_local std::string t_lastError;

} // namespace

std::string DSEFetcher::LastError() { return t_lastError; }

bool DSEFetcher::Init() {
  if (m_hConnect)
    return true;
  m_hSession = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!m_hSession) {
    t_lastError = "WinHttpOpen failed";
    return false;
  }
  // Resolve, connect, send, receive (ms). A stalled request is retried rather
  // than waited out, since AmiBroker may be waiting on it.
  WinHttpSetTimeouts(m_hSession, 10000, 10000, 15000, 15000);

  // Windows 7 does not enable TLS 1.2 by default. Failure here only means the
  // system default applies.
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
  WinHttpSetOption(m_hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                   sizeof(protocols));
#ifdef WINHTTP_OPTION_DECOMPRESSION
  // The price page is about 550 KB raw and 53 KB compressed. Older Windows
  // rejects this option and simply receives the uncompressed page.
  DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
  WinHttpSetOption(m_hSession, WINHTTP_OPTION_DECOMPRESSION, &decompression,
                   sizeof(decompression));
#endif

  m_hConnect =
      WinHttpConnect(m_hSession, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!m_hConnect) {
    t_lastError = "WinHttpConnect failed";
    Shutdown();
    return false;
  }
  return true;
}

void DSEFetcher::Shutdown() {
  if (m_hConnect) {
    WinHttpCloseHandle(m_hConnect);
    m_hConnect = nullptr;
  }
  if (m_hSession) {
    WinHttpCloseHandle(m_hSession);
    m_hSession = nullptr;
  }
}

bool DSEFetcher::RequestOnce(const wchar_t *verb, const std::wstring &path,
                             const wchar_t *headers,
                             const std::string &postBody,
                             std::string &responseBody, bool &retryable) {
  responseBody.clear();
  retryable = true;

  HINTERNET hReq = WinHttpOpenRequest(
      m_hConnect, verb, path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hReq) {
    t_lastError = "WinHttpOpenRequest failed";
    return false;
  }

  bool ok = false;
  LPVOID data = postBody.empty() ? WINHTTP_NO_REQUEST_DATA
                                 : const_cast<char *>(postBody.data());
  const DWORD dataLen = static_cast<DWORD>(postBody.size());
  if (WinHttpSendRequest(hReq, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
                         headers ? static_cast<DWORD>(-1L) : 0, data, dataLen,
                         dataLen, 0) &&
      WinHttpReceiveResponse(hReq, nullptr)) {
    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status == 200) {
      ok = true;
      DWORD avail = 0;
      while (true) {
        if (!WinHttpQueryDataAvailable(hReq, &avail)) {
          t_lastError = "Connection lost while reading the response";
          ok = false;
          break;
        }
        if (avail == 0)
          break;
        const size_t offset = responseBody.size();
        responseBody.resize(offset + avail);
        DWORD read = 0;
        if (!WinHttpReadData(hReq, &responseBody[offset], avail, &read)) {
          t_lastError = "Connection lost while reading the response";
          ok = false;
          break;
        }
        responseBody.resize(offset + read);
      }
    } else {
      t_lastError = "HTTP " + std::to_string(status);
      // A 4xx answer will not change on an immediate retry.
      retryable = status < 400 || status >= 500;
    }
  } else {
    const DWORD err = GetLastError();
    if (err == ERROR_WINHTTP_SECURE_FAILURE) {
      t_lastError = "TLS certificate check failed for old.dsebd.org";
      retryable = false;
    } else {
      t_lastError = "Request failed (WinHTTP error " + std::to_string(err) + ")";
    }
  }

  WinHttpCloseHandle(hReq);
  return ok;
}

bool DSEFetcher::Request(const wchar_t *verb, const std::string &pathAndQuery,
                         const wchar_t *headers, const std::string &postBody,
                         std::string &responseBody) {
  if (!m_hConnect) {
    t_lastError = "HTTP client not initialised";
    return false;
  }
  // Paths and encoded query strings are ASCII.
  const std::wstring path(pathAndQuery.begin(), pathAndQuery.end());
  DWORD backoff = kBaseBackoffMs;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    if (attempt > 0) {
      Sleep(backoff);
      backoff *= 2;
    }
    if (m_cancelled) {
      t_lastError = "Cancelled";
      return false;
    }
    bool retryable = true;
    if (RequestOnce(verb, path, headers, postBody, responseBody, retryable))
      return true;
    if (!retryable)
      return false;
  }
  return false;
}

bool DSEFetcher::FetchLive(std::vector<DSEQuote> &out) {
  std::string html;
  if (!Request(L"GET", "/latest_share_price_scroll_l.php", nullptr, "", html))
    return false;
  if (!dse::ParseLiveTable(html, out)) {
    t_lastError = "Price page layout not recognised";
    return false;
  }
  return true;
}

bool DSEFetcher::FetchHistorical(const std::string &symbol,
                                 const std::string &startDate,
                                 const std::string &endDate,
                                 std::vector<DSEBar> &out) {
  const std::string path = "/day_end_archive.php?startDate=" + startDate +
                           "&endDate=" + endDate +
                           "&inst=" + dse::UrlEncode(symbol) + "&archive=data";
  std::string html;
  if (!Request(L"GET", path, nullptr, "", html))
    return false;
  if (!dse::ParseDayEndTable(html, out)) {
    t_lastError = "Archive page layout not recognised";
    return false;
  }
  return true;
}

bool DSEFetcher::FetchDepth(const std::string &symbol, DSEDepth &out) {
  // The endpoint expects the headers of the site's own AJAX call.
  static const wchar_t headers[] =
      L"Content-Type: application/x-www-form-urlencoded\r\n"
      L"X-Requested-With: XMLHttpRequest\r\n"
      L"Referer: https://old.dsebd.org/mkt_depth_3.php\r\n";
  std::string html;
  if (!Request(L"POST", "/ajax/load-instrument.php", headers,
               "inst=" + dse::UrlEncode(symbol), html))
    return false;
  if (!dse::ParseDepth(html, out)) {
    t_lastError = "Depth page layout not recognised";
    return false;
  }
  return true;
}

bool DSEFetcher::FetchNews(std::vector<DSENewsItem> &out) {
  // The feed behind the site's "News Archive (7 Days)" page. It expects the
  // headers of the site's own AJAX call.
  static const wchar_t headers[] =
      L"Content-Type: application/x-www-form-urlencoded\r\n"
      L"X-Requested-With: XMLHttpRequest\r\n"
      L"Referer: https://old.dsebd.org/news_archive_7days.php\r\n";
  std::string html;
  if (!Request(L"POST", "/ajax/load-news.php", headers, "criteria=2", html))
    return false;
  if (!dse::ParseNews(html, out)) {
    t_lastError = "News page layout not recognised";
    return false;
  }
  return true;
}

bool DSEFetcher::FetchMarketStatus(std::string &status) {
  std::string html;
  if (!Request(L"GET", "/", nullptr, "", html))
    return false;
  if (!dse::ParseMarketStatus(html, status)) {
    t_lastError = "Market status not found on the home page";
    return false;
  }
  return true;
}
