#include <canopy/live_server.hpp>

#include <fizmo/../../Source/Networking/Node/http.hpp>
#include <fizmo/../../Source/Networking/Node/loop.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;
namespace fnet = fizmo::networking;

namespace canopy {

namespace {

const char* const k_reload_path = "/__canopy/reload";
const char* const k_poll_path = "/__canopy/poll";
const char* const k_console_path = "/__canopy/console";
const char* const k_dom_path = "/__canopy/dom";

const char* const k_page_script = R"JS((function () {
    if (window.__canopy) return;
    window.__canopy = true;
    var RELOAD = __RELOAD__, CONSOLE = __CONSOLE__, base = '/__canopy/';
    var id = Math.random().toString(36).slice(2) + Date.now().toString(36);
    var me = document.currentScript;
    if (me && me.setAttribute) me.setAttribute('data-canopy', 'script');
    var page = location.pathname + location.search;
    var queue = [], timer = null, depth = 0, counts = {}, timers = {}, own = null;
    function clean(s) { return String(s).replace(/[\x1c-\x1f]/g, ' '); }
    function short(u) { u = String(u || ''); if (u.indexOf(location.origin) === 0) u = u.slice(location.origin.length); return u; }
    function flush() {
        timer = null;
        if (!queue.length) return;
        var body = queue.join('');
        queue = [];
        try { fetch(base + 'console', { method: 'POST', body: body, keepalive: body.length < 60000, headers: { 'Content-Type': 'text/plain' } }).catch(function () {}); } catch (e) {}
    }
    function send(level, text, src, line, col) {
        if (!CONSOLE) return;
        var pad = depth > 0 && level !== 'clear' ? new Array(depth + 1).join('  ') : '';
        if (pad) text = pad + String(text).split('\n').join('\n' + pad);
        queue.push([level, clean(src || ''), line || 0, col || 0, clean(page), id, clean(text)].join('\x1f') + '\x1e');
        if (!timer) timer = setTimeout(flush, 30);
    }
    function frames(stack) {
        var out = [], lines = String(stack || '').split('\n');
        for (var i = 0; i < lines.length; i++) {
            var m = /((?:https?|file):\/\/[^\s()]+?):(\d+):(\d+)\)?\s*$/.exec(lines[i]);
            if (!m) continue;
            if (own && m[1] === own.url && +m[2] === own.line) continue;
            out.push({ url: m[1], line: +m[2], col: +m[3], text: lines[i].trim() });
        }
        return out;
    }
    try { throw new Error('x'); } catch (e) { var f = frames(e.stack); if (f.length) own = { url: f[0].url, line: f[0].line }; }
    function caller() {
        try { throw new Error('x'); } catch (e) { var f = frames(e.stack); return f.length ? f[0] : null; }
    }
    function key(k) { return /^[A-Za-z_$][\w$]*$/.test(k) ? k : JSON.stringify(k); }
    function show(v, d, seen) {
        d = d || 0; seen = seen || [];
        var t = typeof v;
        if (v === null) return 'null';
        if (t === 'undefined') return 'undefined';
        if (t === 'string') return d ? JSON.stringify(v) : v;
        if (t === 'bigint') return String(v) + 'n';
        if (t === 'number' || t === 'boolean') return Object.is(v, -0) ? '-0' : String(v);
        if (t === 'symbol') return v.toString();
        if (t === 'function') return (/^class\b/.test(Function.prototype.toString.call(v)) ? 'class ' : 'ƒ ') + (v.name || 'anonymous') + (/^class\b/.test(Function.prototype.toString.call(v)) ? '' : '()');
        try {
            if (v instanceof Error) {
                var head = (v.name || 'Error') + ': ' + v.message, st = String(v.stack || '').split(location.origin + '/').join('/');
                if (st.indexOf(head) === 0) return st;
                return head + (st ? '\n' + st : '');
            }
            if (typeof Node !== 'undefined' && v instanceof Node) {
                if (v.nodeType === 3) return JSON.stringify(v.nodeValue);
                if (v.nodeType === 9) return '#document';
                if (v.nodeType !== 1) return v.nodeName;
                var s = '<' + v.tagName.toLowerCase();
                for (var a = 0; a < v.attributes.length && a < 6; a++) s += ' ' + v.attributes[a].name + '="' + v.attributes[a].value + '"';
                return s + '>';
            }
            if (seen.indexOf(v) >= 0) return '[Circular]';
            if (d > 3) return Array.isArray(v) ? '[…]' : '{…}';
            seen = seen.concat([v]);
            if (v instanceof Date) return isNaN(v) ? 'Invalid Date' : v.toISOString();
            if (v instanceof RegExp) return String(v);
            if (typeof Promise !== 'undefined' && v instanceof Promise) return 'Promise {…}';
            var parts = [], i, n = 0;
            if (Array.isArray(v) || (typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView && ArrayBuffer.isView(v) && !(v instanceof DataView))) {
                for (i = 0; i < v.length && i < 100; i++) parts.push(i in v ? show(v[i], d + 1, seen) : '<empty>');
                if (v.length > 100) parts.push('… ' + (v.length - 100) + ' more');
                return (Array.isArray(v) ? '' : v.constructor.name + '(' + v.length + ') ') + '[' + parts.join(', ') + ']';
            }
            if (typeof Map !== 'undefined' && v instanceof Map) {
                v.forEach(function (val, k) { if (n++ < 50) parts.push(show(k, d + 1, seen) + ' => ' + show(val, d + 1, seen)); });
                return 'Map(' + v.size + ') {' + parts.join(', ') + '}';
            }
            if (typeof Set !== 'undefined' && v instanceof Set) {
                v.forEach(function (val) { if (n++ < 50) parts.push(show(val, d + 1, seen)); });
                return 'Set(' + v.size + ') {' + parts.join(', ') + '}';
            }
            var keys = Object.keys(v);
            for (i = 0; i < keys.length && i < 50; i++) parts.push(key(keys[i]) + ': ' + show(v[keys[i]], d + 1, seen));
            if (keys.length > 50) parts.push('…');
            var proto = Object.getPrototypeOf(v), name = proto && proto.constructor && proto.constructor !== Object ? proto.constructor.name + ' ' : '';
            if (!proto) name = '[Object: null prototype] ';
            return name + '{' + parts.join(', ') + '}';
        } catch (e) { return Object.prototype.toString.call(v); }
    }
    function format(args) {
        var out = [], i = 0;
        if (typeof args[0] === 'string' && /%[sdifoOc%]/.test(args[0])) {
            var rest = args.slice(1), j = 0;
            out.push(args[0].replace(/%([sdifoOc%])/g, function (all, c) {
                if (c === '%') return '%';
                if (j >= rest.length) return all;
                var a = rest[j++];
                if (c === 's') return typeof a === 'string' ? a : show(a, 1);
                if (c === 'd' || c === 'i') return typeof a === 'bigint' ? String(a) + 'n' : String(parseInt(a, 10));
                if (c === 'f') return String(parseFloat(a));
                if (c === 'c') return '';
                return show(a, 1);
            }));
            i = 1 + j;
        } else if (args.length) {
            out.push(show(args[0]));
            i = 1;
        }
        for (; i < args.length; i++) out.push(show(args[i]));
        return out.join(' ');
    }
    function emit(level, text, at) {
        var f = at || caller();
        send(level, text, f ? short(f.url) : '', f ? f.line : 0, f ? f.col : 0);
    }
    function hook(name, fn) {
        var orig = console[name];
        console[name] = function () {
            try { fn.apply(null, [].slice.call(arguments)); } catch (e) {}
            if (typeof orig === 'function') return orig.apply(console, arguments);
        };
    }
    if (CONSOLE) {
        ['log', 'info', 'warn', 'error', 'debug', 'dir', 'dirxml', 'table'].forEach(function (m) {
            hook(m, function () {
                var level = m === 'dir' || m === 'dirxml' || m === 'table' ? 'log' : m;
                emit(level, format([].slice.call(arguments)));
            });
        });
        hook('trace', function () {
            var f = [];
            try { throw new Error('x'); } catch (e) { f = frames(e.stack); }
            var lines = ['console.trace' + (arguments.length ? ': ' + format([].slice.call(arguments)) : '')];
            for (var i = 0; i < f.length; i++) lines.push('    at ' + short(f[i].url) + ':' + f[i].line + ':' + f[i].col);
            emit('log', lines.join('\n'), f[0]);
        });
        hook('assert', function (ok) {
            if (ok) return;
            var rest = [].slice.call(arguments, 1);
            emit('error', 'Assertion failed' + (rest.length ? ': ' + format(rest) : ''));
        });
        hook('clear', function () { send('clear', '', '', 0, 0); });
        hook('group', function () { emit('log', arguments.length ? format([].slice.call(arguments)) : 'console.group'); depth++; });
        hook('groupCollapsed', function () { emit('log', arguments.length ? format([].slice.call(arguments)) : 'console.group'); depth++; });
        hook('groupEnd', function () { if (depth > 0) depth--; });
        hook('count', function (label) {
            label = label === undefined ? 'default' : String(label);
            counts[label] = (counts[label] || 0) + 1;
            emit('info', label + ': ' + counts[label]);
        });
        hook('countReset', function (label) { counts[label === undefined ? 'default' : String(label)] = 0; });
        hook('time', function (label) { timers[label === undefined ? 'default' : String(label)] = performance.now(); });
        function lap(label, end, extra) {
            label = label === undefined ? 'default' : String(label);
            if (!(label in timers)) { emit('warn', "Timer '" + label + "' does not exist"); return; }
            var ms = performance.now() - timers[label];
            if (end) delete timers[label];
            emit('info', label + ': ' + (Math.round(ms * 1000) / 1000) + ' ms' + (extra.length ? ' ' + format(extra) : '') + (end ? ' - timer ended' : ''));
        }
        hook('timeLog', function (label) { lap(label, false, [].slice.call(arguments, 1)); });
        hook('timeEnd', function (label) { lap(label, true, []); });
        window.addEventListener('error', function (e) {
            var t = e.target;
            if (t && t !== window && t.tagName) {
                var u = t.currentSrc || t.src || t.href || '';
                if (!u || String(u).indexOf(location.origin + '/') === 0) return;
                var what = t.tagName.toLowerCase() === 'link' ? 'stylesheet' : t.tagName.toLowerCase();
                send('error', 'Failed to load ' + what + (u ? ' ' + short(u) : ''), page, 0, 0);
                return;
            }
            var text = e.error !== undefined && e.error !== null ? show(e.error) : (e.message || 'Script error.');
            if (!/^Uncaught\b/.test(text)) text = 'Uncaught ' + text;
            send('error', text, short(e.filename || ''), e.lineno || 0, e.colno || 0);
        }, true);
        window.addEventListener('unhandledrejection', function (e) {
            var r = e.reason, f = r && r.stack ? frames(r.stack)[0] : null;
            send('error', 'Uncaught (in promise) ' + show(r, r instanceof Error ? 0 : 1), f ? short(f.url) : '', f ? f.line : 0, f ? f.col : 0);
        });
        document.addEventListener('securitypolicyviolation', function (e) {
            send('error', 'Blocked by the Content Security Policy (' + e.violatedDirective + '): ' + short(e.blockedURI), short(e.sourceFile || ''), e.lineNumber || 0, e.columnNumber || 0);
        });
        window.addEventListener('pagehide', flush);
    }
    function result(v) {
        send('result', typeof v === 'string' ? JSON.stringify(v) : show(v, 0), '', 0, 0);
        window.$_ = v;
    }
    function run(code) {
        var value, ok = false;
        if (/^\s*\{/.test(code) && !/;\s*$/.test(code)) {
            try { value = (0, eval)('(' + code + ')'); ok = true; } catch (e) {}
        }
        if (!ok) {
            try { value = (0, eval)(code); }
            catch (e) { send('error', 'Uncaught ' + (e instanceof Error ? (e.name || 'Error') + ': ' + e.message : show(e, 1)), '', 0, 0); return; }
        }
        if (value && typeof value.then === 'function' && typeof Promise !== 'undefined' && value instanceof Promise) {
            value.then(function (v) { result(v); }, function (r) { send('error', 'Uncaught (in promise) ' + show(r, r instanceof Error ? 0 : 1), '', 0, 0); });
            return;
        }
        result(value);
    }
)JS"
R"JS(    var dom = { ids: new WeakMap(), nodes: {}, next: 1, watching: false, timer: null, observer: null, box: null, label: null, shown: 0, sent: null };
    function nid(n) {
        var v = dom.ids.get(n);
        if (!v) { v = dom.next++; dom.ids.set(n, v); }
        dom.nodes[v] = n;
        return v;
    }
    function walk(n, depth, out) {
        if (out.length > 20000) return;
        var t = n.nodeType, i;
        if (t === 1) {
            if (n.hasAttribute('data-canopy')) return;
            var attrs = [];
            for (i = 0; i < n.attributes.length; i++) attrs.push(clean(n.attributes[i].name) + '\x1d' + clean(n.attributes[i].value));
            out.push(['e', nid(n), depth, n.tagName.toLowerCase(), attrs.join('\x1c')].join('\x1f'));
            var kids = n.tagName.toLowerCase() === 'template' && n.content ? n.content.childNodes : n.childNodes;
            for (i = 0; i < kids.length; i++) walk(kids[i], depth + 1, out);
        } else if (t === 3) {
            if (!/\S/.test(n.nodeValue)) return;
            out.push(['t', nid(n), depth, clean(n.nodeValue.length > 20000 ? n.nodeValue.slice(0, 20000) + '…' : n.nodeValue)].join('\x1f'));
        } else if (t === 8) {
            out.push(['c', nid(n), depth, clean(n.nodeValue)].join('\x1f'));
        } else if (t === 10) {
            out.push(['d', nid(n), depth, clean(n.name || 'html')].join('\x1f'));
        }
    }
    function snapshot() {
        dom.nodes = {};
        var out = [], kids = document.childNodes;
        for (var i = 0; i < kids.length; i++) walk(kids[i], 0, out);
        return out.join('\x1e');
    }
    function sendDom(force) {
        dom.timer = null;
        var s = snapshot();
        if (!force && s === dom.sent) return;
        dom.sent = s;
        try { fetch(base + 'dom?page=' + id, { method: 'POST', body: s, headers: { 'Content-Type': 'text/plain' } }).catch(function () {}); } catch (e) {}
        if (dom.shown) place(dom.shown);
    }
    function changed() {
        if (dom.watching && !dom.timer) dom.timer = setTimeout(function () { sendDom(false); }, 200);
    }
    function watch(on) {
        if (on === dom.watching) return;
        dom.watching = on;
        if (on) {
            if (!dom.observer && typeof MutationObserver !== 'undefined') dom.observer = new MutationObserver(changed);
            if (dom.observer) dom.observer.observe(document, { subtree: true, childList: true, attributes: true, characterData: true });
            sendDom(true);
        } else {
            if (dom.observer) dom.observer.disconnect();
            if (dom.timer) { clearTimeout(dom.timer); dom.timer = null; }
            dom.sent = null;
            mark(0);
        }
    }
    function rect(n) {
        if (!n || !n.ownerDocument) return null;
        if (n.nodeType === 1 && n.getBoundingClientRect) return n.getBoundingClientRect();
        if (n.nodeType === 3 && document.createRange) { var r = document.createRange(); r.selectNodeContents(n); return r.getBoundingClientRect(); }
        return null;
    }
    function place(n) {
        var node = dom.nodes[n], r = rect(node);
        if (!r || !document.documentElement) { if (dom.box) dom.box.style.display = 'none'; return; }
        if (!dom.box) {
            dom.box = document.createElement('div');
            dom.box.setAttribute('data-canopy', 'highlight');
            dom.box.style.cssText = 'position:fixed;pointer-events:none;z-index:2147483647;background:rgba(111,168,220,.35);outline:1px solid rgba(111,168,220,.95);box-sizing:border-box;margin:0;padding:0;border:0';
            dom.label = document.createElement('div');
            dom.label.setAttribute('data-canopy', 'label');
            dom.label.style.cssText = 'position:fixed;pointer-events:none;z-index:2147483647;background:#1e2230;color:#e6e6e6;font:12px/1.4 monospace;padding:2px 6px;border-radius:3px;white-space:nowrap;margin:0;border:0';
            window.addEventListener('scroll', function () { if (dom.shown) place(dom.shown); }, true);
            window.addEventListener('resize', function () { if (dom.shown) place(dom.shown); });
        }
        if (!dom.box.parentNode) document.documentElement.appendChild(dom.box);
        if (!dom.label.parentNode) document.documentElement.appendChild(dom.label);
        dom.box.style.display = 'block';
        dom.box.style.left = r.left + 'px';
        dom.box.style.top = r.top + 'px';
        dom.box.style.width = r.width + 'px';
        dom.box.style.height = r.height + 'px';
        var name = node.nodeType === 1 ? node.tagName.toLowerCase() + (node.id ? '#' + node.id : '') + (typeof node.className === 'string' && node.className.trim() ? '.' + node.className.trim().split(/\s+/).join('.') : '') : '#text';
        dom.label.textContent = name + '  ' + Math.round(r.width * 100) / 100 + ' × ' + Math.round(r.height * 100) / 100;
        dom.label.style.display = 'block';
        dom.label.style.left = Math.max(0, r.left) + 'px';
        dom.label.style.top = (r.top > 24 ? r.top - 22 : r.bottom + 2) + 'px';
    }
    function mark(n, reveal) {
        dom.shown = n || 0;
        if (!n) {
            if (dom.box) dom.box.style.display = 'none';
            if (dom.label) dom.label.style.display = 'none';
            return;
        }
        var node = dom.nodes[n];
        if (reveal && node) {
            var el = node.nodeType === 1 ? node : node.parentNode;
            if (el && el.scrollIntoView) { try { el.scrollIntoView({ block: 'nearest', inline: 'nearest' }); } catch (e) { el.scrollIntoView(false); } }
        }
        place(n);
    }
    function edited() { if (dom.watching) sendDom(false); }
    window.__canopyDom = {
        mark: mark,
        refresh: function () { sendDom(true); },
        text: function (n, v) {
            var node = dom.nodes[n];
            if (node && (node.nodeType === 3 || node.nodeType === 8)) node.nodeValue = v;
            edited();
        },
        attr: function (n, name, v) {
            var node = dom.nodes[n];
            if (!node || node.nodeType !== 1) return;
            if (v === null) node.removeAttribute(name); else node.setAttribute(name, v);
            edited();
        },
        remove: function (n) {
            var node = dom.nodes[n];
            if (node && node.parentNode && node !== document.documentElement) node.parentNode.removeChild(node);
            mark(0);
            edited();
        },
        html: function (n, html) {
            var node = dom.nodes[n];
            if (!node) return;
            if (node.nodeType !== 1) {
                var t = document.createElement('template');
                t.innerHTML = html;
                if (node.parentNode) {
                    var kids = [].slice.call(t.content.childNodes);
                    for (var i = 0; i < kids.length; i++) node.parentNode.insertBefore(kids[i], node);
                    node.parentNode.removeChild(node);
                }
            } else if (node === document.documentElement || node === document.head || node === document.body) {
                var tag = node.tagName.toLowerCase();
                var d = new DOMParser().parseFromString(tag === 'html' ? html : tag === 'head' ? '<html>' + html + '<body></body></html>' : '<html><head></head>' + html + '</html>', 'text/html');
                var src = tag === 'html' ? d.documentElement : tag === 'head' ? d.head : d.body;
                var a;
                for (a = node.attributes.length - 1; a >= 0; a--) node.removeAttribute(node.attributes[a].name);
                for (a = 0; a < src.attributes.length; a++) node.setAttribute(src.attributes[a].name, src.attributes[a].value);
                var keep = [].slice.call(node.childNodes).filter(function (k) { return k.nodeType === 1 && k.hasAttribute('data-canopy'); });
                while (node.firstChild) node.removeChild(node.firstChild);
                var add = [].slice.call(src.childNodes);
                for (a = 0; a < add.length; a++) node.appendChild(document.importNode(add[a], true));
                for (a = 0; a < keep.length; a++) node.appendChild(keep[a]);
            } else {
                node.outerHTML = html;
            }
            mark(0);
            edited();
        }
    };
    function quiet(code) {
        try { (0, eval)(code); }
        catch (e) { send('error', 'Elements: ' + (e instanceof Error ? (e.name || 'Error') + ': ' + e.message : show(e, 1)), '', 0, 0); }
    }
    var version = null, after = -1;
    function poll() {
        fetch(base + 'poll?page=' + id + '&after=' + after + '&path=' + encodeURIComponent(page), { cache: 'no-store' })
            .then(function (r) { return r.json(); })
            .then(function (j) {
                if (RELOAD && version !== null && j.version !== version) { flush(); location.reload(); return; }
                version = j.version;
                if (after < 0) after = j.last;
                else for (var i = 0; i < j.commands.length; i++) {
                    var c = j.commands[i];
                    after = c.id;
                    if (c.target && c.target !== id) continue;
                    if (c.quiet) quiet(c.code); else run(c.code);
                }
                watch(!!j.watch);
                setTimeout(poll, dom.watching ? 150 : 500);
            })
            .catch(function () { setTimeout(poll, 1500); });
    }
    poll();
})();)JS";

std::mutex g_loop_mutex;
int g_running_servers = 0;

void loop_started() {
    std::lock_guard<std::mutex> lk(g_loop_mutex);
    if (g_running_servers++ == 0 && !fnet::background_running()) fnet::start_background(1);
}

void loop_stopped() {
    std::lock_guard<std::mutex> lk(g_loop_mutex);
    if (g_running_servers > 0 && --g_running_servers == 0 && fnet::background_running()) fnet::stop_background();
}

} // namespace

void network_loop_acquire() { loop_started(); }
void network_loop_release() { loop_stopped(); }

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

bool inside(const fs::path& path, const fs::path& root) {
    std::string p = path.generic_string();
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    std::string r = root.generic_string();
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    if (p == r) return true;
    if (!r.empty() && r.back() != '/') r += '/';
    return p.size() > r.size() && p.compare(0, r.size(), r) == 0;
}

fs::path clean_root(const std::string& folder) {
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::absolute(folder, ec), ec);
    if (ec) root = fs::absolute(folder).lexically_normal();
    while (root.has_relative_path() && root.filename().empty()) root = root.parent_path();
    return root;
}

std::optional<std::string> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string reload_script() {
    return std::string("<script>(function(){var seen=null;function check(){fetch('") + k_reload_path +
           "',{cache:'no-store'}).then(function(r){return r.text();}).then(function(v){if(seen!==null&&v!==seen){location.reload();return;}seen=v;setTimeout(check,600);})"
           ".catch(function(){setTimeout(check,1500);});}check();})();</script>";
}

std::string json_string(const std::string& text) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += "\"";
    return out;
}

std::size_t find_tag_end(const std::string& low, const std::string& tag) {
    std::size_t at = 0;
    while ((at = low.find("<" + tag, at)) != std::string::npos) {
        const char next = at + tag.size() + 1 < low.size() ? low[at + tag.size() + 1] : '\0';
        if (next == '>' || next == ' ' || next == '\t' || next == '\n' || next == '\r' || next == '/') {
            const std::size_t close = low.find('>', at);
            return close == std::string::npos ? std::string::npos : close + 1;
        }
        at += tag.size() + 1;
    }
    return std::string::npos;
}

std::string waiting_page(std::uint16_t port) {
    return "<!doctype html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"1\"><title>Waiting\xE2\x80\xA6</title></head>"
           "<body style=\"font-family:sans-serif;padding:24px;color:#555\"><h2>Waiting for your app on port " + std::to_string(port) + "\xE2\x80\xA6</h2>"
           "<p>It isn't answering yet (it may be starting or restarting). This page tries again every second.</p><p style=\"color:#888\">Canopy local server</p></body></html>";
}

std::string url_page(const std::string& url) {
    std::string path = url;
    const std::size_t scheme = path.find("://");
    if (scheme != std::string::npos) {
        const std::size_t slash = path.find('/', scheme + 3);
        path = slash == std::string::npos ? std::string("/") : path.substr(slash);
    }
    return path.substr(0, path.find('#'));
}

std::string url_path_only(const std::string& url) {
    std::string path = url;
    const std::size_t scheme = path.find("://");
    if (scheme != std::string::npos) {
        const std::size_t slash = path.find('/', scheme + 3);
        path = slash == std::string::npos ? std::string("/") : path.substr(slash);
    }
    return path.substr(0, path.find_first_of("?#"));
}

std::string error_page(int code, const std::string& message) {
    return "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + std::to_string(code) + "</title></head><body style=\"font-family:sans-serif;padding:24px\"><h1>" +
           std::to_string(code) + " " + html_escape(fnet::http::status_text(code)) + "</h1><p>" + html_escape(message) + "</p><p style=\"color:#888\">Canopy local server</p></body></html>";
}

} // namespace

std::string mime_type_for(const std::string& path) {
    static const std::pair<const char*, const char*> types[] = {
        { ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" }, { ".css", "text/css; charset=utf-8" },
        { ".js", "text/javascript; charset=utf-8" }, { ".mjs", "text/javascript; charset=utf-8" }, { ".json", "application/json; charset=utf-8" },
        { ".map", "application/json; charset=utf-8" }, { ".txt", "text/plain; charset=utf-8" }, { ".md", "text/markdown; charset=utf-8" },
        { ".csv", "text/csv; charset=utf-8" }, { ".xml", "application/xml; charset=utf-8" }, { ".svg", "image/svg+xml" },
        { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" }, { ".webp", "image/webp" },
        { ".avif", "image/avif" }, { ".bmp", "image/bmp" }, { ".ico", "image/x-icon" }, { ".wasm", "application/wasm" },
        { ".woff", "font/woff" }, { ".woff2", "font/woff2" }, { ".ttf", "font/ttf" }, { ".otf", "font/otf" },
        { ".mp3", "audio/mpeg" }, { ".wav", "audio/wav" }, { ".ogg", "audio/ogg" }, { ".flac", "audio/flac" },
        { ".mp4", "video/mp4" }, { ".webm", "video/webm" }, { ".pdf", "application/pdf" }, { ".zip", "application/zip" },
        { ".webmanifest", "application/manifest+json" },
    };
    const std::string ext = lower(fs::path(path).extension().string());
    for (const auto& [e, type] : types) {
        if (ext == e) return type;
    }
    return "application/octet-stream";
}

std::string url_decode(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::string url_encode_path(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : path) {
        const bool plain = std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
        if (plain) out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

ServedFile resolve_served(const std::string& root_folder, const std::string& url) {
    ServedFile out;
    std::string path = url.substr(0, url.find_first_of("?#"));
    path = url_decode(path);
    if (path.empty() || path[0] != '/') path = "/" + path;
    if (path.find('\0') != std::string::npos) {
        out.status = 400;
        return out;
    }
    std::replace(path.begin(), path.end(), '\\', '/');
    const fs::path root = clean_root(root_folder);
    const fs::path target = (root / fs::path(path.substr(1))).lexically_normal();
    if (!inside(target, root)) {
        out.status = 403;
        return out;
    }
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        if (path.back() != '/') {
            out.status = 301;
            out.redirect = url_encode_path(path + "/");
            const std::size_t q = url.find('?');
            if (q != std::string::npos) out.redirect += url.substr(q);
            return out;
        }
        for (const char* index : { "index.html", "index.htm" }) {
            if (fs::is_regular_file(target / index, ec)) {
                out.file = (target / index).string();
                return out;
            }
        }
        out.listing = true;
        out.file = target.string();
        return out;
    }
    if (fs::is_regular_file(target, ec)) {
        out.file = target.string();
        return out;
    }
    if (!target.has_extension()) {
        fs::path html = target;
        html += ".html";
        if (fs::is_regular_file(html, ec)) {
            out.file = html.string();
            return out;
        }
    }
    out.status = 404;
    return out;
}

std::string inject_live_reload(const std::string& html) {
    const std::string script = reload_script();
    const std::string low = lower(html);
    const std::size_t body = low.rfind("</body>");
    if (body != std::string::npos) return html.substr(0, body) + script + html.substr(body);
    const std::size_t end = low.rfind("</html>");
    if (end != std::string::npos) return html.substr(0, end) + script + html.substr(end);
    return html + script;
}

std::string page_script(bool live_reload, bool console) {
    std::string js;
    const std::string_view source(k_page_script);
    js.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            while (i + 1 < source.size() && source[i + 1] == ' ') ++i;
            js += ' ';
            continue;
        }
        js += source[i];
    }
    const auto put = [&js](const std::string& name, bool value) {
        const std::size_t at = js.find(name);
        if (at != std::string::npos) js.replace(at, name.size(), value ? "true" : "false");
    };
    put("__RELOAD__", live_reload);
    put("__CONSOLE__", console);
    return "<script>" + js + "</script>";
}

std::string inject_page_script(const std::string& html, bool live_reload, bool console) {
    if (!live_reload && !console) return html;
    const std::string script = page_script(live_reload, console);
    const std::string low = lower(html);
    std::size_t at = find_tag_end(low, "head");
    if (at == std::string::npos) at = find_tag_end(low, "html");
    if (at == std::string::npos) {
        const std::size_t first = low.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
        if (first != std::string::npos && low.compare(first, 9, "<!doctype") == 0) {
            const std::size_t close = low.find('>', first);
            at = close == std::string::npos ? std::string::npos : close + 1;
        } else {
            at = 0;
        }
    }
    if (at == std::string::npos) return html + script;
    return html.substr(0, at) + script + html.substr(at);
}

bool is_jquery_request(const std::string& url) {
    std::string path = url.substr(0, url.find_first_of("?#"));
    if (path == "/__canopy/jquery.js") return true;
    const std::size_t slash = path.find_last_of('/');
    std::string name = lower(slash == std::string::npos ? path : path.substr(slash + 1));
    if (name.rfind("jquery", 0) != 0) return false;
    if (name.size() < 9 || name.compare(name.size() - 3, 3, ".js") != 0) return false;
    std::string middle = name.substr(6, name.size() - 9);
    for (const char* tail : { ".min", ".slim" }) {
        const std::string t(tail);
        if (middle.size() >= t.size() && middle.compare(middle.size() - t.size(), t.size(), t) == 0) middle.erase(middle.size() - t.size());
    }
    for (const char* tail : { ".min", ".slim" }) {
        const std::string t(tail);
        if (middle.size() >= t.size() && middle.compare(middle.size() - t.size(), t.size(), t) == 0) middle.erase(middle.size() - t.size());
    }
    if (middle.empty()) return true;
    if (middle[0] != '-' && middle[0] != '.') return false;
    return std::all_of(middle.begin() + 1, middle.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; }) && middle.size() > 1;
}

std::vector<ConsoleMessage> parse_console_messages(const std::string& body) {
    std::vector<ConsoleMessage> out;
    std::size_t start = 0;
    while (start < body.size()) {
        std::size_t end = body.find('\x1e', start);
        if (end == std::string::npos) end = body.size();
        std::vector<std::string> fields;
        std::size_t from = start;
        while (fields.size() < 6) {
            const std::size_t sep = body.find('\x1f', from);
            if (sep == std::string::npos || sep > end) break;
            fields.push_back(body.substr(from, sep - from));
            from = sep + 1;
        }
        if (fields.size() == 6) {
            ConsoleMessage m;
            m.level = fields[0];
            m.source = fields[1];
            m.line = static_cast<int>(std::strtol(fields[2].c_str(), nullptr, 10));
            m.column = static_cast<int>(std::strtol(fields[3].c_str(), nullptr, 10));
            m.page = fields[4];
            m.page_id = fields[5];
            m.text = body.substr(from, end - from);
            static const char* const levels[] = { "log", "info", "warn", "error", "debug", "result", "clear" };
            if (std::find_if(std::begin(levels), std::end(levels), [&](const char* l) { return m.level == l; }) == std::end(levels)) m.level = "log";
            out.push_back(std::move(m));
        }
        start = end + 1;
    }
    return out;
}

std::string folder_listing_html(const std::string& root, const std::string& folder, const std::string& url_path) {
    std::vector<std::pair<std::string, bool>> entries;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code dir_ec;
        entries.emplace_back(name, it->is_directory(dir_ec));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second;
        return lower(a.first) < lower(b.first);
    });
    std::string title = url_path.empty() ? std::string("/") : url_path;
    std::string out = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of " + html_escape(title) +
                      "</title><style>body{font-family:sans-serif;padding:20px 28px;}a{text-decoration:none;}li{padding:2px 0;}.d{font-weight:bold;}</style></head><body><h2>Index of " +
                      html_escape(title) + "</h2><ul>";
    if (clean_root(folder) != clean_root(root)) out += "<li><a href=\"../\">../</a></li>";
    for (const auto& [name, dir] : entries) {
        const std::string href = url_encode_path(name) + (dir ? "/" : "");
        out += "<li><a" + std::string(dir ? " class=\"d\"" : "") + " href=\"" + href + "\">" + html_escape(name) + (dir ? "/" : "") + "</a></li>";
    }
    out += "</ul></body></html>";
    return out;
}

struct LiveServer::State {
    std::weak_ptr<State> self;
    std::mutex m;
    fnet::http::Server server;
    std::string folder;
    std::string host;
    std::uint16_t port = 0;
    bool live_reload = true;
    bool running = false;
    std::size_t requests = 0;
    unsigned long long version = 1;
    std::vector<std::string> log;
    bool console = true;
    std::vector<ConsoleMessage> console_messages;
    struct Command {
        long long id = 0;
        std::string code;
        std::string target;
        bool quiet = false;
    };
    std::deque<Command> commands;
    std::map<std::string, unsigned long long> page_order;
    unsigned long long page_serial = 0;
    std::string watch_page;
    std::chrono::steady_clock::time_point watch_until;
    std::map<std::string, PageDom> doms;
    unsigned long long dom_serial = 0;

    void push_command(const std::string& code, const std::string& target, bool quiet) {
        Command c;
        c.id = ++next_command;
        c.code = code;
        c.target = target;
        c.quiet = quiet;
        commands.push_back(std::move(c));
        while (commands.size() > 200) commands.pop_front();
    }

    std::size_t live_count() const {
        const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(3);
        std::size_t n = 0;
        for (const auto& [id, seen] : pages) {
            if (seen.first >= cutoff) ++n;
        }
        return n;
    }
    long long next_command = 0;
    std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> pages;
    std::uint16_t proxy_port = 0;

    void send_jquery(fnet::http::ServerResponse& res, const std::string& url, const std::string& referer, bool console_on) {
        res.set_header("X-Canopy", "bundled jQuery");
        res.send(200, std::string(bundled_jquery()), "text/javascript; charset=utf-8");
        if (!console_on || url.rfind("/__canopy/", 0) == 0) return;
        ConsoleMessage msg;
        msg.level = "info";
        msg.page = referer.empty() ? std::string() : url_page(referer);
        msg.text = std::string("Served Canopy's bundled jQuery ") + bundled_jquery_version() + " for " + url_decode(url.substr(0, url.find_first_of("?#"))) + ", since there's no such file";
        note_console(std::move(msg));
    }

    void forward(fnet::http::IncomingMessage& req, fnet::http::ServerResponse res, const std::string& url, const std::string& method, bool reload, bool console_on, std::uint16_t target) {
        res.remove_header("Cache-Control");
        const fnet::http::Headers incoming = req.headers();
        const std::string referer = req.header("referer");
        std::weak_ptr<State> weak = self;
        auto go = [weak, res, url, method, reload, console_on, target, incoming, referer](const std::error_code& ec, std::string body) mutable {
            if (ec) {
                res.send(400, error_page(400, "Couldn't read the request."), "text/html; charset=utf-8");
                return;
            }
            fnet::http::FetchOptions options;
            options.method = method;
            options.follow_redirects = false;
            options.body = std::move(body);
            options.timeout = std::chrono::milliseconds(60000);
            for (const auto& [name, value] : incoming) {
                const std::string low = lower(name);
                if (low == "host" || low == "accept-encoding" || low == "connection" || low == "content-length" || low == "keep-alive" || low == "upgrade" || low == "transfer-encoding" || low == "proxy-connection") continue;
                options.headers.append(name, value);
            }
            options.headers.set("Host", "127.0.0.1:" + std::to_string(target));
            options.headers.set("Accept-Encoding", "identity");
            fnet::http::fetch("http://127.0.0.1:" + std::to_string(target) + url, options, [weak, res, url, method, reload, console_on, target, referer](const std::error_code& e, fnet::http::FetchResponse r) mutable {
                const std::shared_ptr<State> st = weak.lock();
                if (!st) {
                    res.send(503, "stopped");
                    return;
                }
                if (e) {
                    res.set_header("Cache-Control", "no-store");
                    res.send(502, waiting_page(target), "text/html; charset=utf-8");
                    return;
                }
                if (r.status == 404 && is_jquery_request(url)) {
                    st->send_jquery(res, url, referer, console_on);
                    return;
                }
                const std::string here = "http://127.0.0.1:" + std::to_string(target);
                const std::string here_name = "http://localhost:" + std::to_string(target);
                for (const auto& [name, value] : r.headers) {
                    const std::string low = lower(name);
                    if (low == "content-length" || low == "transfer-encoding" || low == "connection" || low == "content-encoding" || low == "keep-alive" || low == "content-type") continue;
                    std::string v = value;
                    if (low == "location") {
                        if (v.rfind(here, 0) == 0) v = v.substr(here.size());
                        else if (v.rfind(here_name, 0) == 0) v = v.substr(here_name.size());
                        if (v.empty()) v = "/";
                    }
                    res.append_header(name, v);
                }
                const std::string type = r.headers.get("content-type");
                std::string out = std::move(r.body);
                if (lower(type).rfind("text/html", 0) == 0) {
                    out = inject_page_script(out, reload, console_on);
                    if (console_on && method == "GET") st->opened(url);
                }
                if (r.status >= 400 && console_on && !referer.empty() && lower(type).rfind("text/html", 0) != 0) st->missing(method, url, r.status, referer);
                res.send(r.status, out, type.empty() ? std::string("application/octet-stream") : type);
            });
        };
        if (method == "GET" || method == "HEAD" || method == "OPTIONS" || method == "DELETE") go(std::error_code(), std::string());
        else req.body(std::move(go), 64u << 20);
    }

    void note_console(ConsoleMessage message) {
        std::lock_guard<std::mutex> lk(m);
        if (!console) return;
        console_messages.push_back(std::move(message));
        if (console_messages.size() > 2000) console_messages.erase(console_messages.begin(), console_messages.begin() + 1000);
    }

    void opened(const std::string& url) {
        ConsoleMessage msg;
        msg.level = "navigate";
        msg.page = url_page(url);
        note_console(std::move(msg));
    }

    void missing(const std::string& method, const std::string& url, int status, const std::string& referer) {
        if (referer.empty() || url_path_only(url) == "/favicon.ico") return;
        ConsoleMessage msg;
        msg.level = "error";
        msg.page = url_page(referer);
        msg.text = method + " " + url_decode(url.substr(0, url.find_first_of("?#"))) + " " + std::to_string(status) + " (" + fnet::http::status_text(status) + ")";
        if (status == 404) msg.text += ": there's no such file in the served folder";
        note_console(std::move(msg));
    }

    void poll(fnet::http::IncomingMessage& req, fnet::http::ServerResponse& res, unsigned long long now) {
        const fnet::URLSearchParams query = req.query();
        const std::string page = query.get("page");
        const long long after = std::strtoll(query.get("after", "-1").c_str(), nullptr, 10);
        std::string json = "{\"version\":" + json_string(std::to_string(now));
        {
            std::lock_guard<std::mutex> lk(m);
            if (!page.empty()) {
                pages[page] = { std::chrono::steady_clock::now(), query.get("path") };
                if (page_order.find(page) == page_order.end()) page_order[page] = ++page_serial;
                if (pages.size() > 64) {
                    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(30);
                    for (auto it = pages.begin(); it != pages.end();) {
                        if (it->second.first < cutoff) {
                            page_order.erase(it->first);
                            doms.erase(it->first);
                            it = pages.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
            json += ",\"last\":" + std::to_string(next_command) + ",\"commands\":[";
            bool first = true;
            if (after >= 0) {
                for (const Command& c : commands) {
                    if (c.id <= after) continue;
                    if (!first) json += ',';
                    first = false;
                    json += "{\"id\":" + std::to_string(c.id) + ",\"code\":" + json_string(c.code);
                    if (!c.target.empty()) json += ",\"target\":" + json_string(c.target);
                    if (c.quiet) json += ",\"quiet\":true";
                    json += "}";
                }
            }
            if (!page.empty() && page == watch_page && std::chrono::steady_clock::now() < watch_until) json += "],\"watch\":true";
            else json += "]";
        }
        json += "}";
        res.send(200, json, "application/json; charset=utf-8");
    }

    void note(const std::string& line) {
        std::lock_guard<std::mutex> lk(m);
        log.push_back(line);
        if (log.size() > 500) log.erase(log.begin(), log.begin() + 250);
    }

    void handle(fnet::http::IncomingMessage req, fnet::http::ServerResponse res) {
        const std::string method = req.method();
        const std::string url = req.url();
        const std::string path_only = url.substr(0, url.find('?'));
        const bool internal = path_only.rfind("/__canopy/", 0) == 0;
        std::string root;
        bool reload = false;
        bool console_on = false;
        unsigned long long now = 0;
        {
            std::lock_guard<std::mutex> lk(m);
            if (!internal) ++requests;
            root = folder;
            reload = live_reload;
            console_on = console;
            now = version;
        }
        res.set_header("Cache-Control", "no-store");
        if (path_only == k_console_path && method == "POST") {
            req.body([weak = self, res](const std::error_code& ec, std::string body) mutable {
                const std::shared_ptr<State> st = weak.lock();
                if (st && !ec) {
                    for (ConsoleMessage& msg : parse_console_messages(body)) st->note_console(std::move(msg));
                }
                res.send(204, std::string());
            }, 1u << 20);
            return;
        }
        if (path_only == k_dom_path && method == "POST") {
            const std::string page = req.query().get("page");
            req.body([weak = self, res, page](const std::error_code& ec, std::string body) mutable {
                const std::shared_ptr<State> st = weak.lock();
                if (st && !ec && !page.empty()) {
                    std::lock_guard<std::mutex> lk(st->m);
                    if (st->pages.find(page) != st->pages.end()) {
                        PageDom& d = st->doms[page];
                        d.page_id = page;
                        d.serial = ++st->dom_serial;
                        d.tree = std::move(body);
                    }
                }
                res.send(204, std::string());
            }, 32u << 20);
            return;
        }
        if (path_only == k_poll_path && (method == "GET" || method == "HEAD")) {
            poll(req, res, now);
            return;
        }
        if (path_only == "/__canopy/jquery.js") {
            send_jquery(res, url, req.header("referer"), console_on);
            return;
        }
        std::uint16_t target = 0;
        {
            std::lock_guard<std::mutex> lk(m);
            target = proxy_port;
        }
        if (target != 0 && !internal) {
            note(method + " " + url + " \xE2\x86\x92 :" + std::to_string(target));
            forward(req, res, url, method, reload, console_on, target);
            return;
        }
        if (method != "GET" && method != "HEAD") {
            req.discard();
            res.set_header("Allow", "GET, HEAD");
            res.send(405, error_page(405, "Only GET and HEAD work here."), "text/html; charset=utf-8");
            note(method + " " + url + " 405");
            return;
        }
        if (url.substr(0, url.find('?')) == k_reload_path) {
            res.send(200, std::to_string(now), "text/plain; charset=utf-8");
            return;
        }
        const ServedFile served = resolve_served(root, url);
        if (!served.redirect.empty()) {
            res.redirect(served.redirect, served.status);
            note(method + " " + url + " " + std::to_string(served.status));
            return;
        }
        if (served.status == 404 && is_jquery_request(url)) {
            send_jquery(res, url, req.header("referer"), console_on);
            note(method + " " + url + " 200 (bundled jQuery)");
            return;
        }
        if (served.status != 200) {
            const std::string why = served.status == 404 ? "There's no file at " + url_decode(url.substr(0, url.find_first_of("?#"))) + " in this folder." : std::string("That path isn't allowed.");
            res.send(served.status, error_page(served.status, why), "text/html; charset=utf-8");
            note(method + " " + url + " " + std::to_string(served.status));
            if (console_on) missing(method, url, served.status, req.header("referer"));
            return;
        }
        if (served.listing) {
            std::string page = folder_listing_html(root, served.file, url_decode(url.substr(0, url.find_first_of("?#"))));
            page = inject_page_script(page, reload, console_on);
            if (console_on && method == "GET") opened(url);
            res.send(200, page, "text/html; charset=utf-8");
            note(method + " " + url + " 200");
            return;
        }
        std::optional<std::string> body = read_all(served.file);
        if (!body) {
            res.send(500, error_page(500, "Couldn't read " + fs::path(served.file).filename().string() + "."), "text/html; charset=utf-8");
            note(method + " " + url + " 500");
            return;
        }
        const std::string type = mime_type_for(served.file);
        if (type.rfind("text/html", 0) == 0) {
            *body = inject_page_script(*body, reload, console_on);
            if (console_on && method == "GET") opened(url);
        }
        res.send(200, *body, type);
        note(method + " " + url + " 200");
    }
};

LiveServer::LiveServer() : m_state(std::make_shared<State>()) {
    m_state->self = m_state;
}

LiveServer::~LiveServer() {
    stop();
}

bool LiveServer::start(const std::string& folder, std::uint16_t port, const std::string& host, bool live_reload, std::string& error) {
    stop();
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        error = folder + " isn't a folder";
        return false;
    }
    const std::string bind = host.empty() ? std::string("127.0.0.1") : host;
    std::weak_ptr<State> weak = m_state;
    std::string last;
    const std::uint16_t first = port == 0 ? 5500 : port;
    for (unsigned attempt = 0; attempt < 25; ++attempt) {
        const unsigned want = static_cast<unsigned>(first) + attempt;
        if (want > 65535) break;
        fnet::http::Server server = fnet::http::create_server([weak](fnet::http::IncomingMessage req, fnet::http::ServerResponse res) {
            if (const std::shared_ptr<State> st = weak.lock()) st->handle(std::move(req), std::move(res));
            else res.send(503, "stopped");
        });
        server.on_error([weak](const std::error_code& e) {
            if (const std::shared_ptr<State> st = weak.lock()) st->note("error: " + e.message());
        });
        server.on_client_error([](const std::error_code&, fnet::net::Socket socket) { socket.destroy(); });
        server.listen(static_cast<std::uint16_t>(want), bind);
        if (server.listening()) {
            std::lock_guard<std::mutex> lk(m_state->m);
            m_state->server = server;
            m_state->folder = clean_root(folder).string();
            m_state->host = bind;
            m_state->port = server.port() != 0 ? server.port() : static_cast<std::uint16_t>(want);
            m_state->live_reload = live_reload;
            m_state->running = true;
            m_state->requests = 0;
            loop_started();
            return true;
        }
        last = "port " + std::to_string(want) + " is busy";
        server.close();
    }
    error = "Couldn't start a server: " + (last.empty() ? std::string("no free port") : last + " and so are the next ones");
    return false;
}

void LiveServer::stop() {
    fnet::http::Server server;
    {
        std::lock_guard<std::mutex> lk(m_state->m);
        if (!m_state->running) return;
        m_state->running = false;
        server = m_state->server;
        m_state->server = fnet::http::Server();
    }
    if (server) {
        server.close();
        server.close_all_connections();
    }
    loop_stopped();
}

bool LiveServer::running() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    return m_state->running;
}

const std::string& LiveServer::folder() const {
    return m_state->folder;
}

const std::string& LiveServer::host() const {
    return m_state->host;
}

std::uint16_t LiveServer::port() const {
    return m_state->port;
}

bool LiveServer::live_reload() const {
    return m_state->live_reload;
}

std::string LiveServer::url(const std::string& file) const {
    const std::string shown = m_state->host == "0.0.0.0" || m_state->host == "::" || m_state->host == "127.0.0.1" ? std::string("localhost") : m_state->host;
    std::string out = "http://" + shown + ":" + std::to_string(m_state->port) + "/";
    if (file.empty()) return out;
    const fs::path root = clean_root(m_state->folder);
    std::error_code ec;
    fs::path target = fs::absolute(file, ec).lexically_normal();
    if (!inside(target, root)) return out;
    std::string rel = target.lexically_relative(root).generic_string();
    if (rel == ".") rel.clear();
    if (fs::is_directory(target, ec) && !rel.empty() && rel.back() != '/') rel += '/';
    return out + url_encode_path(rel);
}

std::size_t LiveServer::requests() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    return m_state->requests;
}

bool LiveServer::contains(const std::string& path) const {
    std::error_code ec;
    return inside(fs::absolute(path, ec).lexically_normal(), clean_root(m_state->folder));
}

void LiveServer::changed(const std::string& path) {
    if (!running() || !contains(path)) return;
    std::lock_guard<std::mutex> lk(m_state->m);
    ++m_state->version;
}

std::vector<std::string> LiveServer::take_log() {
    std::lock_guard<std::mutex> lk(m_state->m);
    std::vector<std::string> out;
    out.swap(m_state->log);
    return out;
}

void LiveServer::set_console(bool on) {
    std::lock_guard<std::mutex> lk(m_state->m);
    m_state->console = on;
    if (!on) m_state->console_messages.clear();
}

bool LiveServer::console() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    return m_state->console;
}

std::vector<ConsoleMessage> LiveServer::take_console() {
    std::lock_guard<std::mutex> lk(m_state->m);
    std::vector<ConsoleMessage> out;
    out.swap(m_state->console_messages);
    return out;
}

std::size_t LiveServer::evaluate(const std::string& code) {
    std::lock_guard<std::mutex> lk(m_state->m);
    m_state->push_command(code, std::string(), false);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    std::size_t n = 0;
    for (const auto& [id, seen] : m_state->pages) {
        if (seen.first >= cutoff) ++n;
    }
    return n;
}

std::vector<LivePage> LiveServer::live_pages() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    std::vector<std::pair<unsigned long long, LivePage>> found;
    for (const auto& [id, seen] : m_state->pages) {
        if (seen.first < cutoff) continue;
        LivePage p;
        p.id = id;
        p.path = seen.second;
        const auto order = m_state->page_order.find(id);
        found.emplace_back(order == m_state->page_order.end() ? 0 : order->second, std::move(p));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<LivePage> out;
    for (auto& [order, p] : found) out.push_back(std::move(p));
    return out;
}

std::size_t LiveServer::evaluate_in(const std::string& page_id, const std::string& code) {
    std::lock_guard<std::mutex> lk(m_state->m);
    if (page_id.empty()) {
        m_state->push_command(code, std::string(), false);
        return m_state->live_count();
    }
    m_state->push_command(code, page_id, false);
    return m_state->pages.find(page_id) == m_state->pages.end() ? 0 : 1;
}

void LiveServer::page_command(const std::string& page_id, const std::string& code) {
    std::lock_guard<std::mutex> lk(m_state->m);
    m_state->push_command(code, page_id, true);
}

void LiveServer::watch_dom(const std::string& page_id) {
    std::lock_guard<std::mutex> lk(m_state->m);
    if (m_state->watch_page != page_id) m_state->doms.erase(m_state->watch_page);
    m_state->watch_page = page_id;
    m_state->watch_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

void LiveServer::stop_watching_dom() {
    std::lock_guard<std::mutex> lk(m_state->m);
    m_state->doms.erase(m_state->watch_page);
    m_state->watch_page.clear();
}

bool LiveServer::page_dom(const std::string& page_id, unsigned long long after, PageDom& out) const {
    std::lock_guard<std::mutex> lk(m_state->m);
    const auto it = m_state->doms.find(page_id);
    if (it == m_state->doms.end() || it->second.serial <= after) return false;
    out = it->second;
    return true;
}

std::size_t LiveServer::pages() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    std::size_t n = 0;
    for (const auto& [id, seen] : m_state->pages) {
        if (seen.first >= cutoff) ++n;
    }
    return n;
}

std::string LiveServer::file_for_url(const std::string& url) const {
    std::string root;
    {
        std::lock_guard<std::mutex> lk(m_state->m);
        root = m_state->folder;
    }
    if (root.empty() || url.empty()) return std::string();
    const ServedFile served = resolve_served(root, url_path_only(url));
    return served.status == 200 && !served.listing ? served.file : std::string();
}

void LiveServer::set_proxy(std::uint16_t port) {
    std::lock_guard<std::mutex> lk(m_state->m);
    m_state->proxy_port = port;
}

std::uint16_t LiveServer::proxy() const {
    std::lock_guard<std::mutex> lk(m_state->m);
    return m_state->proxy_port;
}

void LiveServer::pump() {
    if (!fnet::background_running()) fnet::poll();
}

} // namespace canopy
