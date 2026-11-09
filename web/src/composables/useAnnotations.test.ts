import { describe, it, expect, vi } from 'vitest';
import { useAnnotations } from './useAnnotations';
const bytes = (data: unknown) => new TextEncoder().encode(JSON.stringify(data));
describe('shared annotations', () => {
  it('sends sequential undo to the presenter and preserves other authors', async () => {
    const streams = () => [{ streamId: 'screen', participantId: 'presenter', source: 'screen' }];
    const presenter = useAnnotations(streams, () => 'presenter', vi.fn());
    const viewer = useAnnotations(
      streams,
      () => 'viewer',
      async (data) => presenter.receive(data, 'viewer'),
    );
    const stroke = {
      color: '#78dba9',
      points: [
        [0, 0],
        [1, 1],
      ] as [number, number][],
    };
    await viewer.draw('screen', stroke);
    await viewer.draw('screen', { ...stroke, color: '#b99aff' });
    presenter.receive(
      bytes({
        type: 'stroke',
        stream: 'screen',
        id: 'other',
        color: '#eeeeee',
        points: stroke.points,
      }),
      'other',
    );
    expect(presenter.drawings.value.screen).toHaveLength(3);
    await viewer.undo('screen');
    expect(presenter.drawings.value.screen).toHaveLength(2);
    await viewer.undo('screen');
    await viewer.undo('screen');
    expect(presenter.drawings.value.screen?.map((s) => s.author)).toEqual(['other']);
    presenter.receive(bytes({ type: 'undo', stream: 'screen', id: 'other' }), 'viewer');
    expect(presenter.drawings.value.screen).toHaveLength(1);
  });
  it('accepts bounded strokes only for an active screen, binds author to transport and deduplicates', () => {
    const a = useAnnotations(
      () => [{ streamId: 'screen', participantId: 'owner', source: 'screen' }],
      () => '',
      vi.fn(),
    );
    const stroke = {
      type: 'stroke',
      stream: 'screen',
      id: '1',
      author: 'spoof',
      color: '#eeeeee',
      points: [
        [0, 0],
        [1, 1],
      ],
    };
    a.receive(bytes(stroke), 'guest');
    a.receive(bytes(stroke), 'guest');
    expect(a.drawings.value.screen).toHaveLength(1);
    expect(a.drawings.value.screen![0]!.author).toBe('guest');
    for (const patch of [
      { stream: 'other' },
      {
        points: [
          [2, 0],
          [1, 1],
        ],
      },
      { color: 'url(evil)' },
      { points: Array(351).fill([0, 0]) },
    ])
      a.receive(bytes({ ...stroke, id: '2', ...patch }), 'guest');
    expect(a.drawings.value.screen).toHaveLength(1);
  });
  it('allows guests to clear only their marks and the stream owner to clear everyone', () => {
    const a = useAnnotations(
      () => [{ streamId: 'screen', participantId: 'owner', source: 'screen' }],
      () => '',
      vi.fn(),
    );
    for (const author of ['a', 'b'])
      a.receive(
        bytes({
          type: 'stroke',
          stream: 'screen',
          id: author,
          color: '#eeeeee',
          points: [
            [0, 0],
            [1, 1],
          ],
        }),
        author,
      );
    a.receive(bytes({ type: 'clear', stream: 'screen' }), 'a');
    expect(a.drawings.value.screen!.map((s) => s.author)).toEqual(['b']);
    a.receive(bytes({ type: 'clear', stream: 'screen' }), 'owner');
    expect(a.drawings.value.screen).toEqual([]);
  });
  it('removes drawings when sharing ends and reports failed delivery', async () => {
    let streams = [{ streamId: 's', participantId: 'me', source: 'screen' }];
    const a = useAnnotations(
      () => streams,
      () => 'me',
      vi.fn().mockRejectedValue(new Error()),
    );
    await a.draw('s', {
      color: '#eeeeee',
      points: [
        [0, 0],
        [1, 1],
      ],
    });
    expect(a.note.value).toContain('только вам');
    streams = [];
    a.prune();
    expect(a.drawings.value).toEqual({});
  });
});
