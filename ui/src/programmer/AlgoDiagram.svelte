<script lang="ts">
  // Algorithm diagram derived from the MSFA table (algorithms.ts): carriers on
  // the bottom row, modulators stacked above, the feedback loop drawn. Drawn in
  // a fixed 184 × 196 coordinate system and scaled by the viewBox.
  import { ALGORITHMS } from './algorithms';

  interface Props {
    index: number;
    width: number;
    height: number;
    labels?: boolean;
  }
  let { index, width, height, labels = false }: Props = $props();

  const VW = 184;
  const VH = 196;
  const NW = 26;
  const NH = 20;

  const g = $derived(ALGORITHMS[Math.min(31, Math.max(0, index))]);
  const layout = $derived.by(() => {
    const colPitch = Math.min(VW / g.cols, 34);
    const rowPitch = Math.min((VH - 26) / g.rows, 44);
    const x0 = (VW - g.cols * colPitch) / 2;
    const baseY = VH - 16 - NH / 2;
    const px = (op: number): number => x0 + (g.col[op] + 0.5) * colPitch;
    const py = (op: number): number => baseY - g.row[op] * rowPitch;
    const nodes = [1, 2, 3, 4, 5, 6].map((op) => ({ op, x: px(op), y: py(op), carrier: g.carriers.includes(op) }));
    const edges = g.edges.map(([m, t]) => {
      const mx = px(m);
      const my = py(m) + NH / 2;
      const tx = px(t);
      const ty = py(t) - NH / 2;
      const mid = (my + ty) / 2;
      return mx === tx ? `M${mx} ${my}V${ty}` : `M${mx} ${my}V${mid}H${tx}V${ty}`;
    });
    const cx = g.carriers.map(px);
    const busY = VH - 6;
    const bus =
      `M${Math.min(...cx)} ${busY}H${Math.max(...cx)}` +
      g.carriers.map((c) => `M${px(c)} ${py(c) + NH / 2}V${busY}`).join('');
    const f = g.feedback;
    const fx = px(f.from);
    const fy = py(f.from) + NH / 2;
    const tx = px(f.to);
    const ty = py(f.to) - NH / 2;
    const rx = Math.max(fx, tx) + NW / 2 + 6;
    const fb = `M${fx} ${fy}V${fy + 4}H${rx}V${ty - 5}H${tx}V${ty}`;
    return { nodes, edges, bus, fb };
  });
</script>

<svg {width} {height} viewBox="0 0 {VW} {VH}" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
  <path d={layout.bus} fill="none" stroke="var(--legend)" stroke-width="1.5" vector-effect="non-scaling-stroke" />
  {#each layout.edges as d}
    <path {d} fill="none" stroke="var(--legend)" stroke-width="1.5" vector-effect="non-scaling-stroke" />
  {/each}
  <path d={layout.fb} fill="none" stroke="var(--legend)" stroke-width="1.5" vector-effect="non-scaling-stroke" />
  {#each layout.nodes as n}
    <rect
      x={n.x - NW / 2}
      y={n.y - NH / 2}
      width={NW}
      height={NH}
      rx="2"
      fill={n.carrier ? 'var(--carrier)' : 'var(--modulator)'}
    />
    {#if labels}
      <text x={n.x} y={n.y + 0.5} text-anchor="middle" dominant-baseline="central">{n.op}</text>
    {/if}
  {/each}
</svg>

<style>
  svg {
    display: block;
  }
  text {
    font: 600 13px 'Barlow Condensed', sans-serif;
    fill: var(--body);
  }
</style>
