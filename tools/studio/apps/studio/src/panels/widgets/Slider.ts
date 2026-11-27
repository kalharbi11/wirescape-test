// SPDX-License-Identifier: GPL-3.0-only
import type { ModuleParam } from '../../modules/types';
import type { Ticker } from '../../runtime/Ticker';
import { normToValue, quantize, valueToNorm, parseTyped, displayValue } from '../../modules/registry';
import { el } from '../../app/dom';

export interface SliderOptions {
  param: ModuleParam;
  moduleId: number;
  getValue(): number;
  setValue(v: number, commit: boolean): void;
  ticker?: Ticker;
}

const clamp01 = (n: number) => (n < 0 ? 0 : n > 1 ? 1 : n);

/**
 * Horizontal slider: label, hair track and value on one line, for dense banks of
 * related amounts. Same gestures as the fader: press the track to jump there and
 * drag (or drag the thumb, Shift for fine), wheel and arrow keys nudge, double-click
 * resets, click the value to type one. Stepped parameters land on whole steps.
 * The fill starts at zero for bipolar ranges; a rust tick shows a modulated value.
 */
export class Slider {
  readonly root: HTMLElement;
  private readonly track: HTMLElement;
  private readonly fill: HTMLElement;
  private readonly thumb: HTMLElement;
  private readonly ghost: HTMLElement;
  private readonly valueEl: HTMLElement;
  private unbind: () => void = () => {};
  private live: number | undefined;
  private liveBase = NaN;

  constructor(private readonly o: SliderOptions) {
    const p = o.param;
    this.root = el('div', 'control-slider');
    this.root.dataset.param = p.id;
    this.root.dataset.testid = `slider-${o.moduleId}-${p.id}`;

    this.track = el('div', 'slider-track');
    this.fill = el('div', 'slider-fill');
    this.ghost = el('div', 'slider-live');
    this.thumb = el('div', 'slider-thumb');
    this.thumb.tabIndex = 0;
    this.thumb.setAttribute('role', 'slider');
    this.thumb.setAttribute('aria-label', p.name);
    this.thumb.setAttribute('aria-orientation', 'horizontal');
    this.thumb.setAttribute('aria-valuemin', String(p.minimum));
    this.thumb.setAttribute('aria-valuemax', String(p.maximum));
    this.thumb.dataset.testid = `slider-thumb-${o.moduleId}-${p.id}`;
    this.track.append(this.fill, this.ghost, this.thumb);

    this.valueEl = el('div', 'slider-value');
    this.valueEl.title = 'Click to type a value';
    this.valueEl.addEventListener('click', () => this.promptValue());
    this.root.append(el('div', 'control-label slider-label', p.name), this.track, this.valueEl);

    this.track.addEventListener('pointerdown', e => this.onDown(e));
    this.track.addEventListener('dblclick', e => {
      e.preventDefault();
      o.setValue(p.default, true);
      this.refresh();
    });
    this.track.addEventListener('wheel', e => {
      e.preventDefault(); e.stopPropagation();
      this.nudge(e.deltaY > 0 ? -1 : 1, e.shiftKey);
    }, { passive: false });
    this.thumb.addEventListener('keydown', e => {
      const dir = e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1
        : e.key === 'ArrowDown' || e.key === 'ArrowLeft' ? -1 : 0;
      if (dir) { e.preventDefault(); this.nudge(dir, e.shiftKey); }
      else if (e.key === 'Home') { e.preventDefault(); this.o.setValue(p.minimum, true); this.refresh(); }
      else if (e.key === 'End') { e.preventDefault(); this.o.setValue(p.maximum, true); this.refresh(); }
    });

    this.refresh();
    if (o.ticker) {
      this.unbind = o.ticker.bind(o.moduleId, t => {
        const v = t.effective[o.moduleId]?.[p.id];
        this.paintLive(v ?? this.o.getValue());
      });
    }
  }

  destroy(): void { this.unbind(); }

  refresh(): void {
    const p = this.o.param;
    const v = this.o.getValue();
    const n = valueToNorm(p, v);
    const zero = p.minimum < 0 && p.maximum > 0 ? valueToNorm(p, 0) : 0;
    this.fill.style.left = `${Math.min(zero, n) * 100}%`;
    this.fill.style.width = `${Math.abs(n - zero) * 100}%`;
    this.thumb.style.left = `${n * 100}%`;
    const text = displayValue(p, v);
    this.valueEl.textContent = text;
    this.valueEl.dataset.testid = `readout-${this.o.moduleId}-${p.id}`;
    this.thumb.setAttribute('aria-valuenow', String(v));
    this.thumb.setAttribute('aria-valuetext', text);
    this.paintLive(this.root.classList.contains('modded') ? this.live ?? v : v);
  }

  private paintLive(v: number): void {
    const base = this.o.getValue();
    if (v === this.live && base === this.liveBase) return;
    this.live = v;
    this.liveBase = base;
    this.root.classList.toggle('modded', Math.abs(v - base) > 1e-9);
    this.ghost.style.left = `${valueToNorm(this.o.param, v) * 100}%`;
  }

  /** Normalised position to a value; stepped parameters snap to whole steps. */
  private valueAt(n: number): number {
    const p = this.o.param;
    const v = normToValue(p, clamp01(n));
    return p.step && p.step > 0 ? quantize(p, v) : v;
  }

  private nudge(dir: number, fine: boolean): void {
    const p = this.o.param;
    const v = this.o.getValue();
    const next = p.step && p.step > 0
      ? quantize(p, v + dir * p.step)
      : normToValue(p, valueToNorm(p, v) + dir * (fine ? 0.002 : 0.01));
    this.o.setValue(next, true);
    this.refresh();
  }

  private onDown(e: PointerEvent): void {
    if (e.button !== 0) return;
    e.preventDefault(); e.stopPropagation();
    const p = this.o.param;
    this.thumb.focus();
    this.track.setPointerCapture(e.pointerId);
    const box = this.track.getBoundingClientRect();
    const onThumb = e.target === this.thumb;
    const startX = e.clientX;
    const startNorm = valueToNorm(p, this.o.getValue());
    // Pressing the thumb drags relative to it; pressing the track jumps there first.
    const at = (ev: PointerEvent) => onThumb
      ? startNorm + ((ev.clientX - startX) / box.width) * (ev.shiftKey ? 0.2 : 1)
      : (ev.clientX - box.left) / box.width;
    this.root.classList.add('dragging');
    if (!onThumb) { this.o.setValue(this.valueAt(at(e)), false); this.refresh(); }
    const move = (ev: PointerEvent) => {
      if (ev.pointerId !== e.pointerId) return;
      this.o.setValue(this.valueAt(at(ev)), false);
      this.refresh();
    };
    const up = () => {
      this.track.removeEventListener('pointermove', move);
      this.track.removeEventListener('pointerup', up);
      this.track.removeEventListener('pointercancel', up);
      this.root.classList.remove('dragging');
      this.o.setValue(this.o.getValue(), true);
    };
    this.track.addEventListener('pointermove', move);
    this.track.addEventListener('pointerup', up);
    this.track.addEventListener('pointercancel', up);
  }

  private promptValue(): void {
    const p = this.o.param;
    const input = el('input', 'readout-input') as HTMLInputElement;
    input.value = displayValue(p, this.o.getValue());
    input.dataset.testid = `readout-input-${this.o.moduleId}-${p.id}`;
    input.setAttribute('aria-label', `${p.name} value`);
    this.valueEl.replaceWith(input);
    input.focus(); input.select();
    let done = false;
    const finish = (apply: boolean) => {
      if (done) return;
      done = true;
      if (apply) {
        const v = parseTyped(p, input.value, this.o.getValue());
        if (v !== null) this.o.setValue(p.step && p.step > 0 ? quantize(p, v) : v, true);
      }
      input.replaceWith(this.valueEl);
      this.refresh();
    };
    input.addEventListener('keydown', e => {
      e.stopPropagation();
      if (e.key === 'Enter') { e.preventDefault(); finish(true); }
      if (e.key === 'Escape') { e.preventDefault(); finish(false); }
    });
    input.addEventListener('blur', () => finish(true));
    input.addEventListener('pointerdown', e => e.stopPropagation());
  }
}
