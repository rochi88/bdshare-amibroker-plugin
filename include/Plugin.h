////////////////////////////////////////////////////
// Plugin.h
// Standard header file for all AmiBroker plug-ins
// Official ADK 2.1a — unmodified except for additions
// marked with "// BDSHARE ADDITION"
////////////////////////////////////////////////////

#ifndef PLUGIN_H
#define PLUGIN_H 1

// Must come first — defines BOOL, HWND, COLORREF, LPCTSTR, etc.
// The include guard above prevents double-inclusion problems.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define PLUGIN_TYPE_AFL             1
#define PLUGIN_TYPE_DATA            2
#define PLUGIN_TYPE_AFL_AND_DATA    3
#define PLUGIN_TYPE_OPTIMIZER       4

#ifdef _USRDLL
#define PLUGINAPI extern "C" __declspec(dllexport)
#else
#define PLUGINAPI extern "C" __declspec(dllimport)
#endif

#define DATE_TIME_INT unsigned __int64

typedef unsigned char UBYTE;
typedef signed char   SBYTE;

#define EMPTY_VAL    (-1e10f)
#define IS_EMPTY(x)  ( (x) == EMPTY_VAL )
#define NOT_EMPTY(x) ( (x) != EMPTY_VAL )

// Official PIDCODE — high byte first: PIDCODE('B','D','S','E')
#define PIDCODE(a,b,c,d) ( ((a)<<24) | ((b)<<16) | ((c)<<8) | (d) )

struct PluginInfo {
    int  nStructSize;
    int  nType;
    int  nVersion;
    int  nIDCode;
    char szName[64];      // ← char[], NOT LPCTSTR
    char szVendor[64];    // ← char[], NOT LPCTSTR
    int  nCertificate;
    int  nMinAmiVersion;
};

// AmiVar types
enum { VAR_NONE, VAR_FLOAT, VAR_ARRAY, VAR_STRING, VAR_DISP };

#pragma pack(push, 2)
typedef struct AmiVar {
    int type;
    union {
        float  val;
        float *array;
        char  *string;
        void  *disp;
    };
} AmiVar;
#pragma pack(pop)

// Common required exports
PLUGINAPI int GetPluginInfo( struct PluginInfo *pInfo );
PLUGINAPI int Init( void );
PLUGINAPI int Release( void );

// AFL plugin structures (not used here but must not conflict)
struct SiteInterface {
    int         nStructSize;
    int         (*GetArraySize)(void);
    float *     (*GetStockArray)(int nType);
    AmiVar      (*GetVariable)(const char *pszName);
    void        (*SetVariable)(const char *pszName, AmiVar newValue);
    AmiVar      (*CallFunction)(const char *szName, int nNumArgs, AmiVar *ArgsTable);
    AmiVar      (*AllocArrayResult)(void);
    void *      (*Alloc)(unsigned int nSize);
    void        (*Free)(void *pMemory);
    DATE_TIME_INT* (*GetDateTimeArray)(void);
};

typedef struct FunDesc {
    AmiVar (*Function)(int NumArgs, AmiVar *ArgsTable);
    UBYTE   ArrayQty;
    UBYTE   StringQty;
    SBYTE   FloatQty;
    UBYTE   DefaultQty;
    float  *DefaultValues;
} FunDesc;

typedef struct FunctionTag {
    char   *Name;
    FunDesc Descript;
} FunctionTag;

PLUGINAPI int GetFunctionTable( FunctionTag **ppFunctionTable );
PLUGINAPI int SetSiteInterface( struct SiteInterface *pInterface );

extern FunctionTag gFunctionTable[];
extern int         gFunctionTableSize;
extern struct SiteInterface gSite;

// ── Data plugin constants ────────────────────────────────────────────────────

#define DATE_EOD_TICKS   15
#define DATE_EOD_MINUTES 63
#define DATE_EOD_HOURS   31

// Official periodicity values (seconds)
#define PERIODICITY_1SEC   1
#define PERIODICITY_1MIN   60
#define PERIODICITY_1HOUR  3600
#define PERIODICITY_EOD    86400    // ← 86400, NOT 0

// Notify reasons
#define REASON_DATABASE_LOADED    1
#define REASON_DATABASE_UNLOADED  2
#define REASON_SETTINGS_CHANGE    4
#define REASON_STATUS_RMBCLICK    8

// Official streaming update message — WM_USER+13000, NOT WM_USER+2020
#define WM_USER_STREAMING_UPDATE  (WM_USER + 13000)

// ── AmiDate (64-bit packed date/time) ────────────────────────────────────────

struct PackedDate {
    // lower 32 bits
    unsigned int IsFuturePad : 1;
    unsigned int Reserved    : 5;
    unsigned int MicroSec    : 10;  // 0..999
    unsigned int MilliSec    : 10;  // 0..999
    unsigned int Second      : 6;   // 0..59
    // upper 32 bits
    unsigned int Minute      : 6;   // 0..59
    unsigned int Hour        : 5;   // 0..23
    unsigned int Day         : 5;   // 1..31
    unsigned int Month       : 4;   // 1..12
    unsigned int Year        : 12;  // 0..4095
};

union AmiDate {
    DATE_TIME_INT  Date;
    struct PackedDate PackDate;
};

// Helper to fill an AmiDate from y/m/d h:m:s  — BDSHARE ADDITION
inline AmiDate MakeAmiDate( int year, int month, int day,
                              int hour = DATE_EOD_HOURS,
                              int minute = DATE_EOD_MINUTES,
                              int second = 0 )
{
    AmiDate d = {};
    d.PackDate.Year   = (unsigned)year;
    d.PackDate.Month  = (unsigned)month;
    d.PackDate.Day    = (unsigned)day;
    d.PackDate.Hour   = (unsigned)hour;
    d.PackDate.Minute = (unsigned)minute;
    d.PackDate.Second = (unsigned)second;
    return d;
}

// ── Quotation (8-byte aligned, 40 bytes) ─────────────────────────────────────

struct Quotation {
    union AmiDate DateTime;  // 8 bytes
    float Price;
    float Open;
    float High;
    float Low;
    float Volume;
    float OpenInterest;
    float AuxData1;
    float AuxData2;
};

// Legacy pre-5.27 structures (forward-declare; full defs in plugin_legacy.h
// which we don't ship — forward declare enough for PluginNotification)
struct QuotationFormat4;
struct StockInfoFormat4;

// ── InfoSite — NO hMainWnd field in the official header ──────────────────────
enum { CATEGORY_MARKET, CATEGORY_GROUP, CATEGORY_SECTOR,
       CATEGORY_INDUSTRY, CATEGORY_WATCHLIST };

struct InfoSite {
    int    nStructSize;
    int    (*GetStockQty)(void);
    struct StockInfoFormat4 * (*AddStock)(const char *pszTicker);
    int    (*SetCategoryName)(int nCategory, int nItem, const char *pszName);
    const char * (*GetCategoryName)(int nCategory, int nItem);
    int    (*SetIndustrySector)(int nIndustry, int nSector);
    int    (*GetIndustrySector)(int nIndustry);
    struct StockInfo * (*AddStockNew)(const char *pszTicker);
};

// RecentInfo / RI_ flags
#define RI_LAST         (1L<<0)
#define RI_OPEN         (1L<<1)
#define RI_HIGHLOW      (1L<<2)
#define RI_TRADEVOL     (1L<<3)
#define RI_TOTALVOL     (1L<<4)
#define RI_OPENINT      (1L<<5)
#define RI_PREVCHANGE   (1L<<6)
#define RI_BID          (1L<<7)
#define RI_ASK          (1L<<8)

#define RI_STATUS_UPDATE     (1L<<0)
#define RI_STATUS_BIDASK     (1L<<1)
#define RI_STATUS_TRADE      (1L<<2)
#define RI_STATUS_BARSREADY  (1L<<3)
#define RI_STATUS_INCOMPLETE (1L<<4)

struct RecentInfo {
    int   nStructSize;
    char  Name[64];
    char  Exchange[8];
    int   nStatus;
    int   nBitmap;
    float fOpen;
    float fHigh;
    float fLow;
    float fLast;
    int   iTradeVol;
    int   iTotalVol;
    float fOpenInt;
    float fChange;
    float fPrev;
    float fBid;
    int   iBidSize;
    float fAsk;
    int   iAskSize;
    float fEPS;
    float fDividend;
    float fDivYield;
    int   nShares;
    float f52WeekHigh;
    int   n52WeekHighDate;
    float f52WeekLow;
    int   n52WeekLowDate;
    int   nDateChange;
    int   nTimeChange;
    int   nDateUpdate;
    int   nTimeUpdate;
    float fTradeVol;
    float fTotalVol;
};

struct GQEContext {
    int nStructSize;
};

struct PluginStatus {
    int      nStructSize;
    int      nStatusCode;
    COLORREF clrStatusColor;
    char     szLongMessage[256];
    char     szShortMessage[32];  // ← 32 bytes in official header
};

// ── Status codes — BDSHARE ADDITION (not in official header) ─────────────────
#define STATUS_OK           0
#define STATUS_WAIT         1
#define STATUS_DISCONNECTED 2
#define STATUS_ERROR        3
#define STATUS_CONNECTING   4

// ── StockInfo ─────────────────────────────────────────────────────────────────
#define MAX_SYMBOL_LEN 48

struct StockInfo {
    char  ShortName[MAX_SYMBOL_LEN];
    char  AliasName[MAX_SYMBOL_LEN];
    char  WebID[MAX_SYMBOL_LEN];
    char  FullName[128];
    char  Address[128];
    char  Country[64];
    char  Currency[4];
    int   DataSource;
    int   DataLocalMode;
    int   MarketID;
    int   GroupID;
    int   IndustryID;
    int   GICS;
    int   Flags;
    int   MoreFlags;
    float MarginDeposit;
    float PointValue;
    float RoundLotSize;
    float TickSize;
    int   Decimals;
    short LastSplitFactor[2];
    DATE_TIME_INT LastSplitDate;
    DATE_TIME_INT DividendPayDate;
    DATE_TIME_INT ExDividendDate;
    float SharesFloat;
    float SharesOut;
    float DividendPerShare;
    float BookValuePerShare;
    float PEGRatio;
    float ProfitMargin;
    float OperatingMargin;
    float OneYearTargetPrice;
    float ReturnOnAssets;
    float ReturnOnEquity;
    float QtrlyRevenueGrowth;
    float GrossProfitPerShare;
    float SalesPerShare;
    float EBITDAPerShare;
    float QtrlyEarningsGrowth;
    float InsiderHoldPercent;
    float InstitutionHoldPercent;
    float SharesShort;
    float SharesShortPrevMonth;
    float ForwardEPS;
    float EPS;
    float EPSEstCurrentYear;
    float EPSEstNextYear;
    float EPSEstNextQuarter;
    float ForwardDividendPerShare;
    float Beta;
    float OperatingCashFlow;
    float LeveredFreeCashFlow;
    float ReservedInternal[28];
    float UserData[100];
};

struct PluginNotification {
    int     nStructSize;
    int     nReason;
    LPCTSTR pszDatabasePath;
    HWND    hMainWnd;           // ← AmiBroker main window IS here, in PluginNotification
    struct  StockInfoFormat4  *pCurrentSI;
    struct  _Workspace        *pWorkspace;
    struct  StockInfo         *pCurrentSINew;
};

// Data plugin required/optional exports
PLUGINAPI int GetQuotes( LPCTSTR pszTicker, int nPeriodicity, int nLastValid,
                          int nSize, struct QuotationFormat4 *pQuotes );
PLUGINAPI int GetQuotesEx( LPCTSTR pszTicker, int nPeriodicity, int nLastValid,
                            int nSize, struct Quotation *pQuotes, GQEContext *pContext );
PLUGINAPI AmiVar GetExtraData( LPCTSTR pszTicker, LPCTSTR pszName,
                                int nArraySize, int nPeriodicity,
                                void* (*pfAlloc)(unsigned int nSize) );
PLUGINAPI int Configure( LPCTSTR pszPath, struct InfoSite *pSite );
PLUGINAPI struct RecentInfo * GetRecentInfo( LPCTSTR pszTicker );
PLUGINAPI BOOL IsBackfillComplete( LPCTSTR pszTicker );
PLUGINAPI int GetSymbolLimit( void );
PLUGINAPI int GetPluginStatus( struct PluginStatus *status );
PLUGINAPI int SetDatabasePath( LPCTSTR pszPath );
PLUGINAPI int SetTimeBase( int nTimeBase );
PLUGINAPI int Notify( struct PluginNotification *pNotifyData );

#endif // PLUGIN_H