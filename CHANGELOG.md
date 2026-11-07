# Change log

All notable changes to the **BDShare DSE AmiBroker plugin** are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 20261107

DSE retired the pages this plugin read: `www.dsebd.org` and `dsebd.org` now answer `410 Gone`, so version 1.1.2 receives no data. This release restores the feed and fixes problems found along the way, following what the [bdshare](https://github.com/rochi88/bdshare) Python package learned about the DSE site.

### Changed
- All data now comes from the legacy site, `old.dsebd.org`. The current site, `dse.com.bd`, refuses declared automated clients (see "Data source" in the README), so the plugin does not use it.
- The plugin identifies itself as `BDShare-AmiBroker/<version>` rather than as a browser.
- Live prices are polled every 15 seconds during the session (was 5). Outside the session the plugin fetches prices only once, then just re-checks the market status.
- Daily history is cached per symbol for 30 minutes, and market depth for 30 seconds. Previously every chart refresh made a network request on AmiBroker's UI thread.
- `NewsCount` now counts the current symbol's items in DSE's 7-day news feed, rather than the size of the whole feed. News is refreshed every 10 minutes in session and hourly otherwise.
- Status bar shows `CLSD` when the market is closed.
- Failed requests are retried up to 3 times with exponential back-off, except for 4xx responses and certificate failures. Repeated live fetch failures back off to every 2 minutes.
- Times and dates use Dhaka time (UTC+6) rather than the PC's clock.
- The debug log moved from a hard-coded `d:\software\` path to `%TEMP%\bdshare_plugin_debug.log`, and is written only when the `BDSHARE_DEBUG` environment variable is set.
- `scripts/BDShare_DSE_Dashboard.afl` rewritten: it called `GetExtraData` with five arguments, and used `AuxData()`, `HorizLine()` and an `AddTextColumn` chart column, none of which work in a chart formula as written.

### Added
- `GetExtraData("MarketOpen")`: 1 during a session, otherwise 0.
- `GetExtraData("NewsHeadline")`: the latest headline for the current symbol.
- Unit tests that run on any platform against saved DSE pages (`ctest`), a GitHub Actions CI workflow, and `tests/check_endpoints.py` to see which DSE page changed when the feed breaks.

### Removed
- `GetExtraData("SectorChg")`: DSE removed the sector performance page (it now answers 404), and the old value was the first sector's change for every symbol anyway.
- `test_fetch.py` and `test_get_form.py`: one-off debug scripts for the retired site.

### Fixed
- Daily charts lost their newest bars when DSE returned more rows than AmiBroker's array holds, because the oldest rows were the ones kept.
- Bars older than DSE's two-year limit that were already in a local database were overwritten by each update. They are now kept.
- Untraded days were stored with open, high and low of 0, drawing spikes down to zero. They are now flat bars at the close.
- Daily databases now show the running session's bar until the day-end archive has it.
- Intraday bars used the whole day's high, low and cumulative volume, and a new bar was added on every call. Bars are now built per period, using each bar's own range and the volume traded within it. Nothing is added outside a session.
- Market depth was parsed with the wrong layout and from a retired endpoint, and could not represent a book with different numbers of bids and asks. A side with no orders is now Null in AFL rather than 0.
- `PLUGIN_VERSION` was written `010102`, an octal literal, so AmiBroker saw version 4162. The version now comes from `CMakeLists.txt`.
- Symbols containing URL characters, such as `AMCL(PRAN)`, were sent unencoded.
- Startup and shutdown work moved out of `DllMain`, where joining a thread and using WinHTTP under the loader lock could hang AmiBroker. It now happens in `Init()` and `Release()`.
- `GetExtraData` held the cache lock during network calls, blocking the streaming thread and other callers.
- Data races on the last-error and status strings when the streaming thread and AmiBroker's UI thread ran together.
- A truncated response is now an error rather than being parsed as a complete page.
