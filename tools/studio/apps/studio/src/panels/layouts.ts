// SPDX-License-Identifier: GPL-3.0-only
import type { ModuleDescriptor, ModuleParam } from '../modules/types';
import { inputs, outputs, isChoice, isToggle, hasChoices, SEGMENT_LIMIT } from '../modules/registry';

/**
 * Parameter roles (DESIGN §4.2). A role says what the parameter is *for*;
 * the control and its size follow from it. `panels/<id>.json` may set a role
 * per parameter; otherwise it is inferred from the parameter's own shape.
 */
export type Role =
  | 'hero' | 'primary' | 'secondary' | 'trim'
  | 'mode' | 'toggle' | 'fader' | 'slider' | 'number' | 'readout' | 'select';

export type WidgetKind = 'knob' | 'segmented' | 'toggle' | 'readout' | 'select' | 'fader' | 'slider' | 'number';
export type KnobTier = 'L' | 'M' | 'S';
export type PaneKind = 'wave' | 'envelope' | 'meter' | 'scope' | 'steps' | 'filter' | 'models' | 'channels' | 'groove';
/** Card proportion (DESIGN §7): `tall` stacks displays above controls, `wide` puts them side by side. */
export type CardShape = 'tall' | 'wide';

export interface PanelSection {
  label?: string;
  /** Legacy flag: secondary knobs in a hero section are drawn one tier up (M). */
  hero?: boolean;
  /** Controls per row. Defaults to as many as fit. For a run of sliders: slider columns (default 1). */
  columns?: number;
  /** Controls in one flowing group. Optional when `rows` or `cells` place them instead. */
  params?: string[];
  /** Explicit rows, each centred under the one above (e.g. a hero, then its satellites). Drawn before `params`. */
  rows?: string[][];
  /** Child sections side by side, split by hairlines: an irregular grid. A child may have cells of its own. */
  cells?: PanelSection[];
  /** This cell's share of its parent's width (default 1). Cells with equal shares line up across rows. */
  grow?: number;
  /** `band`: draw this section on an inset surface, set apart from the rest of the card. */
  tone?: 'band';
}

/** Every parameter a section places, at any depth (params, rows and cells). */
export function sectionParams(s: PanelSection): string[] {
  return [...(s.rows ?? []).flat(), ...(s.params ?? []), ...(s.cells ?? []).flatMap(sectionParams)];
}

/** The rows a leaf section draws: its explicit rows, then `params` as one more row. */
export function sectionRows(s: PanelSection): string[][] {
  const rows = (s.rows ?? []).filter(r => r.length);
  return s.params?.length ? [...rows, s.params] : rows;
}

/**
 * A `panels/<module-id>.json` override. Everything is optional: a module
 * with no override still gets a complete working panel.
 */
export interface PanelOverride {
  width?: number;
  /** Header code, e.g. `VCF` (DESIGN §7 header A). Derived from the id when absent. */
  code?: string;
  layout?: CardShape;
  sections?: PanelSection[];
  /** Shorter labels for cramped controls. */
  short?: Record<string, string>;
  roles?: Record<string, Role>;
  /** Force a widget kind (older overrides; `roles` is preferred). */
  widgets?: Record<string, WidgetKind>;
  panes?: PaneKind[];
  /** Free-form options per pane, e.g. which parameters the filter display reads. */
  paneOptions?: Partial<Record<PaneKind, Record<string, unknown>>>;
  /** Parameters to leave off the card (still saved, still modulatable). */
  hidden?: string[];
  /** Modulatable parameters that always have a node on the left rail. Default: hero + primary, at most 2. */
  modPorts?: string[];
  /** Rail options per side: how many ports show when collapsed, which first, editable CC numbers. */
  ports?: Partial<Record<'input' | 'output', { shown?: number; priority?: string[]; assign?: 'cc' }>>;
}

export interface ResolvedLayout {
  width: number;
  /** Minimum height so the side-rail ports keep their pitch. The real height is measured once. */
  minHeight: number;
  shape: CardShape;
  gutterIn: number;
  gutterOut: number;
  sections: PanelSection[];
  short: Record<string, string>;
  roles: Record<string, Role>;
  widgets: Record<string, WidgetKind>;
  tiers: Record<string, KnobTier>;
  panes: PaneKind[];
  paneOptions: Partial<Record<PaneKind, Record<string, unknown>>>;
}

/* ---- geometry (recorded in DESIGN.md "Card sizing") -------------------- */
export const HEADER_H = 36;
const CORE_PAD_X = 12;
const CORE_MIN_W = 150;
const ITEM_GAP = 10;
/** Horizontal slider footprint (label, track, value) and the gap between slider columns. */
export const SLIDER_W = 236;
const SLIDER_GAP = 18;
/** Horizontal padding each grid cell keeps on both sides of its hairline. */
const CELL_PAD = 11;
const BAND_PAD = 12;
/** Footprint of each control, in px. Heights are measured; widths decide the card width. */
const W_TIER: Record<KnobTier, number> = { L: 150, M: 84, S: 58 };
const PANE_MIN_W: Record<PaneKind, number> = { wave: 230, envelope: 240, meter: 160, scope: 220, steps: 430, filter: 220, models: 260, channels: 200, groove: 1060 };

/** Vertical distance between side-rail ports. Touch needs more room. */
export let portMinPitch = 30;
export function setDensity(hit: 'mouse' | 'touch'): void {
  portMinPitch = hit === 'touch' ? 42 : 30;
}

const isTime = (u: string) => u === 's' || u === 'ms';

/** Infer roles for one module, in catalogue order (DESIGN §4.2). */
export function inferRoles(params: ModuleParam[]): Record<string, Role> {
  const out: Record<string, Role> = {};
  let hero = false;
  let primaries = 0;
  for (const p of params) {
    let r: Role;
    if (hasChoices(p) && !isChoice(p)) r = 'readout';
    else if (isToggle(p)) r = 'toggle';
    else if (isChoice(p)) r = p.choices!.length > SEGMENT_LIMIT ? 'select' : 'mode';
    else if (p.unit === 'dB') r = 'fader';
    else if (p.unit === 'BPM' || (p.step >= 1 && !p.modulatable)) r = 'number';
    else if (p.minimum < 0) r = 'trim';
    else if (!hero && p.modulatable && (p.unit === 'Hz' || isTime(p.unit))) { r = 'hero'; hero = true; }
    else r = 'secondary';
    out[p.id] = r;
  }
  // The first two remaining knobs become primaries.
  for (const p of params) {
    if (out[p.id] !== 'secondary' || primaries >= 2) continue;
    out[p.id] = 'primary'; primaries++;
  }
  return out;
}

const ROLE_WIDGET: Record<Role, WidgetKind> = {
  hero: 'knob', primary: 'knob', secondary: 'knob', trim: 'knob',
  mode: 'segmented', toggle: 'toggle', fader: 'fader', slider: 'slider', number: 'number', readout: 'readout', select: 'select',
};
const ROLE_TIER: Partial<Record<Role, KnobTier>> = { hero: 'L', primary: 'M', secondary: 'M', trim: 'S' };

/** Approximate footprint width of one control, for the card width. */
function itemWidth(p: ModuleParam, w: WidgetKind, tier: KnobTier | undefined, label: string): number {
  switch (w) {
    case 'knob': return W_TIER[tier ?? 'M'];
    case 'segmented': return 8 + (p.choices ?? []).reduce((n, c) => n + Math.max(28, 14 + c.label.length * 6.2), 0);
    case 'toggle': return 26 + label.length * 6.4;
    case 'number': return Math.max(96, 30 + label.length * 6);
    case 'select': return 150;
    case 'fader': return 46;
    case 'slider': return SLIDER_W;
    default: return Math.max(90, 20 + label.length * 6);
  }
}

/** Width a port rail needs for its longest label (labels sit inside the card). */
function gutter(labels: string[]): number {
  if (!labels.length) return 14;
  const longest = labels.reduce((n, s) => Math.max(n, s.length), 0);
  return Math.round(Math.min(132, 22 + longest * 5.6));
}

/**
 * Resolve a module descriptor plus an optional override into a concrete
 * card layout. Width is derived from the controls (or set by the override);
 * height is measured once after the card is built, so content is never
 * clipped.
 */
export function resolveLayout(d: ModuleDescriptor, ov: PanelOverride | undefined): ResolvedLayout {
  const hidden = new Set(ov?.hidden ?? []);
  const visible = d.parameters.filter(p => !hidden.has(p.id));
  const inferred = inferRoles(visible);
  const short = ov?.short ?? {};

  const roles: Record<string, Role> = {};
  const widgets: Record<string, WidgetKind> = {};
  const tiers: Record<string, KnobTier> = {};
  for (const p of visible) {
    roles[p.id] = ov?.roles?.[p.id] ?? inferred[p.id];
    widgets[p.id] = ov?.widgets?.[p.id] ?? ROLE_WIDGET[roles[p.id]];
    if (widgets[p.id] === 'knob') tiers[p.id] = ROLE_TIER[roles[p.id]] ?? 'M';
  }

  let sections: PanelSection[] = ov?.sections
    ? ov.sections.map(s => keepVisible(s, id => !hidden.has(id) && visible.some(p => p.id === id)))
        .filter(s => sectionParams(s).length)
    : autoSections(visible.map(p => p.id), roles);

  // Any parameter the override forgot still gets a home, so the panel can
  // never silently drop a control.
  const placed = new Set(sections.flatMap(sectionParams));
  const orphans = visible.filter(p => !placed.has(p.id)).map(p => p.id);
  if (orphans.length) sections = [...sections, { label: 'MORE', params: orphans }];

  const panes = ov?.panes ?? [];
  const shape: CardShape = ov?.layout ?? 'tall';
  // Parameter nodes share the left rail, so their labels count too (owner, 3.5 review).
  const gutterIn = gutter([...inputs(d).map(p => p.name), ...d.parameters.filter(p => p.modulatable).map(p => short[p.id] ?? p.name)]);
  const gutterOut = gutter(outputs(d).map(p => p.name));

  // Width: the widest row of controls, or the widest pane.
  const rowW = (ids: string[], columns: number | undefined): number => {
    if (isSliderRun(ids, widgets)) {
      const n = Math.min(columns ?? 1, ids.length);
      return n * SLIDER_W + SLIDER_GAP * (n - 1);
    }
    const ws = ids.map(id => {
      const p = visible.find(q => q.id === id)!;
      return itemWidth(p, widgets[id], tiers[id], short[id] ?? p.name);
    });
    const per = columns ?? ws.length;
    let w = 0;
    for (let i = 0; i < ws.length; i += per) {
      const row = ws.slice(i, i + per);
      w = Math.max(w, row.reduce((a, b) => a + b, 0) + ITEM_GAP * (row.length - 1));
    }
    return w;
  };
  // A grid row is as wide as its most demanding cell needs, given each cell's share.
  const sectionW = (s: PanelSection): number => {
    const pad = s.tone === 'band' ? 2 * BAND_PAD : 0;
    if (!s.cells?.length) return pad + sectionRows(s).reduce((n, r) => Math.max(n, rowW(r, s.columns)), 0);
    const total = s.cells.reduce((n, c) => n + (c.grow ?? 1), 0);
    return pad + s.cells.reduce((n, c) => Math.max(n, (sectionW(c) + 2 * CELL_PAD) * total / (c.grow ?? 1)), 0);
  };
  let controlsW = CORE_MIN_W;
  for (const s of sections) controlsW = Math.max(controlsW, sectionW(s));
  const panesW = panes.reduce((n, p) => Math.max(n, PANE_MIN_W[p]), 0);
  const coreW = shape === 'wide' && panes.length
    ? panesW + 18 + controlsW
    : Math.max(controlsW, panesW);
  // The header must fit: code · name · id · i, plus a view switch when there is one.
  const headerW = 24 + ((ov?.code ?? d.id.split('.').pop() ?? '').length * 8) + d.name.length * 6.2 + 54
    + (ov?.paneOptions?.filter?.graph ? 96 : 0);
  const width = Math.round(ov?.width ?? Math.max(headerW, gutterIn + gutterOut + coreW + CORE_PAD_X * 2));

  const portRows = Math.max(inputs(d).length, outputs(d).length);
  const minHeight = HEADER_H + (portRows + 1) * portMinPitch;

  return {
    width, minHeight, shape, gutterIn, gutterOut, sections, short, roles, widgets, tiers,
    panes, paneOptions: ov?.paneOptions ?? {},
  };
}

/** A row made only of sliders stacks them in `columns` (default 1) instead of flowing left to right. */
export function isSliderRun(ids: string[], widgets: Record<string, WidgetKind>): boolean {
  return ids.length > 0 && ids.every(id => widgets[id] === 'slider');
}

/** Drop hidden or unknown parameters at any depth, and any row or cell left empty. */
function keepVisible(s: PanelSection, ok: (id: string) => boolean): PanelSection {
  const out: PanelSection = { ...s, params: (s.params ?? []).filter(ok) };
  if (s.rows) out.rows = s.rows.map(r => r.filter(ok)).filter(r => r.length);
  if (s.cells) out.cells = s.cells.map(c => keepVisible(c, ok)).filter(c => sectionParams(c).length);
  return out;
}

/**
 * Group parameters when no override says how (DESIGN §7: hero first,
 * primaries in a row, trims and selectors last). No labels.
 */
function autoSections(ids: string[], roles: Record<string, Role>): PanelSection[] {
  const pick = (...rs: Role[]) => ids.filter(id => rs.includes(roles[id]));
  return [
    { params: pick('hero') },
    { params: pick('primary') },
    { params: pick('secondary', 'fader') },
    { params: pick('trim', 'number', 'readout') },
    { params: pick('mode', 'toggle', 'select') },
  ].filter(s => s.params.length);
}

/* ---- override loading -------------------------------------------------- */

export class PanelOverrides {
  private map = new Map<string, PanelOverride>();

  static async load(base = 'panels'): Promise<PanelOverrides> {
    const o = new PanelOverrides();
    try {
      const res = await fetch(`${base}/index.json`);
      if (!res.ok) return o;
      const list = (await res.json()) as string[];
      await Promise.all(list.map(async id => {
        try {
          const r = await fetch(`${base}/${id}.json`);
          if (r.ok) o.map.set(id, (await r.json()) as PanelOverride);
        } catch { /* a missing override is not an error */ }
      }));
    } catch { /* no overrides at all is fine */ }
    return o;
  }

  get(id: string): PanelOverride | undefined { return this.map.get(id); }
}

