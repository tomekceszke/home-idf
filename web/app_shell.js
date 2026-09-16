/* Shared app shell script (inserted into the app page by tools/render_page.py): API calls with CSRF,
   tabs, swipe control, event and list rendering, formatting. Pages build on the global `hi`. */
const $ = id => document.getElementById(id);
const hi = (() => {
  let csrf = "";
  const pad = n => String(n).padStart(2, "0");

  // GET without a body, POST with one. 401 reloads (the device then serves the sign-in page).
  async function api(path, body, {timeout = 0} = {}) {
    const ctl = timeout ? new AbortController() : null;
    const timer = ctl ? setTimeout(() => ctl.abort(), timeout) : 0;
    const opts = {cache: "no-store", credentials: "same-origin", signal: ctl ? ctl.signal : undefined};
    if (body !== undefined) {
      Object.assign(opts, {method: "POST", body: JSON.stringify(body),
        headers: {"Content-Type": "application/json", "X-CSRF-Token": csrf}});
    }
    try {
      const r = await fetch(path, opts);
      if (r.status === 401) { location.reload(); throw Object.assign(new Error("signed out"), {status: 401}); }
      const data = await r.json().catch(() => ({}));
      if (!r.ok) throw Object.assign(new Error(data.error || `HTTP ${r.status}`), {status: r.status, data});
      return Object.assign(data, {httpStatus: r.status});
    } finally {
      clearTimeout(timer);
    }
  }

  async function session() {
    const s = await api("/api/session");
    if (!s.authenticated) { location.reload(); throw new Error("signed out"); }
    csrf = s.csrf;
    return s;
  }

  // Durations in words, so they never read like a clock time
  const dur = s => {
    s = Math.max(0, Math.round(s));
    const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), r = s % 60;
    return h ? `${h} h ${m} min` : m ? `${m} min ${r} s` : `${r} s`;
  };
  const clock = ts => {
    if (!ts) return "–";
    const d = new Date(ts * 1000);
    return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  };
  const ago = ts => {
    if (!ts) return "";
    const d = Math.round(Date.now() / 1000 - ts);
    if (d < 90) return "just now";
    if (d < 5400) return `${Math.round(d / 60)} min ago`;
    if (d < 86400 * 2) return `at ${clock(ts)}`;
    return new Date(ts * 1000).toLocaleDateString([], {day: "numeric", month: "short"});
  };
  const uptime = s => s < 3600 ? `${Math.round(s / 60)} min` : s < 172800 ? `${Math.round(s / 3600)} h` : `${Math.round(s / 86400)} days`;

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  // items: {ts, what, small, amt, kind: ""|"ok"|"warn"|"bad"}
  function events(ul, items) {
    ul.replaceChildren(...items.map(e => {
      const li = el("li", e.kind || "");
      const what = el("div", "what", e.what);
      if (e.small) what.append(el("small", "", e.small));
      li.append(el("time", "", clock(e.ts)), what, el("div", "amt", e.amt || ""));
      return li;
    }));
  }

  // rows: [label, value, small]
  function rows(ul, list) {
    ul.replaceChildren(...list.map(([k, v, small]) => {
      const li = el("li");
      const kd = el("div", "k", k);
      if (small) kd.append(el("small", "", small));
      li.append(kd, el("div", "v", v));
      return li;
    }));
  }

  function pill(node, kind, text) {
    node.dataset.kind = kind;
    node.lastChild.textContent = text;
  }

  function banner(text, info) {
    const b = $("banner");
    if (!b) return;
    b.hidden = !text;
    b.textContent = text || "";
    b.classList.toggle("info", !!info);
  }

  // Sections .tab#live/#history/#settings, nav.tabs buttons [data-tab], links [data-goto]; the dock shows on Live only
  function tabs(onShow) {
    let current = "live";
    function show(name, initial) {
      current = name;
      document.querySelectorAll(".tab").forEach(t => { t.hidden = t.id !== name; });
      const dock = $("dock");
      if (dock) dock.hidden = name !== "live";
      document.body.classList.toggle("tab-live", name === "live");
      document.querySelectorAll("[data-tab]").forEach(b => {
        if (b.dataset.tab === name) b.setAttribute("aria-current", "page");
        else b.removeAttribute("aria-current");
      });
      window.scrollTo(0, 0);
      if (onShow && !initial) onShow(name);
    }
    document.addEventListener("click", e => {
      const b = e.target.closest("[data-tab],[data-goto]");
      if (b) show(b.dataset.tab || b.dataset.goto);
    });
    show("live", true);
    return {show, get current() { return current; }};
  }

  // Slide the knob to the end: fires once, the moment it gets there (no release, no dialog). A tap does nothing.
  function swipe(root, {text, kind = "", onFire, canStart = () => true}) {
    root.classList.add("swipe");
    root.innerHTML = '<div class="swipe-fill"></div><span class="swipe-text" aria-live="polite"></span><button type="button" class="swipe-knob">›</button>';
    const knob = root.querySelector(".swipe-knob"), label = root.querySelector(".swipe-text");
    const AT = 0.85;
    let drag = null, idle = "", flashTimer = 0;
    const max = () => root.clientWidth - knob.offsetWidth - 12;
    const set = f => { root.style.setProperty("--x", `${Math.round(f * max())}px`); root.style.setProperty("--p", f.toFixed(3)); };
    const fire = () => { if (navigator.vibrate) navigator.vibrate(40); onFire(); };

    knob.addEventListener("pointerdown", e => {
      e.preventDefault();
      if (!canStart()) return;
      knob.setPointerCapture(e.pointerId);
      root.classList.remove("snap");
      drag = {x: e.clientX, fired: false};
    });
    knob.addEventListener("pointermove", e => {
      if (!drag || drag.fired) return;
      const f = Math.min(1, Math.max(0, (e.clientX - drag.x) / max()));
      set(f);
      if (f >= AT) { drag.fired = true; set(1); fire(); }
    });
    const end = () => { if (!drag) return; drag = null; root.classList.add("snap"); set(0); };
    ["pointerup", "pointercancel", "lostpointercapture"].forEach(t => knob.addEventListener(t, end));
    knob.addEventListener("click", e => e.preventDefault());
    knob.addEventListener("contextmenu", e => e.preventDefault());
    knob.addEventListener("keydown", e => {   // keyboard: deliberate by nature
      if ((e.key === "Enter" || e.key === " " || e.key === "ArrowRight") && !e.repeat) {
        e.preventDefault();
        if (canStart()) fire();
      }
    });

    const api = {
      setText(t, k = kind) {
        idle = t;
        root.dataset.kind = k;
        knob.setAttribute("aria-label", t);
        if (!root.dataset.state) label.textContent = t;
      },
      // state: "busy" | "sent" | "error" | null; ms > 0 returns to idle afterwards
      flash(state, t, ms, after) {
        clearTimeout(flashTimer);
        if (state) root.dataset.state = state; else delete root.dataset.state;
        label.textContent = t || idle;
        if (ms) flashTimer = setTimeout(() => api.flash(after ? after() : null, "", 0), ms);
      },
    };
    api.setText(text, kind);
    return api;
  }

  return {api, session, dur, clock, ago, uptime, el, events, rows, pill, banner, tabs, swipe};
})();
