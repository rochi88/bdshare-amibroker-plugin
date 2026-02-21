// ============================================================================
// Plugin.cpp
// AmiBroker Data Plugin DLL — BDShare DSE data feed
// Built against official ADK 2.1a (Plugin.h)
// ============================================================================

#include "../include/Plugin.h"
#include "../include/DSEFetcher.h"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

// ---------------------------------------------------------------------------
// Plugin identity
// ---------------------------------------------------------------------------
#define PLUGIN_NAME     "BDShare DSE Data Feed"
#define VENDOR_NAME     "BDShare"
#define PLUGIN_VERSION   010101          // 1.1.1  (MAJOR*10000 + MINOR*100 + REL)
#define PLUGIN_ID        PIDCODE('B','D','S','E')
#define MIN_AB_VERSION   530000          // AmiBroker 5.30+ (ADK 2.1)

// ---------------------------------------------------------------------------
// AFL plugin stubs — Init/Release/GetFunctionTable/SetSiteInterface must be
// exported even by pure data plugins or AmiBroker may refuse to load the DLL.
// ---------------------------------------------------------------------------
// MSVC C2466: zero-size arrays are illegal in C++.
// One null-sentinel entry; gFunctionTableSize=0 tells AmiBroker: no AFL functions.
static FunctionTag _gFTStorage = { nullptr, { nullptr, 0, 0, 0, 0, nullptr } };
FunctionTag       *gFunctionTable    = &_gFTStorage;
int                gFunctionTableSize = 0;
struct SiteInterface gSite           = {};

PLUGINAPI int Init(void)    { return 1; }
PLUGINAPI int Release(void) { return 1; }

PLUGINAPI int GetFunctionTable( FunctionTag **ppFunctionTable )
{
    *ppFunctionTable = gFunctionTable;
    return gFunctionTableSize;
}

PLUGINAPI int SetSiteInterface( struct SiteInterface *pInterface )
{
    gSite = *pInterface;
    return 1;
}

// Legacy GetQuotes stub — required export for pre-5.27 compatibility
PLUGINAPI int GetQuotes( LPCTSTR /*pszTicker*/, int /*nPeriodicity*/,
                          int nLastValid, int /*nSize*/,
                          struct QuotationFormat4 * /*pQuotes*/ )
{
    return nLastValid + 1;  // "no update"
}

// ---------------------------------------------------------------------------
// Live quote cache
// ---------------------------------------------------------------------------
static std::mutex                                g_cacheMutex;
static std::unordered_map<std::string, DSEQuote> g_liveCache;
static std::vector<DSESectorEntry>               g_sectorCache;
static std::vector<DSENewsItem>                  g_newsCache;

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
static std::atomic<int> g_statusCode { STATUS_DISCONNECTED };
static char             g_statusShort[32]  = "----";   // matches PluginStatus.szShortMessage[32]
static char             g_statusLong[256]  = "Not connected";

static void SetGlobalStatus( int code, const char* sht, const char* lng )
{
    g_statusCode = code;
    strncpy_s( g_statusShort, sizeof(g_statusShort), sht, _TRUNCATE );
    strncpy_s( g_statusLong,  sizeof(g_statusLong),  lng, _TRUNCATE );
}

// ---------------------------------------------------------------------------
// Streaming thread
// ---------------------------------------------------------------------------
static HWND              g_hMainWnd = nullptr;
static std::atomic<bool> g_running  { false };
static std::thread       g_thread;
static DSEFetcher        g_fetcher;

static void StreamingThread()
{
    SetGlobalStatus( STATUS_CONNECTING, "CONN", "Connecting to DSE..." );

    while ( g_running.load() )
    {
        std::vector<DSEQuote> live;
        bool ok = g_fetcher.FetchLive( live );

        if ( ok && !live.empty() )
        {
            static int cycle = 0;
            if ( ++cycle % 12 == 0 )
            {
                std::vector<DSESectorEntry> sec;
                if ( g_fetcher.FetchSectorPerformance( sec ) ) {
                    std::lock_guard<std::mutex> lk( g_cacheMutex );
                    g_sectorCache = std::move( sec );
                }
                std::vector<DSENewsItem> news;
                if ( g_fetcher.FetchNews( news ) ) {
                    std::lock_guard<std::mutex> lk( g_cacheMutex );
                    g_newsCache = std::move( news );
                }
            }

            {
                std::lock_guard<std::mutex> lk( g_cacheMutex );
                for ( auto& q : live )
                    g_liveCache[ q.symbol ] = q;
            }

            SetGlobalStatus( STATUS_OK, "LIVE", "DSE live data - OK" );

            // WM_USER_STREAMING_UPDATE = WM_USER+13000 (official ADK value)
            if ( g_hMainWnd )
                PostMessage( g_hMainWnd, WM_USER_STREAMING_UPDATE, 0, 0 );
        }
        else
        {
            SetGlobalStatus( STATUS_WAIT, "WAIT",
                ("DSE fetch error: " + g_fetcher.LastError()).c_str() );
        }

        for ( int i = 0; i < 50 && g_running.load(); ++i )
            Sleep( 100 );
    }
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain( HMODULE /*hModule*/, DWORD reason, LPVOID /*reserved*/ )
{
    if ( reason == DLL_PROCESS_ATTACH ) {
        if ( !g_fetcher.Init() )
            SetGlobalStatus( STATUS_ERROR, "ERR!", "WinHTTP init failed" );
    }
    else if ( reason == DLL_PROCESS_DETACH ) {
        g_running = false;
        if ( g_thread.joinable() )
            g_thread.join();
        g_fetcher.Shutdown();
    }
    return TRUE;
}

// ===========================================================================
// ADK exported functions
// ===========================================================================

// ---------------------------------------------------------------------------
// GetPluginInfo
// ---------------------------------------------------------------------------
PLUGINAPI int GetPluginInfo( struct PluginInfo *pInfo )
{
    pInfo->nStructSize    = sizeof( PluginInfo );
    pInfo->nType          = PLUGIN_TYPE_DATA;
    pInfo->nVersion       = PLUGIN_VERSION;
    pInfo->nIDCode        = PLUGIN_ID;
    pInfo->nCertificate   = 0;
    pInfo->nMinAmiVersion = MIN_AB_VERSION;

    // szName and szVendor are char[64], NOT LPCTSTR — use strcpy_s
    strcpy_s( pInfo->szName,   sizeof(pInfo->szName),   PLUGIN_NAME );
    strcpy_s( pInfo->szVendor, sizeof(pInfo->szVendor), VENDOR_NAME );

    return 1;
}

// ---------------------------------------------------------------------------
// GetPluginStatus
// ---------------------------------------------------------------------------
PLUGINAPI int GetPluginStatus( struct PluginStatus *status )
{
    status->nStructSize = sizeof( PluginStatus );
    status->nStatusCode = g_statusCode;

    switch ( g_statusCode ) {
        case STATUS_OK:         status->clrStatusColor = RGB(0,200,0);   break;
        case STATUS_WAIT:       status->clrStatusColor = RGB(255,165,0); break;
        case STATUS_CONNECTING: status->clrStatusColor = RGB(0,120,215); break;
        case STATUS_ERROR:      status->clrStatusColor = RGB(220,0,0);   break;
        default:                status->clrStatusColor = RGB(180,180,180);
    }

    strcpy_s( status->szShortMessage, sizeof(status->szShortMessage), g_statusShort );
    strcpy_s( status->szLongMessage,  sizeof(status->szLongMessage),  g_statusLong  );
    return 1;
}

// ---------------------------------------------------------------------------
// Configure — hMainWnd comes from InfoSite in earlier ADK; from
//             PluginNotification in ADK 2.1.  InfoSite has no hMainWnd field,
//             so we save the path and grab the window via FindWindow instead.
// ---------------------------------------------------------------------------
PLUGINAPI int Configure( LPCTSTR /*pszPath*/, struct InfoSite * /*pSite*/ )
{
    // Grab the main window if we don't have it yet
    if ( !g_hMainWnd )
        g_hMainWnd = FindWindow( TEXT("AmiBroker"), nullptr );

    if ( !g_running.load() ) {
        g_running = true;
        g_thread  = std::thread( StreamingThread );
    }

    MessageBox(
        g_hMainWnd,
        TEXT("BDShare DSE Data Feed v1.0.8\r\n\r\n"
             "Fetches live and historical data from www.dsebd.org.\r\n\r\n"
             "- Live prices: polled every ~5 seconds\r\n"
             "- Historical OHLCV: fetched on demand (up to 2 years)\r\n"
             "- Market depth, sector, news via GetExtraData()\r\n\r\n"
             "Set each symbol's ticker to its DSE code (e.g. GP, ACI)."),
        TEXT("BDShare DSE Plugin"),
        MB_OK | MB_ICONINFORMATION
    );
    return 1;
}

// ---------------------------------------------------------------------------
// Notify — hMainWnd is reliably available here (ADK 2.1)
// ---------------------------------------------------------------------------
PLUGINAPI int Notify( struct PluginNotification *pNotify )
{
    if ( !pNotify ) return 0;

    switch ( pNotify->nReason )
    {
        case REASON_DATABASE_LOADED:
            // hMainWnd is in PluginNotification, not InfoSite
            if ( pNotify->hMainWnd )
                g_hMainWnd = pNotify->hMainWnd;
            if ( !g_running.load() ) {
                g_running = true;
                g_thread  = std::thread( StreamingThread );
            }
            break;

        case REASON_DATABASE_UNLOADED:
            g_running = false;
            if ( g_thread.joinable() )
                g_thread.join();
            SetGlobalStatus( STATUS_DISCONNECTED, "----", "Database unloaded" );
            break;

        case REASON_SETTINGS_CHANGE:
            break;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// GetQuotesEx
// PERIODICITY_EOD = 86400, PERIODICITY_1MIN = 60 (official values)
// ---------------------------------------------------------------------------
PLUGINAPI int GetQuotesEx( LPCTSTR      ticker,
                             int          periodicity,
                             int          lastValid,
                             int          size,
                             Quotation   *quotes,
                             GQEContext * /*context*/ )
{
    if ( !ticker || !quotes || size <= 0 ) return 0;

#ifdef UNICODE
    char symBuf[64] = {};
    WideCharToMultiByte( CP_ACP, 0, ticker, -1, symBuf, 63, nullptr, nullptr );
    std::string symbol = symBuf;
#else
    std::string symbol = ticker;
#endif

    // ── EOD / Historical path ─────────────────────────────────────────────
    if ( periodicity == PERIODICITY_EOD )
    {
        SYSTEMTIME today = {};
        GetLocalTime( &today );

        char endDate[16], startDate[16];
        sprintf_s( endDate,   "%04d-%02d-%02d", today.wYear, today.wMonth, today.wDay );
        sprintf_s( startDate, "%04d-%02d-%02d", today.wYear - 2, today.wMonth, today.wDay );

        std::vector<DSEQuote> hist;
        if ( !g_fetcher.FetchHistorical( symbol, startDate, endDate, hist ) )
            return lastValid + 1;

        std::sort( hist.begin(), hist.end(), []( const DSEQuote& a, const DSEQuote& b ) {
            if ( a.year  != b.year  ) return a.year  < b.year;
            if ( a.month != b.month ) return a.month < b.month;
            return a.day < b.day;
        });

        int n = std::min( (int)hist.size(), size );
        for ( int i = 0; i < n; ++i ) {
            const DSEQuote& q = hist[i];
            // EOD bar: use sentinel hours/minutes so AB treats it as end-of-day
            quotes[i].DateTime    = MakeAmiDate( q.year, q.month, q.day );
            quotes[i].Open        = q.open;
            quotes[i].High        = q.high;
            quotes[i].Low         = q.low;
            quotes[i].Price       = q.close;
            quotes[i].Volume      = q.volume;
            quotes[i].OpenInterest= 0.f;
            quotes[i].AuxData1    = q.value;
            quotes[i].AuxData2    = (float)q.trade;
        }
        return n;
    }

    // ── Intraday / real-time path ─────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk( g_cacheMutex );
        auto it = g_liveCache.find( symbol );
        if ( it == g_liveCache.end() )
            return lastValid + 1;

        const DSEQuote& q = it->second;
        int idx = ( lastValid < 0 ) ? 0 : lastValid + 1;
        if ( idx >= size ) return lastValid + 1;

        quotes[idx].DateTime    = MakeAmiDate( q.year, q.month, q.day,
                                               q.hour, q.minute, q.second );
        quotes[idx].Open        = ( q.open > 0.f ) ? q.open : q.ltp;
        quotes[idx].High        = ( q.high > 0.f ) ? q.high : q.ltp;
        quotes[idx].Low         = ( q.low  > 0.f ) ? q.low  : q.ltp;
        quotes[idx].Price       = q.ltp;
        quotes[idx].Volume      = q.volume;
        quotes[idx].OpenInterest= 0.f;
        quotes[idx].AuxData1    = q.ycp;
        quotes[idx].AuxData2    = q.change;
        return idx + 1;
    }
}

// ---------------------------------------------------------------------------
// GetExtraData
// ---------------------------------------------------------------------------
PLUGINAPI AmiVar GetExtraData( LPCTSTR  pszTicker,
                                 LPCTSTR  pszName,
                                 int      nArraySize,
                                 int      /*nPeriodicity*/,
                                 void*  (*pfAlloc)(unsigned int) )
{
    AmiVar result = {};
    result.type = VAR_NONE;

    if ( !pszName || !pfAlloc ) return result;

#ifdef UNICODE
    char nameBuf[64] = {}, tickBuf[64] = {};
    WideCharToMultiByte( CP_ACP, 0, pszName,   -1, nameBuf, 63, nullptr, nullptr );
    WideCharToMultiByte( CP_ACP, 0, pszTicker, -1, tickBuf, 63, nullptr, nullptr );
    std::string name   = nameBuf;
    std::string symbol = tickBuf;
#else
    std::string name   = pszName;
    std::string symbol = pszTicker ? pszTicker : "";
#endif

    auto MakeFloatArray = [&]( float value ) -> AmiVar {
        AmiVar av = {};
        av.type  = VAR_ARRAY;
        float *arr = static_cast<float*>( pfAlloc( sizeof(float) * nArraySize ) );
        if ( arr ) {
            for ( int i = 0; i < nArraySize; ++i ) arr[i] = value;
        }
        av.array = arr;
        return av;
    };

    std::lock_guard<std::mutex> lk( g_cacheMutex );

    if ( name == "BidPrice" || name == "AskPrice" ||
         name == "BidVol"   || name == "AskVol" )
    {
        DSEDepth dep = {};
        if ( g_fetcher.FetchDepth( symbol, dep ) && dep.levels > 0 ) {
            float val = 0.f;
            if      ( name == "BidPrice" ) val = dep.buyPrice[0];
            else if ( name == "AskPrice" ) val = dep.sellPrice[0];
            else if ( name == "BidVol"   ) val = (float)dep.buyVolume[0];
            else if ( name == "AskVol"   ) val = (float)dep.sellVolume[0];
            return MakeFloatArray( val );
        }
        return MakeFloatArray( 0.f );
    }

    if ( name == "SectorChg" ) {
        float val = g_sectorCache.empty() ? 0.f : g_sectorCache[0].change;
        return MakeFloatArray( val );
    }

    if ( name == "NewsCount" ) {
        AmiVar av = {};
        av.type = VAR_FLOAT;
        av.val  = (float)g_newsCache.size();
        return av;
    }

    return result;
}