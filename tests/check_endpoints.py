#!/usr/bin/env python3
"""Check that the legacy DSE pages the plugin reads still have the same layout.

Uses the standard library only and sends the plugin's own User-Agent. Run it
when the feed stops working, to see which page changed:

    python tests/check_endpoints.py

old.dsebd.org sends an incomplete certificate chain. Windows normally completes
it automatically; on other systems pass a CA bundle containing the Sectigo DV R36
intermediate (bdshare ships one as bdshare/util/sectigo_dv_r36.pem):

    python tests/check_endpoints.py --cafile bundle.pem
"""

import argparse
import datetime
import ssl
import sys
import urllib.parse
import urllib.request

BASE = "https://old.dsebd.org/"
USER_AGENT = (
    "BDShare-AmiBroker/1.2 (+https://github.com/rochi88/bdshare-amibroker-plugin)"
)


def fetch(path, context, data=None, headers=None):
    request = urllib.request.Request(
        BASE + path,
        data=urllib.parse.urlencode(data).encode() if data else None,
        headers={"User-Agent": USER_AGENT, **(headers or {})},
    )
    with urllib.request.urlopen(request, timeout=30, context=context) as response:
        return response.read().decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cafile", help="CA bundle to verify the server against")
    args = parser.parse_args()
    context = ssl.create_default_context(cafile=args.cafile)

    today = datetime.date.today()
    archive = (
        f"day_end_archive.php?startDate={today - datetime.timedelta(days=30)}"
        f"&endDate={today}&inst=GP&archive=data"
    )
    depth_headers = {
        "X-Requested-With": "XMLHttpRequest",
        "Referer": BASE + "mkt_depth_3.php",
    }
    checks = [
        ("live prices", "latest_share_price_scroll_l.php", None, None,
         ["shares-table", "TRADING CODE"]),
        ("day-end archive", archive, None, None, ["shares-table", "OPENP"]),
        ("market depth", "ajax/load-instrument.php", {"inst": "GP"}, depth_headers,
         ["Buy Volume", "Sell Price", "Sell Volume"]),
        ("news", "ajax/load-news.php", {"criteria": 2},
         {"X-Requested-With": "XMLHttpRequest",
          "Referer": BASE + "news_archive_7days.php"},
         ["table-news", "Trading Code:", "Post Date:"]),
        ("market status", "", None, None, ["Market Status:"]),
    ]

    failures = 0
    for name, path, data, headers, markers in checks:
        try:
            body = fetch(path, context, data, headers)
            missing = [m for m in markers if m not in body]
            result = f"missing {missing}" if missing else "ok"
        except OSError as exc:  # includes URLError and SSL errors
            result = f"failed: {exc}"
        failures += result != "ok"
        print(f"{name:16} {result}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
