/*
 * harness.mjs — horde 2's engine harness as a robustness tool uses it. ROADMAP
 * B448 phase 2, P2 (ADR-209). The guide is h2/harness/README.md.
 *
 * A tool written on this file knows no engine. It asks for the enrolled engines
 * and their reference patches, says how to render (rate, host block size, seed,
 * length), and gets back the output and the pre-limiter bus:
 *
 *   import { enrolled, patchesFor, render, features } from '../h2/harness/harness.mjs';
 *   for (const engine of enrolled())
 *     for (const patch of patchesFor(engine)) {
 *       const r = render({ patch, sr: 96000, block: 7, seed: 1, seconds: 2, pre: true });
 *       // r.L r.R (the output), r.preL r.preR (the limiter's input), r.summary.digest
 *       const f = features(r, { from: 0.25, to: 1.25 });
 *       if (f.silent) fail(`${engine}/${patch.name}: silent`);   // the tool's own check goes here
 *     }
 *
 * The rendering is done by the C++ binary h2_harness_render (h2/harness/render_cli.cpp),
 * which holds one adapter per enrolled engine. This file writes it a job file and
 * reads its output file; many jobs go through one process (run()).
 *
 * FEATURES ARE tools/patchspace/metrics.mjs's, and only those: the proposal forbids a
 * second metrics library (section 5), and tools/auhost/analyse.mjs is the precedent
 * for measuring a C++ render with it. features() adds no number of its own.
 *
 * DETERMINISM. No clock and no randomness here. The temporary directory's name is
 * the one thing that differs between runs, and it never reaches a result.
 *
 * NOT under the lab sandbox (tools/labharness/sandbox_guard.mjs), on purpose: this
 * file evaluates no lab or packet text (it parses two JSON files of ours and runs
 * our own binary), and the sandbox forbids the child process it needs.
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { analyse } from '../../tools/patchspace/metrics.mjs';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
export const REGISTRY = 'docs/armor/engines.json';
export const PATCHES = 'h2/harness/patches.json';
// verify full's build directory; H2_HARNESS_BIN or run()'s `bin` option overrides it
export const DEFAULT_BIN = join(ROOT, 'build-release', 'h2_harness_render');
export const mtof = n => 440 * Math.pow(2, (n - 69) / 12);

const readJson = rel => JSON.parse(readFileSync(join(ROOT, rel), 'utf8'));

/* ---------------------------------------------------------------- what is enrolled */
export const loadRegistry = () => readJson(REGISTRY);
export const loadPatches = () => readJson(PATCHES);
/* the adapter ids of every enrolled engine and module, in registry order */
export function enrolled() {
  return loadRegistry().units.filter(u => u.kind === 'engine' || u.kind === 'module').map(u => u.id);
}
export function patchesFor(engine) { return loadPatches().patches.filter(p => p.engine === engine); }
export function patch(id) {
  const p = loadPatches().patches.find(q => q.id === id);
  if (!p) throw new Error(`harness: no reference patch "${id}" in ${PATCHES}`);
  return p;
}

/* ---------------------------------------------------------------- building a job */
const setCmd = ([k, v]) => (typeof v === 'string' ? ['sets', k, v] : ['set', k, v]);
/* A patch as commands, played the way patches.json says: its sets, then the caller's
   overrides (so an override starts AT its value, like the patch's own), then snap,
   then the cell's sets. */
export function patchCmds(p, overrides) {
  const cell = loadPatches().cell;
  return [...p.sets.map(setCmd), ...Object.entries(overrides || {}).map(setCmd), ['snap'], ...cell.sets.map(setCmd)];
}
/* One render request -> a job.
     patch      a reference patch (an object from patchesFor / patch(), or its id)
     engine     the adapter id; defaults to the patch's
     sr         sample rate (48000)        seed   the host random stream (1)
     block      host block size in frames (128), or { vary: max } for sizes drawn 1..max
     seconds    total length (1)           hold   seconds until the notes are released
                                                  (default: held to the end)
     notes vel  default to the cell's (the ledger's 8-voice chord)
     overrides  { key: value } set after the patch, before snap
     pre        also return the pre-limiter bus (false)
     cmds       REPLACES the default script after `begin` (patchCmds, notes, render):
                a tool that sweeps parameters or churns notes writes its own, from
                the command words in harness.h, and may start from patchCmds()   */
export function jobFor(req) {
  const p = typeof req.patch === 'string' ? patch(req.patch) : req.patch;
  const cell = loadPatches().cell;
  const sr = req.sr || 48000, seconds = req.seconds === undefined ? 1 : req.seconds;
  const block = req.block === undefined ? 128 : req.block;
  const job = {
    name: req.name || `${p ? p.id : 'script'} sr=${sr} block=${typeof block === 'object' ? 'vary' + block.vary : block} seed=${req.seed === undefined ? 1 : req.seed}`,
    engine: req.engine || (p && p.engine), sr, seed: (req.seed === undefined ? 1 : req.seed) >>> 0,
    block: typeof block === 'object' ? block.vary : block, vary: typeof block === 'object', pre: !!req.pre,
  };
  if (!job.engine) throw new Error('harness: a job needs an engine (or a patch that names one)');
  if (req.cmds) { job.cmds = req.cmds; return job; }
  if (!p) throw new Error('harness: a job needs a patch or its own cmds');
  const notes = req.notes || cell.notes, vel = req.vel === undefined ? cell.vel : req.vel;
  const total = Math.round(seconds * sr), held = req.hold === undefined ? total : Math.min(total, Math.round(req.hold * sr));
  job.cmds = [...patchCmds(p, req.overrides), ...notes.map(n => ['on', n, mtof(n), vel]), ['render', held]];
  if (held < total) job.cmds.push(...notes.map(n => ['off', n]), ['render', total - held]);
  return job;
}

/* the job language (harness.h). Numbers are JS's shortest round-trip spelling, which
   strtod reads back as the same double. */
export function jobText(jobs) {
  const out = ['H2HARNESS 1'];
  for (const j of jobs) {
    if (/[\r\n]/.test(j.name)) throw new Error('harness: a job name cannot hold a line break');
    out.push(`JOB ${j.name}`, `engine ${j.engine}`, `sr ${j.sr}`, `seed ${j.seed}`,
      `block ${j.vary ? 'vary ' : ''}${j.block}`, `pre ${j.pre ? 1 : 0}`, 'begin');
    for (const c of j.cmds) {
      if (c[0] === 'sets' && /[\r\n]/.test(c[2])) throw new Error('harness: a string value cannot hold a line break');
      out.push(c.join(' '));
    }
    out.push('END');
  }
  return out.join('\n') + '\n';
}

/* ---------------------------------------------------------------- running jobs */
/* the output file (render_cli.cpp's header) -> one result per job */
export function parseOutput(buf) {
  let p = 0;
  const line = () => {
    const e = buf.indexOf(0x0a, p);
    if (e < 0) throw new Error('harness: the output file ends mid-line (the render was cut short)');
    const s = buf.toString('utf8', p, e); p = e + 1; return s;
  };
  if (line() !== 'H2HARNESS-OUT 1') throw new Error('harness: not an h2_harness_render output file');
  const results = [];
  for (;;) {
    const head = line();
    if (head.startsWith('END ')) {
      if (Number(head.slice(4)) !== results.length) throw new Error(`harness: END says ${head.slice(4)} jobs, read ${results.length}`);
      return results;
    }
    if (!head.startsWith('JOB ')) throw new Error(`harness: expected JOB, read "${head.slice(0, 60)}"`);
    const chunks = [];
    let frames = 0, channels = 0, summary = null;
    for (;;) {
      const l = line();
      if (l.startsWith('DONE ')) { summary = JSON.parse(l.slice(5)); break; }
      const m = /^CHUNK (\d+) (\d+)$/.exec(l);
      if (!m) throw new Error(`harness: expected CHUNK or DONE, read "${l.slice(0, 60)}"`);
      const n = Number(m[1]), ch = Number(m[2]), bytes = n * ch * 8;
      if (p + bytes > buf.length) throw new Error('harness: the output file ends inside a chunk');
      // copied, so the Float64Array is aligned whatever the chunk's offset in the file
      chunks.push([n, new Float64Array(buf.buffer.slice(buf.byteOffset + p, buf.byteOffset + p + bytes))]);
      p += bytes; frames += n; channels = ch;
    }
    if (summary.frames !== frames) throw new Error(`harness: job "${head.slice(4)}" says ${summary.frames} frames, read ${frames}`);
    const r = { name: head.slice(4), summary, sr: summary.sr, L: new Float64Array(frames), R: new Float64Array(frames), preL: null, preR: null };
    if (channels === 4) { r.preL = new Float64Array(frames); r.preR = new Float64Array(frames); }
    let at = 0;
    for (const [n, d] of chunks) {
      for (let i = 0; i < n; i++) {
        r.L[at + i] = d[i * channels]; r.R[at + i] = d[i * channels + 1];
        if (channels === 4) { r.preL[at + i] = d[i * channels + 2]; r.preR[at + i] = d[i * channels + 3]; }
      }
      at += n;
    }
    results.push(r);
  }
}

export function binPath(opt) { return (opt && opt.bin) || process.env.H2_HARNESS_BIN || DEFAULT_BIN; }

/* Runs jobs through one h2_harness_render process; results are in job order. Throws
   if the binary is missing or refuses the request: a job that did not run must
   never read as a job that passed. */
export function run(jobs, opt) {
  const bin = binPath(opt);
  if (!existsSync(bin)) throw new Error(`harness: ${bin} is not built (see h2/harness/README.md, "Building")`);
  const dir = mkdtempSync(join(tmpdir(), 'h2harness-'));
  try {
    const jf = join(dir, 'jobs.txt'), of = join(dir, 'out.bin');
    writeFileSync(jf, jobText(jobs));
    const r = spawnSync(bin, ['--jobs', jf, '--out', of], { encoding: 'utf8' });
    if (r.status !== 0) throw new Error(`harness: h2_harness_render exited ${r.status}: ${(r.stderr || '').trim() || r.error}`);
    return parseOutput(readFileSync(of));
  } finally { rmSync(dir, { recursive: true, force: true }); }
}
export function render(req, opt) { return run([jobFor(req)], opt)[0]; }

/* the binary's own adapter table: [{ id, control }] */
export function listAdapters(opt) {
  const r = spawnSync(binPath(opt), ['--list'], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error(`harness: h2_harness_render --list exited ${r.status}`);
  return r.stdout.trim().split('\n').map(l => { const [id, kind] = l.split(' '); return { id, control: kind === 'control' }; });
}

/* ---------------------------------------------------------------- shared features */
/* metrics.mjs's analyse() over [from, to) seconds of a result's OUTPUT (the whole
   render by default). noteHz is the note root() is asked about; the default is the
   lowest note of the cell's chord (analyse.mjs: "a chord's bass note is the honest
   choice"). What metrics.mjs has and has not is listed in the README. */
export function features(r, opt) {
  opt = opt || {};
  const a = Math.round((opt.from || 0) * r.sr), z = opt.to === undefined ? r.L.length : Math.min(r.L.length, Math.round(opt.to * r.sr));
  const noteHz = opt.noteHz || mtof(Math.min(...loadPatches().cell.notes));
  const m = analyse(r.L.subarray(a, z), r.R.subarray(a, z), r.sr, noteHz);
  delete m._S;
  return m;
}
