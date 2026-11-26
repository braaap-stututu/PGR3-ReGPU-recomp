"use strict"
// The launcher's page: state from the host (Bridge.cs) drawn; every change saved back to settings.ini.
// Ported from the FM3 launcher's app.js, cut to PGR3's tabs (Play, Graphics, Settings).

const $ = (id) => document.getElementById(id)
let state = null
let customFps = false
let nextId = 1
const pending = new Map()
const PAGES = ["play", "graphics", "settings"]
const PRESETS = [
  { value: "off", name: "Off", note: "Everything drawn as the console does." },
  { value: "mild", name: "Mild", note: "The 3D scene drawn at half size (upscaled with FSR 1) and 4x texture filtering." },
  { value: "low", name: "Low", note: "The 3D scene at 35% with a plain one-pass upscale and 2x texture filtering." },
  { value: "extreme", name: "Extreme", note: "The 3D scene at its smallest (33%), plain upscale, 2x texture filtering. The softest." },
  { value: "potato", name: "Potato", note: "Full resolution: faster shaders (same picture), tiny far objects skipped, no texture filtering boost. Made for integrated GPUs." },
  { value: "auto", name: "Auto", note: "Potato on an integrated GPU (built into the processor), Off on a graphics card." },
]
const SWITCHES = ["vsync", "eqaa", "anisotropy", "anisoFromGame", "hideShaderOverlay", "fpsPanel", "timer", "debugServer", "perfLog", "perfLogLight", "vramUploadRing", "drawWorkers", "dxcShaders", "asyncPresent"]

// ---- host calls

if (host) host.addEventListener("message", (e) => {
  const m = e.data
  if (m.event === "benchmark") return benchProgress(m.data)
  const p = pending.get(m.id)
  if (!p) return
  pending.delete(m.id)
  m.ok ? p.resolve(m.result) : p.reject(new Error(m.error))
})

function call(cmd, args) {
  if (!host) return Promise.reject(new Error("Not running inside the launcher."))
  const id = nextId++
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject })
    host.postMessage({ id, cmd, args })
  })
}

// ---- helpers

function el(tag, cls, text) {
  const e = document.createElement(tag)
  if (cls) e.className = cls
  if (text !== undefined) e.textContent = text
  return e
}
function size(bytes) {
  if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + " GB"
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB"
  return Math.max(1, Math.round(bytes / 1024)) + " KB"
}
function toast(text, good) {
  const t = $("toast")
  t.textContent = text
  t.className = "toast" + (good ? " good" : "")
  t.hidden = false
  clearTimeout(toast.timer)
  toast.timer = setTimeout(() => { t.hidden = true }, 5000)
}
function confirmBox(title, text, yes) {
  return new Promise((resolve) => {
    $("confirm-title").textContent = title
    $("confirm-text").textContent = text
    $("confirm-yes").textContent = yes || "OK"
    const box = $("confirm")
    box.hidden = false
    box.onclick = (e) => {
      const c = e.target.dataset && e.target.dataset.choice
      if (!c && e.target !== box) return
      box.hidden = true
      resolve(c === "yes")
    }
  })
}
async function guarded(fn) {
  try { return await fn() } catch (e) { toast(e.message) }
}

// ---- tooltips: any element with data-tip, shown after a short hover

let tipTimer = 0
let tipFor = null
document.addEventListener("mouseover", (e) => {
  const t = e.target.closest && e.target.closest("[data-tip]")
  if (t === tipFor) return
  hideTip()
  if (!t) return
  tipFor = t
  tipTimer = setTimeout(() => showTip(t), 450)
})
document.addEventListener("mousedown", hideTip)
document.addEventListener("scroll", hideTip, true)
function showTip(t) {
  const tip = $("tooltip")
  tip.textContent = t.dataset.tip
  tip.hidden = false
  const r = t.getBoundingClientRect()
  const w = tip.offsetWidth, h = tip.offsetHeight
  let x = r.left + Math.min(r.width / 2, 120) - 20
  let y = r.bottom + 8
  if (y + h > innerHeight - 8) y = r.top - h - 8
  x = Math.max(8, Math.min(x, innerWidth - w - 8))
  tip.style.left = x + "px"
  tip.style.top = y + "px"
  requestAnimationFrame(() => tip.classList.add("on"))
}
function hideTip() {
  clearTimeout(tipTimer)
  tipFor = null
  const tip = $("tooltip")
  tip.classList.remove("on")
  tip.hidden = true
}

// ---- tabs

function showPage(name) {
  for (const p of PAGES) $("page-" + p).hidden = p !== name
  $("tabs").dataset.shown = name
}
$("tabs").onclick = (e) => {
  const b = e.target.closest("button")
  if (!b) return
  for (const t of $("tabs").children) t.classList.toggle("on", t === b)
  showPage(b.dataset.tab)
  if (b.dataset.tab === "settings") refresh()
}

// ---- drawing

function fill(select, items, value) {
  select.replaceChildren()
  for (const it of items) {
    const o = el("option", "", it.name)
    o.value = String(it.value)
    select.append(o)
  }
  select.value = String(value)
  if (select.selectedIndex < 0 && select.options.length) select.selectedIndex = select.options.length - 1
}

function render() {
  const s = state, set = s.settings, o = s.options
  $("version").textContent = "Version " + s.version + " · native PC port"

  const chips = $("chips")
  chips.replaceChildren()
  const chip = (cls, text, tip) => { const c = el("span", "chip " + cls); c.append(el("span", "dot"), text); if (tip) c.dataset.tip = tip; chips.append(c) }
  chip(s.checks.game ? "ok" : "bad", s.checks.game ? "Game files" : "Game files missing", "The disc's files in the game folder.")
  chip(s.checks.running ? "warn" : "ok", s.checks.running ? "Game running" : "Not running")

  // graphics page
  const gpus = [{ value: "", name: "Automatic (Windows' choice)" }, ...o.gpus]
  if (set.gpu && !gpus.some((g) => g.value === set.gpu)) gpus.push({ value: set.gpu, name: set.gpu + " (not found)" })
  fill($("gpu"), gpus, set.gpu || "")
  fill($("resolution"), o.resolutions.map((r) => ({ value: r, name: r.replace("x", " × ") })), set.resolution)
  fill($("aspect"), o.aspects, set.aspect)
  const custom = customFps || !o.fps.includes(set.maxFps)
  fill($("maxFps"), [...o.fps.map((f) => ({ value: f, name: f + " fps" })), { value: "custom", name: "Custom…" }], custom ? "custom" : set.maxFps)
  $("customFpsField").hidden = !custom
  if (document.activeElement !== $("customFps")) $("customFps").value = set.maxFps
  fill($("msaa"), o.msaa, set.msaa)
  fill($("aa"), o.aa, set.aa)
  fill($("ssaa"), o.ssaa, set.ssaa)
  const noSsaa = set.upscaler !== "off"
  $("ssaa").disabled = noSsaa
  $("ssaaField").classList.toggle("off", noSsaa)
  $("eqaaRow").classList.toggle("off", set.msaa <= 1)
  fill($("upscaler"), o.upscalers, set.upscaler)
  // FSR 3/4 and XeSS do their own anti-aliasing: pgr3.exe turns MSAA off under them (src/settings.cpp).
  const fsrAa = set.upscaler === "fsr3" || set.upscaler === "fsr4" || set.upscaler === "xess"
  const mf = $("msaaField")
  if (!mf.dataset.tip0) mf.dataset.tip0 = mf.dataset.tip
  $("msaa").disabled = fsrAa
  mf.classList.toggle("off", fsrAa)
  mf.dataset.tip = fsrAa ? "Off while FSR 3, FSR 4 or XeSS is on: it handles anti-aliasing itself." : mf.dataset.tip0
  fill($("upscale"), o.upscale, set.upscale)
  fill($("sharpening"), o.sharpening, set.sharpening)
  $("upscaleField").hidden = set.upscaler === "off"
  $("sharpeningField").hidden = set.upscaler === "off" || set.upscaler === "bilinear"
  for (const b of $("display").children) b.classList.toggle("on", (b.dataset.full === "1") === set.fullscreen)
  // Each renderer is its own pgr3.exe build: a missing one can't be picked (PGR3_EXE set: the launcher runs that one).
  for (const b of $("renderer").children) {
    b.classList.toggle("on", b.dataset.renderer === set.renderer)
    b.disabled = s.checks.exeFromEnv || (b.dataset.renderer !== set.renderer && !s.checks[b.dataset.renderer])
  }
  $("rendererField").classList.toggle("off", s.checks.exeFromEnv)
  for (const k of SWITCHES) $(k).classList.toggle("on", !!set[k])
  const preset = Math.max(0, PRESETS.findIndex((p) => p.value === set.perfPreset))
  if (document.activeElement !== $("perfPreset")) $("perfPreset").value = preset
  $("perfPreset-v").textContent = PRESETS[preset].name
  $("perfPreset-note").textContent = PRESETS[preset].note

  // play page
  fill($("language"), o.languages, set.language)
  const named = (list, v) => (list.find((m) => m.value === v) || { name: "Off" }).name
  const aspect = o.aspects.find((a) => a.value === set.aspect)
  $("summary").replaceChildren()
  const line = (label, value) => { const d = el("div"); d.append(label + ": "); d.append(el("b", "", value)); $("summary").append(d) }
  line("Profile", set.profile || "Player")
  line("Display", `${set.resolution.replace("x", " × ")}, ${set.fullscreen ? "fullscreen" : "windowed"}${aspect && set.aspect !== "auto" ? ", " + aspect.name : ""}`)
  line("Renderer", s.checks.exeFromEnv ? "PGR3_EXE" : set.renderer === "d3d12" ? "Direct3D 12" : "Vulkan")
  if (set.gpu) line("Graphics card", set.gpu)
  line("Frame rate", `${set.maxFps} fps cap, VSync ${set.vsync ? "on" : "off"}`)
  const aaParts = [fsrAa ? named(o.upscalers, set.upscaler).split(" (")[0] : `MSAA ${named(o.msaa, set.msaa)}`]
  if (set.aa !== "off") aaParts.push(named(o.aa, set.aa).split(" (")[0])
  if (set.ssaa !== "1" && set.upscaler === "off") aaParts.push(`SSAA ${set.ssaa}x`)
  if (set.eqaa && set.msaa > 1) aaParts.push("EQAA")
  line("Anti-aliasing", aaParts.join(", "))
  if (set.upscaler !== "off") line("Upscaling", `${named(o.upscalers, set.upscaler)}, ${named(o.upscale, set.upscale)}`)
  if (preset) line("Speed preset", PRESETS[preset].name)
  const testing = [set.timer ? "timer" : "", set.debugServer ? "debug server" : "", set.perfLog ? "performance log" : "", set.perfLogLight ? "light performance log" : ""].filter(Boolean)
  if (testing.length) line("Testing", testing.join(", "))

  const cl = $("checklist")
  cl.replaceChildren()
  const item = (ok, text) => { const d = el("div", "check-item " + (ok ? "ok" : "bad")); d.append(el("span", "dot"), text); cl.append(d) }
  const rname = set.renderer === "d3d12" ? "Direct3D 12" : "Vulkan"
  item(s.checks.exe, s.checks.exe ? `pgr3.exe found (${rname})` : `The ${rname} pgr3.exe is missing: build it first (README.md)`)
  item(s.checks.game, s.checks.game ? "Game files found" : "Game files missing: choose the game folder")
  item(s.shaders.exists, s.shaders.exists ? `Shaders compiled (${s.shaders.shaders.toLocaleString()})` : "The first start compiles shaders (a few minutes)")
  if (!benchBusy) {
    $("launch").disabled = !s.checks.exe || !s.checks.game || s.checks.running
    $("bench-run").disabled = !s.checks.exe || !s.checks.game || s.checks.running || !s.checks.benchmark
  }
  $("launch").textContent = s.checks.running ? "Running" : "Play"
  if (!s.checks.benchmark) $("bench-hint").textContent = "The benchmark needs the test route's profile (out\\bench\\baseline and out\\user-route, made by tools\\benchmark.ps1 in a source checkout)."

  // game tile
  $("gameFolder").value = s.paths.gameDir
  $("gameFolder").placeholder = s.paths.gameDefault
  for (const id of ["gameBrowse", "gameReset"]) {
    $(id).disabled = s.paths.gameFromEnv
    if (s.paths.gameFromEnv) $(id).dataset.tip = "PGR3_GAME_DIR is set: it decides the game folder."
  }
  renderTile()

  // settings page
  const st = $("shader-stats")
  st.replaceChildren()
  const stat = (n, l) => { const d = el("div", "stat"); d.append(el("div", "n", n), el("div", "l", l)); st.append(d) }
  stat(s.shaders.exists ? s.shaders.shaders.toLocaleString() : "0", "compiled shaders")
  stat(s.shaders.exists ? size(s.shaders.bytes) : "-", "on disk")
  stat(s.shaders.built ? new Date(s.shaders.built).toLocaleDateString() : "-", "pipelines saved")
  stat(s.shaders.exists ? "Ready" : "Empty", "state")
  $("shaders-delete").disabled = !s.shaders.exists || s.checks.running

  const f = $("folders")
  f.replaceChildren()
  for (const [k, v] of [["Install", s.paths.root], ["Game", s.paths.exe], ["Game files", s.paths.gameDir], ["User data", s.paths.user], ["Log", s.paths.log]])
    f.append(el("div", "k", k), el("div", "v", v))
}

// ---- the Game tile

let tileBusy = null
let tileTimer = 0
function tileSet(cls, text) {
  const t = $("tile-game")
  tileBusy = cls
  t.classList.remove("busy", "done", "err", "indet")
  t.classList.add(cls)
  if (cls === "busy") t.classList.add("indet")
  const s = $("tile-game-status")
  s.textContent = text
  s.className = "tile-status " + (cls === "done" ? "ok" : cls === "err" ? "err" : "")
}
function tileReset() {
  clearTimeout(tileTimer)
  tileBusy = null
  $("tile-game").classList.remove("busy", "done", "err", "indet")
  renderTile()
}
function tileDone(text, good) {
  tileSet(good ? "done" : "err", (good ? "✓ " : "✕ ") + text)
  clearTimeout(tileTimer)
  tileTimer = setTimeout(tileReset, good ? 7000 : 12000)
}
function renderTile() {
  if (tileBusy) return
  const c = state.checks, x = c.xex
  const e = $("tile-game-status")
  e.textContent = !c.game ? "No game files yet: choose the folder with the disc's files"
    : x === "ok" ? "✓ Project Gotham Racing 3 (Europe) ✓"
    : x === "mismatch" ? "⚠ default.xex doesn't match the one this port is built from"
    : "✓ Game files found (not checked yet)"
  e.className = "tile-status " + (!c.game ? "bad" : x === "mismatch" ? "err" : "ok")
}
async function runTile(fn) {
  if (tileBusy === "busy") return toast("Still working on the last one.")
  tileSet("busy", "Checking…")
  try { await fn() } catch (e) { tileDone(e.message, false) }
}

// ---- settings changes

let saveTimer = 0
function change(patch) {
  Object.assign(state.settings, patch)
  render()
  clearTimeout(saveTimer)
  saveTimer = setTimeout(() => guarded(async () => {
    try { state = await call("save", state.settings) } finally { saveTimer = 0 }
    render()
  }), 250)
}
$("resolution").onchange = (e) => change({ resolution: e.target.value })
$("aspect").onchange = (e) => change({ aspect: e.target.value })
$("maxFps").onchange = (e) => {
  customFps = e.target.value === "custom"
  if (customFps) { render(); $("customFps").focus() } else change({ maxFps: +e.target.value })
}
$("customFps").onchange = (e) => {
  const f = Math.min(360, Math.max(30, Math.round(+e.target.value) || 60))
  e.target.value = f
  change({ maxFps: f })
}
$("msaa").onchange = (e) => change({ msaa: +e.target.value })
$("aa").onchange = (e) => change({ aa: e.target.value })
$("ssaa").onchange = (e) => change({ ssaa: e.target.value })
$("gpu").onchange = (e) => change({ gpu: e.target.value })
for (const b of $("renderer").children) b.onclick = () => { if (!b.disabled) change({ renderer: b.dataset.renderer }) }
$("perfPreset").oninput = (e) => { const p = PRESETS[+e.target.value]; $("perfPreset-v").textContent = p.name; $("perfPreset-note").textContent = p.note }
$("perfPreset").onchange = (e) => change({ perfPreset: PRESETS[+e.target.value].value })
$("upscaler").onchange = (e) => change({ upscaler: e.target.value })
$("upscale").onchange = (e) => change({ upscale: +e.target.value })
$("sharpening").onchange = (e) => change({ sharpening: +e.target.value })
$("language").onchange = (e) => change({ language: +e.target.value })
$("display").onclick = (e) => { const b = e.target.closest("button"); if (b) change({ fullscreen: b.dataset.full === "1" }) }
for (const k of SWITCHES) $(k).onclick = () => change({ [k]: !state.settings[k] })

$("gameBrowse").onclick = () => runTile(async () => {
  const before = state.paths.gameDir
  state = await call("game.browse")
  render()
  if (state.paths.gameDir === before) return tileReset()
  tileDone("Game folder set: default.xex matches.", true)
})
$("gameReset").onclick = () => guarded(async () => { state = await call("game.reset"); render() })
$("gameCheck").onclick = () => runTile(async () => {
  state = await call("xex.check")
  render()
  const x = state.checks.xex
  if (x === "ok") tileDone("default.xex matches Project Gotham Racing 3 (Europe).", true)
  else if (x === "missing") tileDone("There is no default.xex in the game folder.", false)
  else tileDone("default.xex doesn't match the one this port is built from.", false)
})

// ---- actions

$("launch").onclick = () => guarded(async () => {
  const status = $("status")
  $("launch").disabled = true
  status.className = "status"
  status.textContent = state.shaders.exists ? "Starting…" : "Starting: the first start compiles shaders."
  try {
    await call("launch", { settings: { ...state.settings } })
    status.className = "status done"
    status.textContent = "Game started."
    setTimeout(refresh, 2500)
  } catch (e) {
    status.className = "status error"
    status.textContent = e.message
    $("launch").disabled = false
  }
})

$("shaders-delete").onclick = () => guarded(async () => {
  if (!await confirmBox("Delete the shader cache?", "The next start compiles every shader again (a few minutes). Saves and settings are kept.", "Delete")) return
  state = await call("shaders.delete")
  render()
  toast("Shader cache deleted.", true)
})

// ---- benchmark race (Benchmark.cs): the AI drives, the game measures and closes by itself

let benchBusy = false
function benchProgress(d) { if (benchBusy) $("bench-status").textContent = d.text }
function renderBench(list, latest) {
  const t = $("bench-table")
  t.textContent = ""
  if (!list || !list.length) return
  const head = el("tr")
  for (const h of ["When", "Resolution", "Cap", "Avg fps", "Lowest 1 s", "p99 ms", "Worst ms", "GPU ms", "Options"]) head.append(el("th", null, h))
  t.append(head)
  list.forEach((r, i) => {
    const row = el("tr", i === 0 && latest ? "latest" : null)
    const cells = [r.time.slice(5, 16), `${r.resolution} ${r.msaa}x`, String(r.maxFps), r.fpsAvg.toFixed(1), r.fpsLowestSecond.toFixed(0), r.p99Ms.toFixed(2),
      r.worstMs.toFixed(1), r.gpuMs.toFixed(2)]
    for (const c of cells) row.append(el("td", null, c))
    row.append(el("td", "opts", r.options || "—"))
    t.append(row)
  })
}
async function loadBench() { await guarded(async () => renderBench(await call("benchmark.list"), false)) }
$("bench-run").onclick = () => guarded(async () => {
  const status = $("bench-status")
  benchBusy = true
  $("bench-run").disabled = true
  $("launch").disabled = true
  status.className = "status"
  status.textContent = "Starting…"
  try {
    const r = await call("benchmark.run", { seconds: Number($("bench-seconds").value) })
    status.className = "status done"
    status.textContent = `${r.result.fpsAvg.toFixed(1)} fps on average (lowest second ${r.result.fpsLowestSecond.toFixed(0)}, p99 ${r.result.p99Ms.toFixed(2)} ms).`
    renderBench(r.history, true)
  } catch (e) {
    status.className = "status error"
    status.textContent = e.message
  } finally {
    benchBusy = false
    refresh()
  }
})

for (const b of document.querySelectorAll("[data-open]")) b.onclick = () => guarded(() => call("open", b.dataset.open))
// Settings changed elsewhere (an edited settings.ini) are picked up when the window comes back into focus.
window.addEventListener("focus", () => {
  if (saveTimer || ["SELECT", "INPUT"].includes(document.activeElement.tagName)) return
  refresh()
})

async function refresh() {
  await guarded(async () => { state = await call("state"); render() })
}
refresh().then(loadBench)
