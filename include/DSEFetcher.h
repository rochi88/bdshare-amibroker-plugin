#pragma once
// ============================================================================
// DSEFetcher.h
// WinHTTP client for the legacy DSE site, so the DLL needs no extra runtime
// dependencies. Safe to use from several threads.
//
// Only old.dsebd.org is used. The current site (dse.com.bd) answers declared
// automated clients with "403 Forbidden: automated access to this site is not
// permitted", so do not point this at it or send a browser User-Agent to get
// past that. See README, "Data source".
// ============================================================================

#include "DSEParsers.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <string>
#include <vector>

class DSEFetcher {
public:
  bool Init();
  void Shutdown();
  // Stops further retries; requests already in flight end at their timeout.
  void Cancel() { m_cancelled = true; }
  void ResetCancel() { m_cancelled = false; }

  bool FetchLive(std::vector<DSEQuote> &out);
  // Dates are YYYY-MM-DD. The site serves at most the last two years.
  bool FetchHistorical(const std::string &symbol, const std::string &startDate,
                       const std::string &endDate, std::vector<DSEBar> &out);
  bool FetchDepth(const std::string &symbol, DSEDepth &out);
  bool FetchNews(std::vector<DSENewsItem> &out);
  bool FetchMarketStatus(std::string &status);

  // Description of the last failure on the calling thread.
  static std::string LastError();

private:
  bool Request(const wchar_t *verb, const std::string &pathAndQuery,
               const wchar_t *headers, const std::string &postBody,
               std::string &responseBody);
  bool RequestOnce(const wchar_t *verb, const std::wstring &path,
                   const wchar_t *headers, const std::string &postBody,
                   std::string &responseBody, bool &retryable);

  HINTERNET m_hSession = nullptr;
  HINTERNET m_hConnect = nullptr;
  std::atomic<bool> m_cancelled{false};
};
