# BDShare DSE AmiBroker Data Plugin

A native C++ AmiBroker data feed plugin (ADK 2.10) that streams live and
historical data from the Dhaka Stock Exchange (DSE) directly into AmiBroker.

## Features

| Feature | Detail |
|---|---|
| Live prices | OHLCV + change, value, trade count — polled every ~5 s |
| Historical OHLCV | Up to 2 years of EOD data fetched on demand per symbol |
| Market depth | Best bid/ask price and volume via `GetExtraData()` |
| Sector performance | Sector change % refreshed every ~60 s |
| News | Latest DSE headlines refreshed every ~60 s |
| Transport | WinHTTP (no extra DLL dependencies) |
| Architecture | x86 or x64 — must match your AmiBroker install |

---

## Requirements

- **Visual Studio 2019 / 2022** (MSVC toolchain)
- **CMake 3.16+**
- **AmiBroker 5.27+** (ADK 2.10 `GetQuotesEx` API)
- Windows 7 or later (WinHTTP ships with Windows)
- Internet access to `www.dsebd.org`

---

## Build

```cmd
git clone https://github.com/rochi88/bdshare-amibroker-plugin
cd bdshare-amibroker-plugin

:: 32-bit build (matches standard AmiBroker install)
cmake -B build32 -A Win32
cmake --build build32 --config Release

:: 64-bit build (matches AmiBroker 64-bit edition)
cmake -B build64 -A x64
cmake --build build64 --config Release
```

The output DLL is at `build32\Release\BDShare_DSE.dll` (or `build64\...`).

### Auto-deploy

Set the `AMIBROKER_PATH` environment variable before running CMake and the
DLL will be copied to `%AMIBROKER_PATH%\Plugins\` automatically after each
build:

```cmd
set AMIBROKER_PATH=C:\Program Files\AmiBroker
cmake -B build32 -A Win32
cmake --build build32 --config Release
```

---

## Installation (manual)

1. Copy `BDShare_DSE.dll` to `C:\Program Files\AmiBroker\Plugins\`.
2. Start AmiBroker.
3. Go to **File → Database Settings**.
4. Set **Data source** to `BDShare DSE Data Feed`.
5. Click **Configure** — a confirmation dialog will appear and the
   streaming thread will start.

---

## Adding symbols

Each symbol's **Name** (the ticker field in AmiBroker) must match its DSE
trading code exactly, e.g.:

| DSE code | Name in AmiBroker |
|---|---|
| `GP` | `GP` |
| `ACI` | `ACI` |
| `BEXIMCO` | `BEXIMCO` |

AmiBroker will call `GetQuotesEx("GP", ...)` and the plugin maps that string
to the DSE scraper.

---

## AFL usage

Copy `scripts/BDShare_DSE_Dashboard.afl` to your AmiBroker `Formulas\`
folder and apply it to any chart. The included formula demonstrates:

- Candlestick chart coloured by daily change
- Best bid / ask horizontal reference lines (from `GetExtraData`)
- Yesterday's close reference line
- MA(20), MA(50), EMA(10) overlays
- RSI(14) sub-panel
- News count and sector change display

### GetExtraData reference

```afl
// Per-symbol depth
BidPrice = GetExtraData( Name(), "BidPrice", BarCount, 1, 1 );
BidVol   = GetExtraData( Name(), "BidVol",   BarCount, 1, 1 );
AskPrice = GetExtraData( Name(), "AskPrice", BarCount, 1, 1 );
AskVol   = GetExtraData( Name(), "AskVol",   BarCount, 1, 1 );

// Global
SectorChg  = GetExtraData( "", "SectorChg",  BarCount, 1, 1 );
NewsCount  = GetExtraData( "", "NewsCount",  BarCount, 1, 1 );
```

### Built-in fields populated by the plugin

| AmiBroker field | Live (intraday) | EOD |
|---|---|---|
| `O` | Last close (DSE doesn't expose live open) | Daily open |
| `H` | Day high | Daily high |
| `L` | Day low | Daily low |
| `C` | Last traded price | Daily close |
| `V` | Volume | Volume |
| `AuxData()` | Yesterday's close (YCP) | Traded value |
| `AuxData2()` | Price change (Δ) | Trade count |

---

## Status bar

The plugin paints a coloured icon in AmiBroker's status bar:

| Colour | Meaning |
|---|---|
| 🟢 Green | Live data — OK |
| 🟠 Orange | Waiting / retrying |
| 🔵 Blue | Connecting |
| 🔴 Red | Error |
| ⚫ Grey | Disconnected |

---

## Architecture notes

```
AmiBroker
  │
  ├─ GetPluginInfo()      ← DLL identification at load time
  ├─ Configure()          ← User selects plugin; streaming thread starts
  ├─ Notify()             ← Database load/unload events
  ├─ GetPluginStatus()    ← Status bar icon refresh
  ├─ GetQuotesEx()        ← Called per symbol when AB needs data
  │    ├─ EOD path        → DSEFetcher::FetchHistorical() (synchronous)
  │    └─ RT path         → g_liveCache lookup (lock-free read)
  └─ GetExtraData()       ← Depth / sector / news arrays
       └─ DSEFetcher::FetchDepth()  (per-call, synchronous)

Background thread (StreamingThread)
  └─ every 5 s:  DSEFetcher::FetchLive()
       → update g_liveCache
       → PostMessage(WM_USER_STREAMING_UPDATE) → AB calls GetQuotesEx() again
  └─ every 60 s: DSEFetcher::FetchSectorPerformance()
                 DSEFetcher::FetchNews()
```

---

## Known limitations

- DSE does not provide a WebSocket or official API; all data is scraped from
  HTML tables. If DSE changes its page layout the parsers will need updating.
- Live "open" price is approximated as the previous close because DSE's live
  table does not include a separate open column.
- The plugin polls at ~5 s intervals. True tick-by-tick data is not available
  from the public DSE website.
- AmiBroker's `GetExtraData()` returns array fills (same value repeated for
  all bars) for depth data — this is expected and correct for real-time use.

---

## Plugin ID

The plugin uses ID code `BDSE`. Contact AmiBroker
(`[email protected]`) before public release to reserve a unique ID.
For testing, `BDSE` is fine.

---

## Repository Structure

```
bdshare-amibroker-plugin/
│
├── CMakeLists.txt                  # Build system — generates MSVC project (x86/x64)
├── README.md                       # Build, install, and AFL usage guide
│
├── include/                        # Header files
│   ├── Plugin.h                    # ADK 2.10 types: Quotation, PluginInfo,
│   │                               #   AmiDate, PackDate, PluginNotification,
│   │                               #   PluginStatus, InfoSite, GQEContext
│   └── DSEFetcher.h                # HTTP fetcher interface: DSEQuote, DSEDepth,
│                                   #   DSESectorEntry, DSENewsItem, DSEFetcher
│
├── src/                            # Implementation files
│   ├── Plugin.cpp                  # All 6 ADK entry points:
│   │                               #   GetPluginInfo()     — DLL identity
│   │                               #   GetPluginStatus()   — status bar icon
│   │                               #   Configure()         — start stream thread
│   │                               #   Notify()            — DB load/unload events
│   │                               #   GetQuotesEx()       — OHLCV data pump
│   │                               #   GetExtraData()      — depth/sector/news
│   │
│   ├── DSEFetcher.cpp              # WinHTTP network layer + HTML table parsers:
│   │                               #   FetchLive()         — current prices
│   │                               #   FetchHistorical()   — EOD OHLCV
│   │                               #   FetchDepth()        — order book
│   │                               #   FetchSectorPerformance()
│   │                               #   FetchNews()
│   │
│   └── Plugin.def                  # DLL export map — prevents MSVC name mangling
│
└── scripts/                        # AFL formulas
    └── BDShare_DSE_Dashboard.afl   # Ready-to-use chart formula:
                                    #   candlesticks, bid/ask lines, YCP,
                                    #   MA(20/50), EMA(10), RSI(14),
                                    #   GetExtraData() depth/news examples
```
---

## License

MIT — see `LICENSE` file.
