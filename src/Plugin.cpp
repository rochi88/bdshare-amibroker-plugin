// ============================================================================
// Plugin.cpp
// AmiBroker Data Plugin DLL — BDShare DSE data feed
//
// Exported entry points (ADK 2.10):
//   GetPluginInfo()       – identification
//   GetQuotesEx()         – primary data delivery
//   GetExtraData()        – depth, sector, news via AmiVar arrays
//   Configure()           – settings dialog + InfoSite hook
//   Notify()              – database/settings change notifications
//   GetPluginStatus()     – status bar icon + text
//
// Streaming:
//   Background thread polls DSE every ~5 s and posts
//   WM_USER_STREAMING_UPDATE to AmiBroker's main window to trigger refresh.
// ============================================================================

#include "../include/Plugin.h"
#include "../include/DSEFetcher.h"

#include <windows.h>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <sstream>

// ---------------------------------------------------------------------------
// Plugin identity
// ---------------------------------------------------------------------------
#define PLUGIN_NAME     "BDShare DSE Data Feed"
#define VENDOR_NAME     "BDShare"
#define PLUGIN_VERSION   010008       // 1.0.8
#define PLUGIN_ID        PIDCODE('B','D','S','E')
#define MIN_AB_VERSION  10000000L     // AmiBroker 5.27+

// ---------------------------------------------------------------------------
// Live quote cache — keyed by uppercase symbol
// ---------------------------------------------------------------------------
static std::mutex                           g_cacheMutex;
static std::unordered_map<std::string, DSEQuote> g_liveCache;
static std::vector<DSESectorEntry>          g_sectorCache;
static std::vector<DSENewsItem>             g_newsCache;

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
static std::atomic<int>   g_statusCode { STATUS_DISCONNECTED };
static char               g_statusShort[16]  = "----";
static char               g_statusLong[256]  = "Not connected";

static void SetGlobalStatus( int code, const char* sht, const char* lng )
{
    g_statusCode = code;
    strncpy_s( g_statusShort, sht, 15 );
    strncpy_s( g_statusLong,  lng, 255 );
}

// ---------------------------------------------------------------------------
// Streaming thread
// ---------------------------------------------------------------------------
static HWND                         g_hMainWnd  = nullptr;
static std::atomic<bool>            g_running   { false };
static std::thread                  g_thread;
static DSEFetcher                   g_fetcher;

static void StreamingThread()
{
    SetGlobalStatus( STATUS_CONNECTING, "CONN", "Connecting to DSE…" );

    while ( g_running.load() )
    {
        std::vector<DSEQuote> live;
        bool ok = g_fetcher.FetchLive( live );

        if ( ok && !live.empty() ) {
            // Also fetch sector + news periodically (every ~60 s)
            static int cycle = 0;
            if ( ++cycle % 12 == 0 ) {
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
                for ( auto& q : live ) {
                    std::string key = q.symbol;
                    g_liveCache[ key ] = q;
                }
            }

            SetGlobalStatus( STATUS_OK, "LIVE", "DSE live data — OK" );

            // Notify AmiBroker that new data arrived → triggers GetQuotesEx()
            if ( g_hMainWnd )
                PostMessage( g_hMainWnd, WM_USER_STREAMING_UPDATE, 0, 0 );
        } else {
            SetGlobalStatus( STATUS_WAIT, "WAIT",
                ( "DSE fetch error: " + g_fetcher.LastError() ).c_str() );
        }

        // Poll every 5 seconds (DSE refreshes roughly every 15–30 s in real-time)
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
// GetPluginInfo  – called once at load time
// ---------------------------------------------------------------------------
PLUGINAPI int GetPluginInfo( struct PluginInfo* info )
{
    info->nStructSize   = sizeof( PluginInfo );
    info->nType         = PLUGIN_TYPE_DATA;
    info->nVersion      = PLUGIN_VERSION;
    info->nIDCode       = PLUGIN_ID;
    info->szName        = TEXT( PLUGIN_NAME );
    info->szVendor      = TEXT( VENDOR_NAME );
    info->nCertificate  = 0;
    info->nMinAmiVersion= MIN_AB_VERSION;
    return 1;
}

// ---------------------------------------------------------------------------
// GetPluginStatus  – called to paint the status bar icon
// ---------------------------------------------------------------------------
PLUGINAPI int GetPluginStatus( struct PluginStatus* status )
{
    status->nStructSize = sizeof( PluginStatus );
    status->nStatusCode = g_statusCode;

    switch ( g_statusCode ) {
        case STATUS_OK:           status->clrStatusColor = RGB(0,200,0);   break;
        case STATUS_WAIT:         status->clrStatusColor = RGB(255,165,0); break;
        case STATUS_CONNECTING:   status->clrStatusColor = RGB(0,120,215); break;
        case STATUS_ERROR:        status->clrStatusColor = RGB(220,0,0);   break;
        default:                  status->clrStatusColor = RGB(180,180,180);
    }

    strncpy_s( status->szShortMessage, g_statusShort, 15 );
    strncpy_s( status->szLongMessage,  g_statusLong,  255 );
    return 1;
}

// ---------------------------------------------------------------------------
// Configure  – called when user selects the plugin as data source
// ---------------------------------------------------------------------------
PLUGINAPI int Configure( LPCTSTR /*pszPath*/, struct InfoSite* pSite )
{
    if ( pSite ) {
        g_hMainWnd = pSite->hMainWnd;
    }

    // Start streaming thread if not already running
    if ( !g_running.load() ) {
        g_running = true;
        g_thread = std::thread( StreamingThread );
    }

    MessageBox(
        g_hMainWnd,
        TEXT( "BDShare DSE Data Feed v1.0.5\n\n"
              "Data is fetched directly from www.dsebd.org.\n\n"
              "• Live prices refresh every ~5 seconds.\n"
              "• Historical data is fetched on demand per symbol.\n"
              "• Market depth available via GetExtraData().\n"
              "• Sector & news data refresh every ~60 seconds.\n\n"
              "Set a symbol's name to its DSE code (e.g. GP, ACI, BEXIMCO).\n"
              "Use Tools → Preferences → Data → In-memory cache to control\n"
              "how many symbols AmiBroker keeps in RAM." ),
        TEXT( "BDShare DSE Plugin" ),
        MB_OK | MB_ICONINFORMATION
    );

    return 1;
}

// ---------------------------------------------------------------------------
// Notify  – database load/unload and settings changes
// ---------------------------------------------------------------------------
PLUGINAPI int Notify( struct PluginNotification* pNotify )
{
    if ( !pNotify ) return 0;

    switch ( pNotify->nReason )
    {
        case REASON_DATABASE_LOADED:
            g_hMainWnd = FindWindow( TEXT("AmiBroker"), nullptr );
            if ( !g_running.load() ) {
                g_running = true;
                g_thread  = std::thread( StreamingThread );
            }
            break;

        case REASON_DATABASE_UNLOADED:
            g_running = false;
            if ( g_thread.joinable() ) {
                g_thread.join();
            }
            SetGlobalStatus( STATUS_DISCONNECTED, "----", "Database unloaded" );
            break;

        case REASON_SETTINGS_CHANGE:
            // Nothing to do — no user-configurable settings yet
            break;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// GetQuotesEx  – primary data delivery; called by AmiBroker 5.27+
//
// AmiBroker passes:
//   ticker      – symbol string (e.g. "GP")
//   periodicity – PERIODICITY_EOD or intraday constant
//   lastValid   – index of the last valid bar already cached by AmiBroker
//   size        – maximum number of Quotation records we can write
//   quotes      – output array (pre-allocated by AmiBroker)
//   context     – reserved, may be NULL
//
// Returns the number of valid bars written (starting at index 0).
// Return lastValid+1 to signal "no update, keep existing data".
// ---------------------------------------------------------------------------
PLUGINAPI int GetQuotesEx( LPCTSTR    ticker,
                             int        periodicity,
                             int        lastValid,
                             int        size,
                             Quotation* quotes,
                             GQEContext* /*context*/ )
{
    if ( !ticker || !quotes || size <= 0 ) return 0;

#ifdef UNICODE
    char symBuf[64] = {};
    WideCharToMultiByte( CP_ACP, 0, ticker, -1, symBuf, 63, nullptr, nullptr );
    std::string symbol = symBuf;
#else
    std::string symbol = ticker;
#endif

    // -----------------------------------------------------------------------
    // EOD / Historical path
    // -----------------------------------------------------------------------
    if ( periodicity == PERIODICITY_EOD ||
         periodicity == PERIODICITY_WEEKLY ||
         periodicity == PERIODICITY_MONTHLY )
    {
        // Ask for 2 years of history
        SYSTEMTIME today = {};
        GetLocalTime( &today );

        char endDate[16], startDate[16];
        sprintf_s( endDate,   "%04d-%02d-%02d", today.wYear, today.wMonth, today.wDay );
        sprintf_s( startDate, "%04d-%02d-%02d", today.wYear - 2, today.wMonth, today.wDay );

        std::vector<DSEQuote> hist;
        if ( !g_fetcher.FetchHistorical( symbol, startDate, endDate, hist ) )
            return lastValid + 1;   // keep existing

        // Sort ascending by date
        std::sort( hist.begin(), hist.end(), []( const DSEQuote& a, const DSEQuote& b ) {
            if ( a.year  != b.year  ) return a.year  < b.year;
            if ( a.month != b.month ) return a.month < b.month;
            return a.day < b.day;
        });

        int n = std::min( (int)hist.size(), size );
        for ( int i = 0; i < n; ++i ) {
            const DSEQuote& q = hist[i];
            quotes[i].DateTime    = PackDate( q.year, q.month, q.day );
            quotes[i].Open        = q.open;
            quotes[i].High        = q.high;
            quotes[i].Low         = q.low;
            quotes[i].Price       = q.close;
            quotes[i].Volume      = q.volume;
            quotes[i].OpenInterest= 0;
            quotes[i].AuxData1    = q.value;    // Aux1 = traded value
            quotes[i].AuxData2    = (float)q.trade; // Aux2 = number of trades
        }
        return n;
    }

    // -----------------------------------------------------------------------
    // Intraday / real-time path — serve from live cache
    // -----------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lk( g_cacheMutex );
        auto it = g_liveCache.find( symbol );
        if ( it == g_liveCache.end() )
            return lastValid + 1;

        const DSEQuote& q = it->second;
        int idx = ( lastValid < 0 ) ? 0 : lastValid + 1;
        if ( idx >= size ) return lastValid + 1;

        quotes[idx].DateTime    = PackDate( q.year, q.month, q.day,
                                             q.hour, q.minute, q.second );
        quotes[idx].Open        = ( q.open  > 0 ) ? q.open  : q.ltp;
        quotes[idx].High        = ( q.high  > 0 ) ? q.high  : q.ltp;
        quotes[idx].Low         = ( q.low   > 0 ) ? q.low   : q.ltp;
        quotes[idx].Price       = q.ltp;
        quotes[idx].Volume      = q.volume;
        quotes[idx].OpenInterest= 0;
        quotes[idx].AuxData1    = q.ycp;           // Aux1 = yesterday's close
        quotes[idx].AuxData2    = (float)q.change; // Aux2 = price change

        return idx + 1;
    }
}

// ---------------------------------------------------------------------------
// GetExtraData  – non-price arrays: depth, sector, news
//
// AmiBroker AFL can access these via:
//   GetExtraData( "GP", "BidPrice",   BarCount, 1, 1 )
//   GetExtraData( "GP", "BidVol",     BarCount, 1, 1 )
//   GetExtraData( "GP", "AskPrice",   BarCount, 1, 1 )
//   GetExtraData( "GP", "AskVol",     BarCount, 1, 1 )
//   GetExtraData( "",   "SectorChg",  BarCount, 1, 1 )
//   GetExtraData( "",   "News",       BarCount, 1, 1 )  ← headline strings
// ---------------------------------------------------------------------------
PLUGINAPI AmiVar GetExtraData( LPCTSTR     pszTicker,
                                 LPCTSTR     pszName,
                                 int         nArraySize,
                                 int         /*nPeriodicity*/,
                                 void*     (*pfAlloc)(unsigned int) )
{
    AmiVar result;
    result.type  = VAR_NONE;
    result.val   = 0.0f;

    if ( !pszName || !pfAlloc ) return result;

#ifdef UNICODE
    char nameBuf[64] = {}, tickBuf[64] = {};
    WideCharToMultiByte( CP_ACP, 0, pszName,   -1, nameBuf,  63, nullptr, nullptr );
    WideCharToMultiByte( CP_ACP, 0, pszTicker, -1, tickBuf,  63, nullptr, nullptr );
    std::string name   = nameBuf;
    std::string symbol = tickBuf;
#else
    std::string name   = pszName;
    std::string symbol = pszTicker ? pszTicker : "";
#endif

    std::lock_guard<std::mutex> lk( g_cacheMutex );

    // ---- Market depth arrays -------------------------------------------- //
    auto itDep = g_liveCache.find( symbol );

    auto MakeFloatArray = [&]( float value ) -> AmiVar {
        AmiVar av;
        av.type  = VAR_ARRAY;
        float* arr = (float*)pfAlloc( sizeof(float) * nArraySize );
        for ( int i = 0; i < nArraySize; ++i ) arr[i] = value;
        av.array = arr;
        return av;
    };

    if ( name == "BidPrice" || name == "AskPrice" ||
         name == "BidVol"   || name == "AskVol" )
    {
        // Fetch fresh depth for this symbol (can't cache per-symbol here easily)
        DSEDepth dep = {};
        if ( g_fetcher.FetchDepth( symbol, dep ) && dep.levels > 0 ) {
            float val = 0.0f;
            if ( name == "BidPrice" ) val = dep.buyPrice[0];
            if ( name == "AskPrice" ) val = dep.sellPrice[0];
            if ( name == "BidVol"   ) val = (float)dep.buyVolume[0];
            if ( name == "AskVol"   ) val = (float)dep.sellVolume[0];
            return MakeFloatArray( val );
        }
        return MakeFloatArray( 0.0f );
    }

    // ---- Sector change array -------------------------------------------- //
    if ( name == "SectorChg" )
    {
        // Return first sector's change as a scalar fill (placeholder)
        float val = g_sectorCache.empty() ? 0.0f : g_sectorCache[0].change;
        return MakeFloatArray( val );
    }

    // ---- News count scalar (AFL can check if news count > threshold) ------ //
    if ( name == "NewsCount" )
    {
        AmiVar av;
        av.type = VAR_FLOAT;
        av.val  = (float)g_newsCache.size();
        return av;
    }

    return result;
}