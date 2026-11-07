# BDShare DSE AmiBroker Data Plugin

A native C++ AmiBroker data feed plugin (ADK 2.10) that brings live and
historical data from the Dhaka Stock Exchange (DSE) into AmiBroker.

## Features

| Feature | Detail |
|---|---|
| Live prices | High, low, last price, change, value, trade count. Polled every 15 s during the session |
| Historical OHLCV | Last 2 years of daily data per symbol, cached for 30 minutes |
| Market depth | Best bid/ask price and volume via `GetExtraData()`, cached for 30 s |
| News | Last 7 days of DSE items per symbol, refreshed every 10 minutes in session |
| Market status | Open or closed, in the status bar and via `GetExtraData()` |
| Transport | WinHTTP (no extra DLL dependencies) |
| Architecture | x86 or x64. Must match your AmiBroker install |

See [CHANGELOG.md](CHANGELOG.md) for what changed in each version.

---

## Data source

All data comes from DSE's legacy website, `old.dsebd.org`, using the same pages
as the [bdshare](https://github.com/rochi88/bdshare) Python package. DSE offers
no official API or WebSocket feed, so the plugin reads HTML tables.

**The plugin does not use the current site, `dse.com.bd`.** It answers declared
automated clients with `403 Forbidden: automated access to this site is not
permitted`. It responds differently to browser User-Agents, but pretending to be
a browser would get around DSE's stated rule, so the plugin identifies itself
honestly as `BDShare-AmiBroker/<version>`. Switching to the current site's data
would need DSE's permission.

The legacy site could be retired without notice, as the older `dsebd.org` pages
were. If the feed stops, run `python tests/check_endpoints.py` to see which page
changed.

Please keep polling modest. Every running copy of the plugin adds load to a public
service; the intervals are constants at the top of `src/Plugin.cpp`.

---

## Requirements

- **Visual Studio 2019 / 2022** (MSVC toolchain)
- **CMake 3.16+**
- **AmiBroker 5.30+** (ADK 2.10 `GetQuotesEx` API)
- Windows 7 or later (WinHTTP ships with Windows)
- Internet access to `old.dsebd.org`

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

### Tests

The parsers and bar logic are platform-neutral and tested against real DSE pages
saved in `tests/fixtures/`. On Windows, after a build above:

```cmd
ctest --test-dir build64 -C Release --output-on-failure
```

Other platforms build only the tests:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

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
5. Click **Configure**. This imports the DSE symbols and shows how many were added.

Use an **end-of-day** database for history. DSE publishes no intraday history, so
an intraday database starts empty and builds bars only while AmiBroker is running
during a session.

---

## Adding symbols

**Configure** imports every instrument on DSE's latest share price page. To add
one by hand, set its **Name** (the ticker field in AmiBroker) to its DSE trading
code exactly, e.g.:

| DSE code | Name in AmiBroker |
|---|---|
| `GP` | `GP` |
| `ACI` | `ACI` |
| `BEXIMCO` | `BEXIMCO` |

---

## AFL usage

Copy `scripts/BDShare_DSE_Dashboard.afl` to your AmiBroker `Formulas\`
folder and apply it to any chart. The included formula demonstrates:

- Candlestick chart coloured by daily change, with volume
- Best bid / ask reference lines (from `GetExtraData`)
- MA(20), MA(50), EMA(10) overlays
- RSI(14) on its own scale
- Traded value, trade count, market status and the latest headline in the title

### GetExtraData reference

All fields apply to the current symbol.

```afl
BidPrice = GetExtraData( "BidPrice" );      // array; Null if no bids
BidVol   = GetExtraData( "BidVol" );        // array; Null if no bids
AskPrice = GetExtraData( "AskPrice" );      // array; Null if no asks
AskVol   = GetExtraData( "AskVol" );        // array; Null if no asks

MarketOpen = GetExtraData( "MarketOpen" );    // 1 during a session, otherwise 0
NewsCount  = GetExtraData( "NewsCount" );     // this symbol's items in the feed
Headline   = GetExtraData( "NewsHeadline" );  // string: "YYYY-MM-DD: title"
```

### Built-in fields populated by the plugin

| AmiBroker field | Intraday database | End-of-day database |
|---|---|---|
| `O` | First price in the bar | Daily open |
| `H` | Highest price seen in the bar | Daily high |
| `L` | Lowest price seen in the bar | Daily low |
| `C` | Last traded price | Daily close |
| `V` | Volume traded in the bar | Volume |
| `Aux1` | Yesterday's close (YCP) | Traded value (million BDT) |
| `Aux2` | Price change (Δ) | Trade count |

---

## Status bar

The plugin paints a coloured icon in AmiBroker's status bar:

| Colour | Text | Meaning |
|---|---|---|
| 🟢 Green | `LIVE` | Market open, data OK |
| 🔘 Blue-grey | `CLSD` | Market closed, data OK |
| 🟠 Orange | `WAIT` | Fetch failed or market status unknown, retrying |
| 🔵 Blue | `CONN` | Connecting |
| 🔴 Red | `ERR!` | Could not start the HTTP client |
| ⚫ Grey | `----` | Disconnected |

For diagnostics, set the `BDSHARE_DEBUG` environment variable to log to
`%TEMP%\bdshare_plugin_debug.log`. Messages also go to the Windows debug output.

---

## Architecture notes

```
AmiBroker
  │
  ├─ Init() / Release()   ← DLL load / unload: HTTP client, stop the thread
  ├─ GetPluginInfo()      ← DLL identification at load time
  ├─ Configure()          ← User selects plugin; imports symbols
  ├─ Notify()             ← Database load/unload: start/stop the thread
  ├─ GetPluginStatus()    ← Status bar icon refresh
  ├─ GetQuotesEx()        ← Called per symbol when AB needs data
  │    ├─ EOD path        → history cache (fetches when stale) + live session bar
  │    └─ Intraday path   → g_liveCache folded into the current bar
  └─ GetExtraData()       ← Depth cache (fetches when stale) / status / news

Background thread (StreamingThread)
  └─ every 1 min around the session, 15 min otherwise: FetchMarketStatus()
  └─ every 15 s in session: FetchLive()
       → update g_liveCache
       → PostMessage(WM_USER_STREAMING_UPDATE) → AB calls GetQuotesEx() again
     outside the session: once at startup and once after the close
  └─ every 10 min in session, 60 min otherwise: FetchNews()
```

---

## Known limitations

- DSE has no official API; all data is scraped from HTML tables. If DSE changes
  a page layout the parsers will need updating.
- History is limited to the last 2 years, DSE's limit on both of its sites. Older
  bars already stored in a local AmiBroker database are kept.
- The live page has no open price. Today's bar in an end-of-day database uses
  yesterday's close (clamped to the day's range) as the open until the day-end
  archive has the official row.
- An intraday bar's volume is worked out from DSE's cumulative daily figure, so
  the first bar after starting mid-session holds all the volume traded so far.
- `GetExtraData()` depth values are the same value for every bar (the latest
  book), so use the last value.
- A first request for a symbol's history blocks AmiBroker briefly on its UI
  thread, and an outage can stall it for up to about a minute before the failure
  is remembered. Later calls use the cache.

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
├── CMakeLists.txt                  # Builds the DLL (Windows) and the unit tests
├── CHANGELOG.md
├── README.md
│
├── include/
│   ├── Plugin.h                    # ADK 2.10 types and entry points
│   ├── DSEParsers.h                # Data types, page parsers, Dhaka time helpers
│   ├── BarMerge.h                  # Writing data into AmiBroker's quote array
│   └── DSEFetcher.h                # WinHTTP client for old.dsebd.org
│
├── src/
│   ├── Plugin.cpp                  # ADK entry points, caches, streaming thread
│   ├── DSEFetcher.cpp              # HTTP requests, retries and back-off
│   ├── DSEParsers.cpp              # HTML parsing (platform-neutral)
│   └── Plugin.def                  # DLL export map
│
├── scripts/
│   └── BDShare_DSE_Dashboard.afl   # Ready-to-use chart formula
│
└── tests/
    ├── test_main.cpp               # Unit tests (run with ctest)
    ├── check_endpoints.py          # Checks the live DSE pages still match
    ├── fixtures/                   # Real DSE pages, trimmed
    └── stub/windows.h              # Lets Plugin.h compile on non-Windows
```

---

## License

MIT — see `LICENSE` file.
