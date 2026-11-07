// ============================================================================
// Plugin.cpp
// AmiBroker Data Plugin DLL - BDShare DSE data feed
// Built against official ADK 2.1a (Plugin.h)
// ============================================================================

#include "../include/Plugin.h"
#include "../include/BarMerge.h"
#include "../include/DSEFetcher.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Plugin identity (BDSHARE_VERSION_* come from CMakeLists.txt)
// ---------------------------------------------------------------------------
#define PLUGIN_NAME "BDShare DSE Data Feed"
#define VENDOR_NAME "BDShare"
#define PLUGIN_ID PIDCODE('B', 'D', 'S', 'E')
#define MIN_AB_VERSION 530000 // AmiBroker 5.30+ (ADK 2.1)

// ---------------------------------------------------------------------------
// Polling. The legacy site is a public service tolerating this traffic, and each
// AmiBroker running the plugin adds to it, so keep these modest.
// ---------------------------------------------------------------------------
static const ULONGLONG kSecond = 1000;
static const ULONGLONG kMinute = 60 * kSecond;

static const ULONGLONG kLiveInterval = 15 * kSecond;
static const ULONGLONG kIdleTick = 30 * kSecond;
static const ULONGLONG kMaxErrorBackoff = 2 * kMinute;
static const ULONGLONG kStatusIntervalActive = kMinute;
static const ULONGLONG kStatusIntervalIdle = 15 * kMinute;
static const ULONGLONG kNewsIntervalOpen = 10 * kMinute;
static const ULONGLONG kNewsIntervalClosed = 60 * kMinute;
static const ULONGLONG kHistoryTtl = 30 * kMinute;
static const ULONGLONG kHistoryRetryAfterFailure = kMinute;
static const ULONGLONG kDepthTtl = 30 * kSecond;

// ---------------------------------------------------------------------------
// Logging: debugger output always; a file only when BDSHARE_DEBUG is set.
// ---------------------------------------------------------------------------
static std::mutex g_logMutex;

static void LogDebug(const std::string &msg) {
  const std::string line = "[BDShare] " + msg + "\n";
  OutputDebugStringA(line.c_str());

  static const bool toFile = [] {
    char value[8] = {};
    return GetEnvironmentVariableA("BDSHARE_DEBUG", value, sizeof(value)) > 0;
  }();
  if (!toFile)
    return;
  char dir[MAX_PATH] = {};
  if (GetTempPathA(MAX_PATH, dir) == 0)
    return;
  std::lock_guard<std::mutex> lk(g_logMutex);
  std::ofstream ofs(std::string(dir) + "bdshare_plugin_debug.log",
                    std::ios::app);
  ofs << line;
}

// ---------------------------------------------------------------------------
// AFL plugin stubs - these must be exported even by pure data plugins or
// AmiBroker may refuse to load the DLL.
// ---------------------------------------------------------------------------
// MSVC C2466: zero-size arrays are illegal in C++.
// One null-sentinel entry; gFunctionTableSize=0 tells AmiBroker: no AFL
// functions.
static FunctionTag _gFTStorage = {nullptr, {nullptr, 0, 0, 0, 0, nullptr}};
FunctionTag *gFunctionTable = &_gFTStorage;
int gFunctionTableSize = 0;
struct SiteInterface gSite = {};

PLUGINAPI int GetFunctionTable(FunctionTag **ppFunctionTable) {
  *ppFunctionTable = gFunctionTable;
  return gFunctionTableSize;
}

PLUGINAPI int SetSiteInterface(struct SiteInterface *pInterface) {
  gSite = *pInterface;
  return 1;
}

// Legacy GetQuotes stub - required export for pre-5.27 compatibility
PLUGINAPI int GetQuotes(LPCTSTR /*pszTicker*/, int /*nPeriodicity*/,
                        int nLastValid, int /*nSize*/,
                        struct QuotationFormat4 * /*pQuotes*/) {
  return nLastValid + 1; // "no update"
}

// ---------------------------------------------------------------------------
// Shared state (g_cacheMutex guards everything in this block)
// ---------------------------------------------------------------------------
struct HistoryEntry {
  std::vector<DSEBar> bars;
  ULONGLONG fetchedAt = 0;
  bool ok = false;
};

struct DepthEntry {
  DSEDepth depth;
  ULONGLONG fetchedAt = 0;
  bool ok = false;
};

static std::mutex g_cacheMutex;
static std::unordered_map<std::string, DSEQuote> g_liveCache;
static std::unordered_map<std::string, HistoryEntry> g_historyCache;
static std::unordered_map<std::string, DepthEntry> g_depthCache;
static std::vector<DSENewsItem> g_newsCache;
static bool g_marketOpen = false;
static DhakaTime g_liveTime;      // when g_liveCache was last refreshed
static DhakaTime g_lastOpenDate;  // the session g_liveCache belongs to, if any
static bool g_haveOpenDate = false;

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
static std::mutex g_statusMutex;
static int g_statusCode = STATUS_DISCONNECTED;
static char g_statusShort[32] = "----";
static char g_statusLong[256] = "Not connected";

static void SetGlobalStatus(int code, const char *sht,
                            const std::string &lng) {
  std::lock_guard<std::mutex> lk(g_statusMutex);
  g_statusCode = code;
  strncpy_s(g_statusShort, sizeof(g_statusShort), sht, _TRUNCATE);
  strncpy_s(g_statusLong, sizeof(g_statusLong), lng.c_str(), _TRUNCATE);
}

// ---------------------------------------------------------------------------
// Streaming thread
// ---------------------------------------------------------------------------
static std::atomic<HWND> g_hMainWnd{nullptr};
static std::atomic<bool> g_running{false};
static std::mutex g_lifecycleMutex;
// Left on the heap so that a process exiting without Release() does not run
// std::thread's destructor on a joinable thread, which would call terminate.
static std::thread *g_thread = nullptr;
static DSEFetcher g_fetcher;

static void SleepWhileRunning(ULONGLONG ms) {
  const ULONGLONG until = GetTickCount64() + ms;
  while (g_running.load() && GetTickCount64() < until)
    Sleep(100);
}

static void StreamingThread() {
  SetGlobalStatus(STATUS_CONNECTING, "CONN", "Connecting to DSE...");

  bool open = false;
  bool statusKnown = false;
  bool haveSnapshot = false;
  bool needFinalSnapshot = false;
  ULONGLONG nextStatus = 0, nextNews = 0;
  ULONGLONG errorBackoff = kLiveInterval;

  while (g_running.load()) {
    const ULONGLONG now = GetTickCount64();
    const DhakaTime dhaka = dse::DhakaNow();

    if (now >= nextStatus) {
      std::string status;
      statusKnown = g_fetcher.FetchMarketStatus(status);
      if (statusKnown) {
        const bool nowOpen = dse::IsMarketOpenStatus(status);
        if (open && !nowOpen)
          needFinalSnapshot = true; // pick up the final close prices
        open = nowOpen;
        const bool soon = open || dse::InTradingWindow(dhaka);
        nextStatus = now + (soon ? kStatusIntervalActive : kStatusIntervalIdle);
      } else {
        // Unknown session: treat as closed, so no bars are stamped with today.
        LogDebug("Market status failed: " + DSEFetcher::LastError());
        open = false;
        nextStatus = now + kStatusIntervalActive;
      }
      std::lock_guard<std::mutex> lk(g_cacheMutex);
      g_marketOpen = open;
    }

    bool failed = false;
    std::string liveError;
    if (open || !haveSnapshot || needFinalSnapshot) {
      std::vector<DSEQuote> live;
      if (g_fetcher.FetchLive(live)) {
        {
          std::lock_guard<std::mutex> lk(g_cacheMutex);
          for (const DSEQuote &q : live)
            g_liveCache[q.symbol] = q;
          g_liveTime = dhaka;
          // Only now does the snapshot belong to this session: after a failed
          // fetch the cache still holds the previous session's prices.
          if (open) {
            g_lastOpenDate = dhaka;
            g_haveOpenDate = true;
          }
        }
        haveSnapshot = true;
        needFinalSnapshot = false;
        errorBackoff = kLiveInterval;
        if (g_hMainWnd.load())
          PostMessage(g_hMainWnd.load(), WM_USER_STREAMING_UPDATE, 0, 0);
      } else {
        failed = true;
        liveError = DSEFetcher::LastError();
        LogDebug("FetchLive failed: " + liveError);
      }
    }

    if (failed)
      SetGlobalStatus(STATUS_WAIT, "WAIT", "DSE fetch error: " + liveError);
    else if (!statusKnown)
      SetGlobalStatus(STATUS_WAIT, "WAIT", "DSE market status unavailable");
    else if (open)
      SetGlobalStatus(STATUS_OK, "LIVE", "DSE live data - OK");
    else
      SetGlobalStatus(STATUS_CLOSED, "CLSD", "DSE market closed");

    if (now >= nextNews) {
      std::vector<DSENewsItem> news;
      if (g_fetcher.FetchNews(news)) {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        g_newsCache = std::move(news);
      } else {
        LogDebug("FetchNews failed: " + DSEFetcher::LastError());
      }
      nextNews = now + (open ? kNewsIntervalOpen : kNewsIntervalClosed);
    }

    if (failed) {
      SleepWhileRunning(errorBackoff);
      errorBackoff = std::min(errorBackoff * 2, kMaxErrorBackoff);
    } else {
      SleepWhileRunning(open ? kLiveInterval : kIdleTick);
    }
  }
}

static void StartStreaming() {
  std::lock_guard<std::mutex> lk(g_lifecycleMutex);
  if (g_thread)
    return;
  if (!g_fetcher.Init()) {
    SetGlobalStatus(STATUS_ERROR, "ERR!", DSEFetcher::LastError());
    return;
  }
  g_fetcher.ResetCancel();
  g_running = true;
  g_thread = new std::thread(StreamingThread);
}

static void StopStreaming() {
  std::lock_guard<std::mutex> lk(g_lifecycleMutex);
  if (!g_thread)
    return;
  g_running = false;
  g_fetcher.Cancel();
  g_thread->join();
  delete g_thread;
  g_thread = nullptr;
}

// ---------------------------------------------------------------------------
// DLL entry point. Nothing is done here: creating or joining threads and using
// WinHTTP under the loader lock can deadlock. Init() and Release() do that work.
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls(hModule);
  return TRUE;
}

// ===========================================================================
// ADK exported functions
// ===========================================================================

PLUGINAPI int Init(void) {
  std::lock_guard<std::mutex> lk(g_lifecycleMutex);
  if (!g_fetcher.Init())
    SetGlobalStatus(STATUS_ERROR, "ERR!", "WinHTTP init failed");
  return 1;
}

PLUGINAPI int Release(void) {
  StopStreaming();
  std::lock_guard<std::mutex> lk(g_lifecycleMutex);
  g_fetcher.Shutdown();
  return 1;
}

PLUGINAPI int GetPluginInfo(struct PluginInfo *pInfo) {
  pInfo->nStructSize = sizeof(PluginInfo);
  pInfo->nType = PLUGIN_TYPE_DATA;
  pInfo->nVersion = BDSHARE_VERSION_NUM;
  pInfo->nIDCode = PLUGIN_ID;
  pInfo->nCertificate = 0;
  pInfo->nMinAmiVersion = MIN_AB_VERSION;

  // szName and szVendor are char[64], NOT LPCTSTR - use strcpy_s
  strcpy_s(pInfo->szName, sizeof(pInfo->szName), PLUGIN_NAME);
  strcpy_s(pInfo->szVendor, sizeof(pInfo->szVendor), VENDOR_NAME);

  return 1;
}

PLUGINAPI int GetPluginStatus(struct PluginStatus *status) {
  std::lock_guard<std::mutex> lk(g_statusMutex);
  status->nStructSize = sizeof(PluginStatus);
  status->nStatusCode = g_statusCode;

  switch (g_statusCode) {
  case STATUS_OK:
    status->clrStatusColor = RGB(0, 200, 0);
    break;
  case STATUS_WAIT:
    status->clrStatusColor = RGB(255, 165, 0);
    break;
  case STATUS_CONNECTING:
    status->clrStatusColor = RGB(0, 120, 215);
    break;
  case STATUS_ERROR:
    status->clrStatusColor = RGB(220, 0, 0);
    break;
  case STATUS_CLOSED:
    status->clrStatusColor = RGB(120, 140, 170);
    break;
  default:
    status->clrStatusColor = RGB(180, 180, 180);
    break;
  }

  strcpy_s(status->szShortMessage, sizeof(status->szShortMessage),
           g_statusShort);
  strcpy_s(status->szLongMessage, sizeof(status->szLongMessage), g_statusLong);

  return 1;
}

// ---------------------------------------------------------------------------
// Configure - InfoSite has no hMainWnd field, so the window is found by class.
// ---------------------------------------------------------------------------
PLUGINAPI int Configure(LPCTSTR /*pszPath*/, struct InfoSite *pSite) {
  if (!g_hMainWnd.load())
    g_hMainWnd = FindWindow(TEXT("AmiBroker"), nullptr);

  StartStreaming();

  // Import the traded symbols through InfoSite.
  int addedCount = 0;
  std::string importError;
  if (pSite && pSite->AddStockNew) {
    std::vector<DSEQuote> live;
    if (g_fetcher.FetchLive(live)) {
      for (const DSEQuote &q : live) {
        if (pSite->AddStockNew(q.symbol.c_str()))
          ++addedCount;
      }
      if (g_hMainWnd.load())
        PostMessage(g_hMainWnd.load(), WM_USER_STREAMING_UPDATE, 0, 0);
    } else {
      importError = DSEFetcher::LastError();
      LogDebug("Symbol import failed: " + importError);
    }
  }

  std::string msg = "BDShare DSE Data Feed v" BDSHARE_VERSION_STRING "\r\n\r\n"
                    "Fetches data from old.dsebd.org.\r\n";
  if (importError.empty())
    msg += "Imported " + std::to_string(addedCount) + " DSE symbols.\r\n\r\n";
  else
    msg += "Could not import symbols: " + importError + "\r\n\r\n";
  msg += "- Live prices: every 15 seconds during the session\r\n"
         "- Daily history: last 2 years, cached for 30 minutes\r\n"
         "- Market depth and news: via GetExtraData()";

  MessageBoxA(g_hMainWnd.load(), msg.c_str(), "BDShare DSE Plugin",
              MB_OK | MB_ICONINFORMATION);
  return 1;
}

// ---------------------------------------------------------------------------
// Notify - hMainWnd is reliably available here (ADK 2.1)
// ---------------------------------------------------------------------------
PLUGINAPI int Notify(struct PluginNotification *pNotify) {
  if (!pNotify)
    return 0;

  switch (pNotify->nReason) {
  case REASON_DATABASE_LOADED:
    if (pNotify->hMainWnd)
      g_hMainWnd = pNotify->hMainWnd;
    StartStreaming();
    break;

  case REASON_DATABASE_UNLOADED:
    StopStreaming();
    SetGlobalStatus(STATUS_DISCONNECTED, "----", "Database unloaded");
    break;

  case REASON_SETTINGS_CHANGE:
    break;
  }
  return 1;
}

static std::string TickerToString(LPCTSTR ticker) {
  if (!ticker)
    return std::string();
#ifdef UNICODE
  char buf[64] = {};
  WideCharToMultiByte(CP_ACP, 0, ticker, -1, buf, sizeof(buf) - 1, nullptr,
                      nullptr);
  return buf;
#else
  return ticker;
#endif
}

// Cached daily bars for a symbol, fetching them when stale. A failed fetch is
// remembered for a while so that AmiBroker's UI thread does not stall on every
// call during an outage; the last good bars are served in the meantime.
static std::vector<DSEBar> GetHistory(const std::string &symbol) {
  const ULONGLONG now = GetTickCount64();
  {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    auto it = g_historyCache.find(symbol);
    if (it != g_historyCache.end()) {
      const ULONGLONG ttl = it->second.ok ? kHistoryTtl : kHistoryRetryAfterFailure;
      if (now - it->second.fetchedAt < ttl)
        return it->second.bars;
    }
  }

  const DhakaTime today = dse::DhakaNow();
  std::vector<DSEBar> bars;
  const bool ok =
      g_fetcher.FetchHistorical(symbol, dse::IsoDateTwoYearsBefore(today),
                                dse::IsoDate(today), bars);
  if (!ok)
    LogDebug("FetchHistorical failed for " + symbol + ": " +
             DSEFetcher::LastError());

  std::lock_guard<std::mutex> lk(g_cacheMutex);
  HistoryEntry &entry = g_historyCache[symbol];
  entry.fetchedAt = now;
  entry.ok = ok;
  if (ok)
    entry.bars = std::move(bars);
  return entry.bars;
}

// ---------------------------------------------------------------------------
// GetQuotesEx
// PERIODICITY_EOD = 86400, PERIODICITY_1MIN = 60 (official values)
// ---------------------------------------------------------------------------
PLUGINAPI int GetQuotesEx(LPCTSTR ticker, int periodicity, int lastValid,
                          int size, Quotation *quotes,
                          GQEContext * /*context*/) {
  if (!ticker || !quotes || size <= 0)
    return 0;
  const std::string symbol = TickerToString(ticker);

  if (periodicity == PERIODICITY_EOD) {
    std::vector<DSEBar> bars = GetHistory(symbol);
    {
      // Show the running session's bar until the archive has it. It stays after
      // the close until the next archive refresh picks up the official row.
      std::lock_guard<std::mutex> lk(g_cacheMutex);
      auto it = g_liveCache.find(symbol);
      if (it != g_liveCache.end() && g_haveOpenDate)
        barmerge::AppendLiveBar(bars, it->second, g_lastOpenDate, g_marketOpen);
    }
    return barmerge::MergeEodBars(quotes, lastValid, size, bars);
  }

  // Intraday: DSE has no intraday history, so bars build up from the moment the
  // plugin is running. Outside a session the snapshot is stale: add nothing.
  std::lock_guard<std::mutex> lk(g_cacheMutex);
  auto it = g_liveCache.find(symbol);
  if (it == g_liveCache.end() || !g_marketOpen)
    return std::max(std::min(lastValid + 1, size), 0);
  return barmerge::MergeTick(quotes, lastValid, size, it->second, g_liveTime,
                             periodicity);
}

// Cached order book for a symbol. ok is false if it could not be fetched.
static DepthEntry GetDepth(const std::string &symbol) {
  const ULONGLONG now = GetTickCount64();
  {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    auto it = g_depthCache.find(symbol);
    if (it != g_depthCache.end() && now - it->second.fetchedAt < kDepthTtl)
      return it->second;
  }
  DepthEntry entry;
  entry.fetchedAt = now;
  entry.ok = g_fetcher.FetchDepth(symbol, entry.depth);
  if (!entry.ok)
    LogDebug("FetchDepth failed for " + symbol + ": " + DSEFetcher::LastError());
  std::lock_guard<std::mutex> lk(g_cacheMutex);
  g_depthCache[symbol] = entry;
  return entry;
}

// ---------------------------------------------------------------------------
// GetExtraData - AFL: GetExtraData("BidPrice"), etc., for the current symbol.
// ---------------------------------------------------------------------------
PLUGINAPI AmiVar GetExtraData(LPCTSTR pszTicker, LPCTSTR pszName,
                              int nArraySize, int /*nPeriodicity*/,
                              void *(*pfAlloc)(unsigned int)) {
  AmiVar result = {};
  result.type = VAR_NONE;
  if (!pszName || !pfAlloc)
    return result;

  const std::string name = TickerToString(pszName);
  const std::string symbol = TickerToString(pszTicker);

  auto MakeNumber = [](float value) {
    AmiVar av = {};
    av.type = VAR_FLOAT;
    av.val = value;
    return av;
  };
  auto MakeFloatArray = [&](float value) {
    AmiVar av = {};
    float *arr = static_cast<float *>(pfAlloc(sizeof(float) * nArraySize));
    if (!arr)
      return av;
    std::fill(arr, arr + nArraySize, value);
    av.type = VAR_ARRAY;
    av.array = arr;
    return av;
  };
  auto MakeString = [&](const std::string &value) {
    AmiVar av = {};
    char *str = static_cast<char *>(
        pfAlloc(static_cast<unsigned int>(value.size() + 1)));
    if (!str)
      return av;
    std::copy(value.begin(), value.end(), str);
    str[value.size()] = '\0';
    av.type = VAR_STRING;
    av.string = str;
    return av;
  };

  if (name == "BidPrice" || name == "AskPrice" || name == "BidVol" ||
      name == "AskVol") {
    // No network call while holding the cache lock. A side with no orders is
    // Null in AFL, so plots skip it instead of drawing a line at zero.
    const DepthEntry entry = GetDepth(symbol);
    const DSEDepth &d = entry.depth;
    float value = EMPTY_VAL;
    if (entry.ok) {
      if (name == "BidPrice" && d.bidLevels > 0)
        value = d.bidPrice[0];
      else if (name == "AskPrice" && d.askLevels > 0)
        value = d.askPrice[0];
      else if (name == "BidVol" && d.bidLevels > 0)
        value = static_cast<float>(d.bidVolume[0]);
      else if (name == "AskVol" && d.askLevels > 0)
        value = static_cast<float>(d.askVolume[0]);
    }
    return MakeFloatArray(value);
  }

  std::lock_guard<std::mutex> lk(g_cacheMutex);

  if (name == "MarketOpen")
    return MakeNumber(g_marketOpen ? 1.f : 0.f);

  // The feed lists the most recent items first.
  if (name == "NewsCount") {
    const auto count = std::count_if(
        g_newsCache.begin(), g_newsCache.end(),
        [&](const DSENewsItem &n) { return n.code == symbol; });
    return MakeNumber(static_cast<float>(count));
  }

  if (name == "NewsHeadline") {
    for (const DSENewsItem &n : g_newsCache) {
      if (n.code == symbol)
        return MakeString(n.date + ": " + n.title);
    }
    return MakeString("");
  }

  return result;
}
