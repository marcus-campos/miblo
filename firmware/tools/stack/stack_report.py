#!/usr/bin/env python3
"""Worst-case stack of loop() on the ESP8266 (4 KB cont stack), from GCC's call graph.

`make stack` builds the firmware with -fstack-usage -fcallgraph-info=su into .pio/stack and runs
this on the .ci files:

    stack_report.py <build dir> [--top N] [--chain REGEX]...

It prints the deepest chains from loop(): through the HTTP handlers (server.handleClient), the
frame (app::frame: screens, notes, friends) and the rest of loop(). Each chain's own frames come
from the compiler; indirect calls are resolved by rule (below), so the numbers are an estimate:
  - the route tables (routes::Route in src/*.cpp and src/platform/ota.cpp) for TableHandler;
  - the canvas virtuals (TftCanvas and the GuardCanvas/ShiftCanvas wrappers) for the screens;
  - TFT_eSPI/TFT_eSprite virtuals for TftCanvas, LittleFS's file implementation for fs::File,
    WiFiClient/Stream virtuals for the web server and Print.
Precompiled SDK/lwIP code has no call graph: add ~300-600 B under a send (tcp_write/ip_output).
"""
import os
import re
import sys

STACK_BYTES = 4096   # cont stack of loop()
WARN_BYTES = 3072    # findings start here

NODE_RE = re.compile(r'node: \{ title: "([^"]+)" label: "([^\n"]*)\\n[^"]*\\n(\d+) bytes \(([^)]*)\)"')
EDGE_RE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
NEVER = re.compile(r'__assert_func|panic|postmortem|abort')


class Graph:
    def __init__(self, build):
        self.nodes = {}  # title -> (own bytes, readable name)
        self.edges = {}  # title -> set(target titles)
        for root, _, files in os.walk(build):
            for f in files:
                if f.endswith('.ci'):
                    self._load(os.path.join(root, f))
        self.by_mangled = {}
        for t in self.nodes:
            self.by_mangled.setdefault(t.split(':')[-1], []).append(t)
        self.best = {}
        self.onstack = set()
        self.rules = []

    def _load(self, path):
        txt = open(path, errors='replace').read()
        for m in NODE_RE.finditer(txt):
            t, name, b = m.group(1), m.group(2), int(m.group(3))
            if t not in self.nodes or self.nodes[t][0] < b:
                self.nodes[t] = (b, name)
        for m in EDGE_RE.finditer(txt):
            self.edges.setdefault(m.group(1), set()).add(m.group(2))

    def name(self, t):
        return self.nodes[t][1] if t in self.nodes else t

    def resolve(self, t):
        return [t] if t in self.nodes else self.by_mangled.get(t.split(':')[-1], [])

    def matching(self, rx):
        r = re.compile(rx)
        return set(t for t in self.nodes if r.search(self.nodes[t][1]))

    def candidates(self, t):
        name = self.name(t)
        for rx, targets in self.rules:
            if re.search(rx, name):
                return targets
        return set()  # unknown indirect call: not counted

    def depth(self, t):
        """(bytes, [(title, own bytes)...]) of the deepest chain from t; recursion is cut."""
        if t in self.best:
            return self.best[t]
        if t in self.onstack:
            return (0, [])
        self.onstack.add(t)
        targets = set()
        for e in self.edges.get(t, ()):
            if e == '__indirect_call':
                targets |= self.candidates(t)
            elif not NEVER.search(e):
                targets.update(self.resolve(e))
        worst = (0, [])
        for x in targets:
            if x != t:
                d = self.depth(x)
                if d[0] > worst[0]:
                    worst = d
        self.onstack.discard(t)
        own = self.nodes[t][0] if t in self.nodes else 0
        res = (own + worst[0], [(t, own)] + worst[1])
        self.best[t] = res
        return res


def route_functions(fw):
    """Handler names from the route tables: {"/path", HTTP_X, handler[, upload]}."""
    names = set()
    for rel in ('src/api.cpp', 'src/web.cpp', 'src/platform/ota.cpp'):
        ns = os.path.splitext(os.path.basename(rel))[0]
        txt = open(os.path.join(fw, rel)).read()
        for m in re.finditer(r'\{"/[^"]*",\s*HTTP_[A-Z]+,\s*([A-Za-z_]\w*)(?:,\s*([A-Za-z_]\w*))?', txt):
            for g in m.groups():
                if g:
                    names.add('%s::%s' % (ns, g))
    return names


def short(g, t, n=48):
    x = re.sub(r'\(.*', '', g.name(t))
    return re.sub(r'^(static |virtual )?\S+ ', '', x)[:n]


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    build, top, chains = args[0], 12, []
    i = 1
    while i < len(args):
        if args[i] == '--top':
            top = int(args[i + 1])
            i += 2
        elif args[i] == '--chain':
            chains.append(args[i + 1])
            i += 2
        else:
            sys.exit('unknown option ' + args[i])
    fw = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    g = Graph(build)
    if not g.nodes:
        sys.exit('no .ci files under %s (build with -fcallgraph-info=su)' % build)

    routes = set()
    for fn in route_functions(fw):
        routes |= g.matching(r'(^|[^\w:])' + re.escape(fn) + r'\(')
    tft_canvas = g.matching(r'^virtual \S+ TftCanvas::')
    wrap_canvas = g.matching(r'^virtual \S+ (screens::GuardCanvas|ui::ShiftCanvas)::')
    tft = g.matching(r'^virtual \S+ (TFT_eSPI|TFT_eSprite|U8g2_for_TFT_eSPI)::')
    net_io = g.matching(r'^virtual \S+ (WiFiClient|StreamString|S2Stream|StreamConstPtr|StreamNull|HardwareSerial)::')
    file_io = g.matching(r'^virtual \S+ (fs::File|littlefs_impl::LittleFSFileImpl|StreamConstPtr|S2Stream|StreamString)::')

    def leaf(s):
        return set(t for t in s if '__indirect_call' not in g.edges.get(t, ()))

    g.rules = [
        (r'TableHandler::handle', routes),
        (r'TableHandler::upload', g.matching(r'^void ota::upload\(')),
        (r'::_handleRequest\(', g.matching(r'TableHandler::handle\(') | g.matching(r'^web::begin.*<lambda')),
        (r'::_parseForm\(', g.matching(r'TableHandler::upload\(')),
        (r'(GuardCanvas|ShiftCanvas)::', tft_canvas | wrap_canvas),
        (r'TftCanvas::', tft),
        (r'^\S+ (TFT_|U8g2)|TFT_eS', leaf(tft)),
        (r'screens::|ui::', tft_canvas | wrap_canvas),
        (r'^\S* ?lfs_', g.matching(r'^(int )?lfs_flash_')),
        (r'^virtual \S+ littlefs_impl', set()),
        (r'^virtual \S+ fs::', g.matching(r'^virtual \S+ littlefs_impl::LittleFSFileImpl::')),
        (r'storage::|fs::|ArduinoJson.*(File|Stream)|MD5|Updater', file_io),
        (r'^virtual ', leaf(net_io)),
        (r'Stream|Print|String|WiFiClient|web::|api::|ota::|ArduinoJson|ESP8266WebServerTemplate', net_io),
    ]

    def one(rx):
        found = sorted(g.matching(rx))
        if not found:
            sys.exit('not in the call graph: ' + rx)
        return found[0]

    wrapper = one(r'^void loop_wrapper\(')
    app = one(r'^void app::loop\(\)$')
    frame = one(r'^void app::frame\(')
    client = one(r'ESP8266WebServerTemplate<ServerType>::handleClient\(')
    request = one(r'::_handleRequest\(')
    table = one(r'TableHandler::handle\(')
    base = g.nodes[wrapper][0] + g.nodes[one(r'^void loop\(\)$')][0] + g.nodes[app][0]

    rows = []
    for e in g.edges.get(frame, ()):
        for x in g.resolve(e):
            rows.append((base + g.nodes[frame][0] + g.depth(x)[0], 'frame > ' + short(g, x), x))
    for e in g.edges.get(app, ()):
        for x in g.resolve(e):
            if x not in (frame, client):
                rows.append((base + g.depth(x)[0], short(g, x), x))
    http = base + g.nodes[client][0] + g.nodes[request][0] + g.nodes[table][0]
    for x in routes:
        rows.append((http + g.depth(x)[0], 'http > ' + short(g, x), x))
    rows.sort(key=lambda r: -r[0])

    worst = g.depth(wrapper)[0]
    print('loop() worst case: %d of %d bytes (%s)' % (worst, STACK_BYTES, 'OVER %d' % WARN_BYTES if worst > WARN_BYTES else 'ok'))
    print('%6s  %-56s %s' % ('bytes', 'chain from loop()', 'deepest frames below it'))
    for d, label, x in rows[:top]:
        tail = ' > '.join(short(g, t, 26) for t, b in g.depth(x)[1][1:8] if b >= 64)
        print('%6d  %-56s %s' % (d, label[:56], tail))
    for rx in chains:
        for t in sorted(g.matching(rx)):
            d = g.depth(t)
            print('\n%d  %s' % (d[0], g.name(t)))
            for x, b in d[1]:
                print('  %5d  %s' % (b, g.name(x)[:110]))
    sys.exit(1 if worst > WARN_BYTES else 0)


if __name__ == '__main__':
    main()
