<script lang="ts">
  // §5.9: the UI is replaced by a centred message naming the problem and the fix.
  interface Props {
    message: string;
  }
  let { message }: Props = $props();
</script>

<div class="fatal" role="alert" data-testid="fatal-error">
  <p>{message}</p>
</div>

<style>
  .fatal {
    position: fixed;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    background: var(--body);
    color: var(--legend);
  }
  p {
    max-width: 720px;
    margin: 0;
    text-align: center;
    font: 600 22px/1.35 'Barlow Condensed', sans-serif;
  }
</style>
