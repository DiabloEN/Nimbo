import { ref } from 'vue';
export type Stroke = { id: string; author: string; color: string; points: [number, number][] };
type Stream = { streamId?: string; participantId: string; source: string };
export const annotationTopic = 'nimbo-draw-v1';
export const annotationColors = [
  { value: '#f5c542', label: 'Жёлтый' },
  { value: '#ef7070', label: 'Красный' },
  { value: '#69b5ff', label: 'Голубой' },
  { value: '#78dba9', label: 'Зелёный' },
  { value: '#b99aff', label: 'Фиолетовый' },
  { value: '#eeeeee', label: 'Белый' },
];
const colors = annotationColors.map((c) => c.value);
export function useAnnotations(
  streams: () => Stream[],
  self: () => string,
  send: (bytes: Uint8Array<ArrayBuffer>) => Promise<void>,
) {
  const drawings = ref<Record<string, Stroke[]>>({}),
    note = ref('');
  function receive(bytes: Uint8Array, author: string) {
    if (bytes.length > 12000 || !author) return;
    try {
      const data = JSON.parse(new TextDecoder().decode(bytes));
      const stream = streams().find((t) => t.source === 'screen' && t.streamId === data.stream);
      if (!stream) return;
      const current = drawings.value[data.stream] || [];
      if (data.type === 'clear') {
        drawings.value[data.stream] =
          author === stream.participantId ? [] : current.filter((s) => s.author !== author);
        return;
      }
      if (data.type === 'undo' && typeof data.id === 'string') {
        drawings.value[data.stream] = current.filter(
          (s) => s.author !== author || s.id !== data.id,
        );
        return;
      }
      if (
        data.type !== 'stroke' ||
        typeof data.id !== 'string' ||
        data.id.length > 80 ||
        !colors.includes(data.color) ||
        !Array.isArray(data.points) ||
        data.points.length < 2 ||
        data.points.length > 350 ||
        !data.points.every(
          (p: unknown) =>
            Array.isArray(p) &&
            p.length === 2 &&
            p.every((n) => typeof n === 'number' && Number.isFinite(n) && n >= 0 && n <= 1),
        )
      )
        return;
      if (current.some((s) => s.id === data.id && s.author === author)) return;
      drawings.value[data.stream] = [
        ...current,
        { id: data.id, author, color: data.color, points: data.points },
      ].slice(-120);
    } catch {
      /* Untrusted data packets never escape into the UI. */
    }
  }
  async function publish(data: unknown) {
    const bytes = new TextEncoder().encode(JSON.stringify(data));
    receive(bytes, self());
    try {
      await send(bytes);
      note.value = '';
    } catch {
      note.value = 'Рисунок виден только вам: не удалось отправить его участникам.';
    }
  }
  function draw(stream: string, stroke: { color: string; points: [number, number][] }) {
    return publish({
      type: 'stroke',
      stream,
      id: crypto.randomUUID(),
      color: stroke.color,
      points: stroke.points.slice(0, 350).map((p) => p.map((n) => Math.round(n * 10000) / 10000)),
    });
  }
  function clear(stream: string) {
    return publish({ type: 'clear', stream });
  }
  function undo(stream: string) {
    const last = drawings.value[stream]?.filter((s) => s.author === self()).at(-1);
    if (last) return publish({ type: 'undo', stream, id: last.id });
  }
  function prune() {
    const ids = new Set(streams().map((t) => t.streamId));
    for (const id of Object.keys(drawings.value)) if (!ids.has(id)) delete drawings.value[id];
  }
  function reset() {
    drawings.value = {};
    note.value = '';
  }
  return { drawings, note, receive, draw, clear, undo, prune, reset };
}
