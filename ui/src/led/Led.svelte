<script lang="ts">
  // 2-digit 7-segment LED (§5.2, §5.4): --led-on on --led-off. The decimal
  // point after the last digit is the EDITED indicator.
  import { ledSegments, segmentPolygons, SEGMENT_NAMES } from './segments';

  interface Props {
    x: number;
    y: number;
    v: number;
    dot: boolean;
  }
  let { x, y, v, dot }: Props = $props();

  const W = 72;
  const H = 46;
  const DIGIT_W = 20;
  const DIGIT_H = 32;
  const T = 4;
  const D0 = 12;
  const D1 = 38;
  const DY = 7;
  const polys = [segmentPolygons(D0, DY, DIGIT_W, DIGIT_H, T), segmentPolygons(D1, DY, DIGIT_W, DIGIT_H, T)];
  const lit = $derived(ledSegments(v));
</script>

<div
  class="led"
  style="left:{x}px;top:{y}px;width:{W}px;height:{H}px"
  role="img"
  aria-label={v > 0 ? `LED ${v}${dot ? '.' : ''}` : 'LED'}
  data-testid="led"
  data-value={v}
  data-dot={dot ? '1' : '0'}
>
  <svg width={W} height={H} viewBox="0 0 {W} {H}" aria-hidden="true">
    <rect x="0" y="0" width={W} height={H} rx="3" fill="var(--body)" />
    <g transform="skewX(-6) translate(4 0)">
      {#each polys as digit, k}
        {#each SEGMENT_NAMES as s}
          <polygon points={digit[s]} fill={lit[k].includes(s) ? 'var(--led-on)' : 'var(--led-off)'} />
        {/each}
      {/each}
      <circle cx={D1 + DIGIT_W + 5} cy={DY + DIGIT_H - 1.5} r="2.2" fill={dot ? 'var(--led-on)' : 'var(--led-off)'} />
    </g>
  </svg>
</div>

<style>
  .led {
    position: absolute;
  }
  svg {
    display: block;
  }
</style>
