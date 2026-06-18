#!/usr/bin/env python3
"""Generate src/stations.h — the bundled offline station list.

Fetches the public GB National Rail (gb-nr) station list from the Realtime
Trains next-generation API (/data/stops) and emits a sorted C array of
(name, CRS) pairs. Bundling the list lets InkStation resolve a typed station
name to a CRS code entirely offline, so the user never has to know or type a
three-letter code and we make zero network calls just to search.

Usage:
    RTT_REFRESH_TOKEN=<token> python3 tools/gen_stations.py > src/stations.h

The refresh token is only used at generation time on a developer machine; it
is NEVER written into the generated header or shipped in the app.
"""
import json
import os
import sys
import urllib.request

API = "https://data.rtt.io"


def fetch(url, bearer):
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + bearer})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def main():
    refresh = os.environ.get("RTT_REFRESH_TOKEN")
    if not refresh:
        sys.exit("set RTT_REFRESH_TOKEN")
    access = fetch(API + "/api/get_access_token", refresh)["token"]
    stops = fetch(API + "/data/stops", access)["stops"]

    seen = {}
    for s in stops:
        crs = (s.get("shortCode") or "").strip().upper()
        name = (s.get("description") or "").strip()
        # Only public stations have a 3-letter CRS code; skip junctions/depots.
        if len(crs) != 3 or not name:
            continue
        seen[crs] = name  # last write wins; CRS is unique anyway
    rows = sorted(seen.items(), key=lambda kv: kv[1].lower())

    out = sys.stdout
    out.write("/*\n")
    out.write(" * stations.h — bundled GB National Rail station list (GENERATED).\n")
    out.write(" *\n")
    out.write(" * Do not edit by hand. Regenerate with tools/gen_stations.py.\n")
    out.write(" * Source: Realtime Trains API /data/stops (namespace gb-nr).\n")
    out.write(" * Sorted by name so the search results read alphabetically.\n")
    out.write(" */\n")
    out.write("#ifndef INKSTATION_STATIONS_H\n#define INKSTATION_STATIONS_H\n\n")
    out.write("typedef struct { const char *name; const char *crs; } station_t;\n\n")
    out.write("static const station_t STATIONS[] = {\n")
    for crs, name in rows:
        safe = name.replace("\\", "\\\\").replace('"', '\\"')
        out.write('    { "%s", "%s" },\n' % (safe, crs))
    out.write("};\n\n")
    out.write("#define STATION_COUNT ((int)(sizeof(STATIONS) / sizeof(STATIONS[0])))\n\n")
    out.write("#endif /* INKSTATION_STATIONS_H */\n")


if __name__ == "__main__":
    main()
