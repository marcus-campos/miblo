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
  - the daily-life lambdas (api.cpp handleX() -> dailyRoute(DailyHandler)) for dailyRoute.
Frames the compiler marks "dynamic" (VLAs) get the bound from DYNAMIC_BOUNDS added; one with no
known bound is printed as a WARNING (counted with its fixed part only). Recursion is cut: each
function counts once per chain, the same whatever order the graph is walked in.
"""
import os
import re
import sys

STACK_BYTES = 4096   # cont stack of loop()
WARN_BYTES = 3072    # findings start here

NODE_RE = re.compile(r'node: \{ title: "([^"]+)" label: "([^\n"]*)\\n[^"]*\\n(\d+) bytes \(([^)]*)\)"')
EDGE_RE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
NEVER = re.compile(r'__assert_func|panic|postmortem|abort')

# Variable-size frames (VLA/alloca) reachable from loop(): the bytes each adds at most on top of
# what the compiler reports, from the source. A dynamic frame missing here is printed as a WARNING.
DYNAMIC_BOUNDS = [
    (r'TFT_eSPI::pushImage\(', 480, 'uint16_t lineBuf[dw]: dw clipped to the 240 px panel'),
    (r'Stream::SendGenericRegular\(', 64, 'char temp[w]: w <= Stream::temporaryStackBufferSize (64)'),
    (r'^qrcode_initBytes$', 272, 'LOCK_VERSION=3 (29x29), inlined: codewords 71 + function grid 106 + ECC 71 + 15 + 2'),
    (r'^void setTZ\(', 48, 'tzram[strlen(rule) + 1]: the rule is net.cpp applyTimezone() char[48]'),
    (r'^umm_info_safe_printf_P$', 80, 'ram_buf[strlen(fmt) + 1]: umm_malloc formats are <= 68 bytes'),
    (r'::_parseForm\(', 80, 'fastBoundary[boundary + 5]: web.cpp limitPostBody admits a multipart body only with a boundary <= 70 characters (RFC 2046)'),
]


class Graph:
    def __init__(self, build):
        self.nodes = {}  # title -> (own bytes, readable name)
        self.dynamic = set()  # titles whose frame GCC marks "dynamic" (a VLA or alloca): own bytes are a minimum
        self.edges = {}  # title -> set(target titles)
        for root, _, files in os.walk(build):
            for f in sorted(files):
                if f.endswith('.ci'):
                    self._load(os.path.join(root, f))
        self.by_mangled = {}
        for t in self.nodes:
            self.by_mangled.setdefault(t.split(':')[-1], []).append(t)
        self.rules = []
        self.extra = {}  # title -> bytes added to a dynamic frame (its known bound)
        self._succ = {}
        self._depth = None

    def _load(self, path):
        txt = open(path, errors='replace').read()
        for m in NODE_RE.finditer(txt):
            t, name, b = m.group(1), m.group(2), int(m.group(3))
            if not re.search(r'\w', name):  # a function defined through a macro: no usable label
                name = t
            if t not in self.nodes or self.nodes[t][0] < b:
                self.nodes[t] = (b, name)
            if 'dynamic' in m.group(4):
                self.dynamic.add(t)
        for m in EDGE_RE.finditer(txt):
            self.edges.setdefault(m.group(1), set()).add(m.group(2))

    def name(self, t):
        return self.nodes[t][1] if t in self.nodes else t

    def own(self, t):
        return (self.nodes[t][0] if t in self.nodes else 0) + self.extra.get(t, 0)

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

    def succ(self, t):
        """What t calls: direct calls, and its indirect calls resolved by the rules (sorted)."""
        if t not in self._succ:
            targets = set()
            for e in self.edges.get(t, ()):
                if e == '__indirect_call':
                    targets |= self.candidates(t)
                elif not NEVER.search(e):
                    targets.update(self.resolve(e))
            self._succ[t] = sorted(targets)
        return self._succ[t]

    def reachable(self, root):
        seen, todo = set(), [root]
        while todo:
            t = todo.pop()
            if t not in seen:
                seen.add(t)
                todo.extend(self.succ(t))
        return seen

    def depth(self, t):
        """(bytes, [(title, own bytes)...]) of the deepest chain from t; recursion is cut.

        The call graph is split into strongly connected components (Tarjan), walked sinks first,
        so a function's depth never depends on where the walk came from. Inside a recursive
        component each entry gets its own DFS (successors in a fixed order) whose back edges are
        the recursion cut: every chain counts each function at most once."""
        if self._depth is None:
            self._depth = {}
            for comp in self._components():
                self._solve(comp)
        return self._depth[t]

    def _components(self):
        """Tarjan's SCCs (iterative), in reverse topological order: callees before callers."""
        index, low, on, stack, out = {}, {}, set(), [], []
        counter = [0]
        for root in sorted(self.nodes):
            if root in index:
                continue
            index[root] = low[root] = counter[0]
            counter[0] += 1
            stack.append(root)
            on.add(root)
            work = [(root, iter(self.succ(root)))]
            while work:
                v, it = work[-1]
                for w in it:
                    if w not in index:
                        index[w] = low[w] = counter[0]
                        counter[0] += 1
                        stack.append(w)
                        on.add(w)
                        work.append((w, iter(self.succ(w))))
                        break
                    if w in on:
                        low[v] = min(low[v], index[w])
                else:
                    work.pop()
                    if work:
                        low[work[-1][0]] = min(low[work[-1][0]], low[v])
                    if low[v] == index[v]:
                        comp = []
                        while True:
                            w = stack.pop()
                            on.discard(w)
                            comp.append(w)
                            if w == v:
                                break
                        out.append(sorted(comp))
        return out

    def _best_outside(self, v, comp):
        worst = (0, [])
        for x in self.succ(v):
            if x not in comp and self._depth[x][0] > worst[0]:
                worst = self._depth[x]
        return worst

    def _solve(self, comp):
        members = set(comp)
        if len(comp) == 1:
            v = comp[0]
            tail = self._best_outside(v, members)  # a call to itself is cut
            self._depth[v] = (self.own(v) + tail[0], [(v, self.own(v))] + tail[1])
            return
        outside = {v: self._best_outside(v, members) for v in comp}
        for entry in comp:
            # DFS from the entry: tree, forward and cross edges stay, back edges (the recursion)
            # go; the longest path over what is left (a DAG) is the deepest chain.
            order, state, keep = [], {entry: 1}, {}
            work = [(entry, iter([x for x in self.succ(entry) if x in members]))]
            keep[entry] = []
            while work:
                v, it = work[-1]
                for w in it:
                    if w not in state:
                        state[w] = 1
                        keep[v].append(w)
                        keep[w] = []
                        work.append((w, iter([x for x in self.succ(w) if x in members])))
                        break
                    if state[w] == 2:
                        keep[v].append(w)  # forward or cross edge: no cycle
                else:
                    work.pop()
                    state[v] = 2
                    order.append(v)
            best = {}
            for v in order:  # postorder: every kept successor is done first
                worst = outside[v]
                for w in keep[v]:
                    if best[w][0] > worst[0]:
                        worst = best[w]
                best[v] = (self.own(v) + worst[0], [(v, self.own(v))] + worst[1])
            self._depth[entry] = best[entry]


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


def daily_handlers(fw):
    """api.cpp's daily-life routes: each handleX() passes a captureless lambda to dailyRoute(),
    which calls it through a DailyHandler pointer. -> the handleX names."""
    txt = open(os.path.join(fw, 'src/api.cpp')).read()
    return set(re.findall(r'static void (handle\w+)\(\) \{\s*dailyRoute\(\[', txt))


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
    shift_canvas = g.matching(r'^virtual \S+ ui::ShiftCanvas::')
    tft = g.matching(r'^virtual \S+ (TFT_eSPI|TFT_eSprite|U8g2_for_TFT_eSPI)::')
    net_io = g.matching(r'^virtual \S+ (WiFiClient|StreamString|S2Stream|StreamConstPtr|StreamNull|HardwareSerial)::')
    file_io = g.matching(r'^virtual \S+ (fs::File|littlefs_impl::LittleFSFileImpl|StreamConstPtr|S2Stream|StreamString)::')

    daily = set()
    for fn in sorted(daily_handlers(fw)):
        found = g.matching(r'api::%s\(\)::<lambda\(.*>::_FUN\(' % fn)
        if not found:
            sys.exit('DailyHandler of api::%s not in the call graph' % fn)
        daily |= found
    if not daily:
        sys.exit('no DailyHandler lambdas found in src/api.cpp')

    def leaf(s):
        return set(t for t in s if '__indirect_call' not in g.edges.get(t, ()))

    g.rules = [
        (r'TableHandler::handle', routes),
        (r'TableHandler::upload', g.matching(r'^void ota::upload\(')),
        (r'::_handleRequest\(', g.matching(r'TableHandler::handle\(') | g.matching(r'^web::begin.*<lambda')),
        (r'::_parseForm\(', g.matching(r'TableHandler::upload\(')),
        # The wrappers' layering (app.cpp, ui_daily_state.cpp): GuardCanvas > ShiftCanvas > TftCanvas.
        (r'GuardCanvas::', shift_canvas | tft_canvas),
        (r'ShiftCanvas::', tft_canvas),
        (r'TftCanvas::', tft),
        (r'^\S+ (TFT_|U8g2)|TFT_eS', leaf(tft)),
        (r'screens::|ui::', tft_canvas | wrap_canvas),
        (r'^\S* ?lfs_', g.matching(r'^(int )?lfs_flash_')),
        (r'^virtual \S+ littlefs_impl', set()),
        (r'^virtual \S+ fs::', g.matching(r'^virtual \S+ littlefs_impl::LittleFSFileImpl::')),
        (r'storage::|fs::|ArduinoJson.*(File|Stream)|MD5|Updater', file_io),
        (r'api::dailyRoute\(', daily | net_io),
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

    # Frames GCC marks "dynamic" (a VLA or alloca) report only their fixed part. Those reachable
    # from loop() get their known bound added (DYNAMIC_BOUNDS); any other is flagged.
    unbounded = []
    for t in sorted(g.reachable(wrapper) & g.dynamic, key=g.name):
        bound = next((b for rx, b, _ in DYNAMIC_BOUNDS if re.search(rx, g.name(t))), None)
        if bound is None:
            unbounded.append(t)
        else:
            g.extra[t] = bound

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
    for t in sorted(g.extra, key=g.name):
        print('dynamic frame: %s counted as %d + %d bytes' % (short(g, t, 70), g.nodes[t][0], g.extra[t]))
    for t in unbounded:
        print('WARNING: dynamic frame with no known bound (only its fixed %d bytes counted): %s'
              % (g.nodes[t][0], g.name(t)[:110]))
    for rx in chains:
        for t in sorted(g.matching(rx)):
            d = g.depth(t)
            print('\n%d  %s' % (d[0], g.name(t)))
            for x, b in d[1]:
                print('  %5d  %s' % (b, g.name(x)[:110]))
    sys.exit(1 if worst > WARN_BYTES else 0)


if __name__ == '__main__':
    main()
