type Entry = { at: string; event: string; details: Record<string, unknown> };
const entries: Entry[] = [];
try {
  const saved = JSON.parse(sessionStorage.getItem('nimbo-audio-diagnostics') || '[]');
  if (Array.isArray(saved))
    entries.push(
      ...saved
        .slice(-100)
        .filter(
          (e) =>
            e &&
            typeof e.at === 'string' &&
            typeof e.event === 'string' &&
            e.details &&
            typeof e.details === 'object',
        ),
    );
} catch {}
// Bounded local history only: no audio, tokens, URLs, or server upload.
export function audioDiagnostic(event: string, details: Record<string, unknown> = {}) {
  entries.push({ at: new Date().toISOString(), event, details });
  if (entries.length > 100) entries.shift();
  try {
    sessionStorage.setItem('nimbo-audio-diagnostics', JSON.stringify(entries));
  } catch {}
}
export function downloadAudioDiagnostics() {
  const blob = new Blob([JSON.stringify({ version: 1, events: entries }, null, 2)], {
    type: 'application/json',
  });
  const url = URL.createObjectURL(blob),
    a = document.createElement('a');
  a.href = url;
  a.download = 'nimbo-audio-diagnostics.json';
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
