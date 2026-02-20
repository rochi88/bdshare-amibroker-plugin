#pragma once
// ============================================================================
// DSEFetcher.h
// HTTP layer: fetches live, historical, depth, sector, and news data from DSE
// using WinHTTP so the DLL has no external runtime dependencies.
// ============================================================================

#ifndef _DSEFETCHER_H_
#define _DSEFETCHER_H_

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <unordered_map>

#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// Data structures returned by the fetcher
// ---------------------------------------------------------------------------

struct DSEQuote
{
    char    symbol[32];
    float   open;
    float   high;
    float   low;
    float   close;
    float   ltp;        // last traded price
    float   ycp;        // yesterday close price
    float   change;
    float   volume;
    float   value;
    int     trade;
    int     year, month, day;
    int     hour, minute, second;
};

struct DSEDepth
{
    float   buyPrice[10];
    int     buyVolume[10];
    float   sellPrice[10];
    int     sellVolume[10];
    int     levels;     // number of valid levels
};

struct DSESectorEntry
{
    char    sector[64];
    float   change;
    float   volume;
    float   value;
};

struct DSENewsItem
{
    char    company[64];
    char    headline[256];
    char    date[16];       // YYYY-MM-DD
};

// ---------------------------------------------------------------------------
// DSEFetcher class
// ---------------------------------------------------------------------------
class DSEFetcher
{
public:
    DSEFetcher();
    ~DSEFetcher();

    bool Init();
    void Shutdown();

    // Live prices — fills result vector; returns true on success
    bool FetchLive( std::vector<DSEQuote>& out );

    // Historical OHLCV for a symbol between startDate and endDate (YYYY-MM-DD)
    bool FetchHistorical( const std::string& symbol,
                          const std::string& startDate,
                          const std::string& endDate,
                          std::vector<DSEQuote>& out );

    // Market depth (order book) for a symbol
    bool FetchDepth( const std::string& symbol, DSEDepth& out );

    // Sector performance
    bool FetchSectorPerformance( std::vector<DSESectorEntry>& out );

    // Latest news
    bool FetchNews( std::vector<DSENewsItem>& out );

    // Return last HTTP error description
    std::string LastError() const { return m_lastError; }

private:
    // HTTP helpers
    bool        HttpGet( const std::wstring& host,
                         const std::wstring& path,
                         const std::wstring& query,
                         std::string& responseBody );

    bool        HttpPost( const std::wstring& host,
                          const std::wstring& path,
                          const std::string&  postBody,
                          std::string& responseBody );

    // HTML table parsers
    bool        ParseLiveTable   ( const std::string& html, std::vector<DSEQuote>& out );
    bool        ParseHistTable   ( const std::string& html, std::vector<DSEQuote>& out );
    bool        ParseDepthTable  ( const std::string& html, DSEDepth& out );
    bool        ParseSectorTable ( const std::string& html, std::vector<DSESectorEntry>& out );
    bool        ParseNewsTable   ( const std::string& html, std::vector<DSENewsItem>& out );

    // String helpers
    static std::string  StripTags   ( const std::string& html );
    static std::string  Trim        ( const std::string& s );
    static float        ParseFloat  ( const std::string& s );
    static int          ParseInt    ( const std::string& s );
    static bool         SplitDate   ( const std::string& s,
                                      int& y, int& m, int& d );

    // Extract text content of the Nth <td> in a <tr>
    static bool         ExtractCells( const std::string& row,
                                      std::vector<std::string>& cells );

    // Find all occurrences of tag content matching a class
    static bool         FindTableByClass( const std::string& html,
                                          const std::string& cls,
                                          std::string& tableHtml );

    HINTERNET   m_hSession  = nullptr;
    std::string m_lastError;

    // DSE endpoints
    static constexpr wchar_t DSE_HOST[]        = L"www.dsebd.org";
    static constexpr wchar_t DSE_ALT_HOST[]    = L"dsebd.org";
    static constexpr wchar_t PATH_LIVE[]       = L"/latest_share_price_scroll_l.php";
    static constexpr wchar_t PATH_HIST[]       = L"/day_end_archive.php";
    static constexpr wchar_t PATH_DEPTH[]      = L"/ajax_mkt_depth.php";
    static constexpr wchar_t PATH_SECTOR[]     = L"/sector_performance.php";
    static constexpr wchar_t PATH_NEWS[]       = L"/news.php";
};

#endif // _DSEFETCHER_H_
