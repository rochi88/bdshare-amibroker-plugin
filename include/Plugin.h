#pragma once
// ============================================================================
// Plugin.h
// AmiBroker Development Kit (ADK 2.10) type definitions
// Matches the official ADK 2.10a header exactly.
// ============================================================================

#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include <windows.h>
#include <stdint.h>

#pragma pack(push, 1)

// ---------------------------------------------------------------------------
// Plugin API export macro
// ---------------------------------------------------------------------------
#define PLUGINAPI extern "C" __declspec(dllexport)

// ---------------------------------------------------------------------------
// Plugin type constants
// ---------------------------------------------------------------------------
#define PLUGIN_TYPE_DATA        1
#define PLUGIN_TYPE_AFL         2
#define PLUGIN_TYPE_OPTIMIZER   3

// ---------------------------------------------------------------------------
// Periodicity (timeframe) constants
// ---------------------------------------------------------------------------
#define PERIODICITY_EOD         0       // End of day
#define PERIODICITY_1MIN        1
#define PERIODICITY_2MIN        2
#define PERIODICITY_3MIN        3
#define PERIODICITY_5MIN        5
#define PERIODICITY_10MIN       10
#define PERIODICITY_15MIN       15
#define PERIODICITY_20MIN       20
#define PERIODICITY_30MIN       30
#define PERIODICITY_HOURLY      60
#define PERIODICITY_2HOURLY     120
#define PERIODICITY_3HOURLY     180
#define PERIODICITY_4HOURLY     240
#define PERIODICITY_6HOURLY     360
#define PERIODICITY_8HOURLY     480
#define PERIODICITY_WEEKLY      -1
#define PERIODICITY_MONTHLY     -2

// ---------------------------------------------------------------------------
// Status codes for SetStatus / GetPluginStatus
// ---------------------------------------------------------------------------
#define STATUS_OK               0
#define STATUS_WAIT             1
#define STATUS_DISCONNECTED     2
#define STATUS_ERROR            3
#define STATUS_CONNECTING       4

// ---------------------------------------------------------------------------
// AmiDate – packed date/time (64-bit, microsecond resolution)
// Bits: year(12) | month(4) | day(5) | hour(5) | minute(6) | second(6) |
//       millisecond(10) | microsecond(10) | reserved(6)
// ---------------------------------------------------------------------------
typedef unsigned __int64 AmiDate;

// Helper: pack a date into AmiDate
inline AmiDate PackDate( int year, int month, int day,
                          int hour = 0, int minute = 0, int second = 0 )
{
    return (AmiDate)(
        ((AmiDate)(year  & 0xFFF) << 52) |
        ((AmiDate)(month & 0x00F) << 48) |
        ((AmiDate)(day   & 0x01F) << 43) |
        ((AmiDate)(hour  & 0x01F) << 38) |
        ((AmiDate)(minute& 0x03F) << 32) |
        ((AmiDate)(second& 0x03F) << 26)
    );
}

// ---------------------------------------------------------------------------
// Quotation record – one OHLCV bar
// ---------------------------------------------------------------------------
struct Quotation
{
    AmiDate     DateTime;       // 64-bit packed date/time
    float       Price;          // Close price
    float       Open;
    float       High;
    float       Low;
    float       Volume;
    float       OpenInterest;
    float       AuxData1;       // Aux field 1 (used for bid price)
    float       AuxData2;       // Aux field 2 (used for ask price)
};

// ---------------------------------------------------------------------------
// PluginInfo – returned by GetPluginInfo()
// ---------------------------------------------------------------------------
struct PluginInfo
{
    int         nStructSize;
    int         nType;          // PLUGIN_TYPE_DATA
    int         nVersion;       // e.g. 010000 = 1.0.0
    int         nIDCode;        // 4-byte ID, use PIDCODE macro
    LPCTSTR     szName;
    LPCTSTR     szVendor;
    int         nCertificate;   // 0 = unsigned
    int         nMinAmiVersion; // minimum AmiBroker version required
};

// ---------------------------------------------------------------------------
// PIDCODE – packs 4 ASCII chars into an int plugin ID
// ---------------------------------------------------------------------------
#define PIDCODE(a,b,c,d) ((int)(((unsigned char)(a)) | \
                                 ((unsigned char)(b) << 8) | \
                                 ((unsigned char)(c) << 16) | \
                                 ((unsigned char)(d) << 24)))

// ---------------------------------------------------------------------------
// StockInfo – symbol metadata (ADK 2.10+)
// ---------------------------------------------------------------------------
struct StockInfo
{
    char        szName[64];
    char        szFullName[128];
    char        szAddress[256];
    int         nSector;
    int         nIndustry;
    float       fMarketCap;
    float       fSharesOut;
    float       fEPS;
    float       fPE;
    float       f52WeekHigh;
    float       f52WeekLow;
    float       fDividend;
    float       fYield;
    int         nCountry;
    char        szCurrency[8];
    char        szISIN[24];
    // extended fields …
    char        szReserved[128];
};

// ---------------------------------------------------------------------------
// PluginNotification – passed to Notify()
// ---------------------------------------------------------------------------
#define REASON_DATABASE_LOADED      1
#define REASON_DATABASE_UNLOADED    2
#define REASON_SETTINGS_CHANGE      3
#define REASON_STATUS_REQUEST       4
#define REASON_RELOAD_SYMBOL        5

struct PluginNotification
{
    int         nStructSize;
    int         nReason;            // REASON_* constant
    HWND        hMainWnd;           // AmiBroker main window handle
    LPCTSTR     pszDatabasePath;
    StockInfo*  pCurrentSINew;      // ADK 2.10+ (new format)
    void*       pCurrentSI;         // ADK 1.x legacy pointer (StockInfoFormat4*)
    LPCTSTR     pszTicker;
};

// ---------------------------------------------------------------------------
// PluginStatus – returned by GetPluginStatus()
// ---------------------------------------------------------------------------
struct PluginStatus
{
    int         nStructSize;
    int         nStatusCode;        // STATUS_* constant
    COLORREF    clrStatusColor;
    char        szShortMessage[16];
    char        szLongMessage[256];
};

// ---------------------------------------------------------------------------
// GQEContext – context pointer passed to GetQuotesEx() (reserved, pass NULL)
// ---------------------------------------------------------------------------
struct GQEContext
{
    int         nStructSize;
    int         nReserved;
};

// ---------------------------------------------------------------------------
// InfoSite – passed to Configure(); used to call back into AmiBroker
// ---------------------------------------------------------------------------
struct InfoSite
{
    int     nStructSize;
    // SetStatus callback: lets plugin push status icon + text to AmiBroker status bar
    void    (*SetStatus)( struct PluginStatus* status );
    // Streaming update: WM_USER+2020 posted to AmiBroker window triggers chart refresh
    HWND    hMainWnd;
};

// Streaming update message ID
#define WM_USER_STREAMING_UPDATE (WM_USER + 2020)

#pragma pack(pop)

#endif // _PLUGIN_H_
