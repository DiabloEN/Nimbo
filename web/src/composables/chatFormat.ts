import { Schema, type Node as EditorNode } from 'prosemirror-model';

// One digit per UTF-16 code unit, in the same coordinate system as mentions.
// Plain messages omit the field. List flags are attached only to the prefix.
export const bold = 1,
  italic = 2,
  bullet = 4,
  numbered = 8;
export type ChatDraft = { text: string; format: string };
export const chatSchema = new Schema({
  nodes: {
    doc: { content: 'line+' },
    line: {
      group: 'block',
      content: 'text*',
      attrs: { prefix: { default: '' } },
      toDOM: (node) => [
        'div',
        { 'data-marker': node.attrs.prefix === '* ' ? '• ' : node.attrs.prefix },
        0,
      ],
    },
    text: { group: 'inline' },
  },
  marks: {
    bold: { toDOM: () => ['strong', 0] },
    italic: { toDOM: () => ['em', 0] },
  },
});

export function chatLines(text: string, format = '') {
  let offset = 0;
  return text.split('\n').map((line) => {
    const start = offset;
    offset += line.length + 1;
    const flag = format[start];
    const prefix =
      flag === '4' && line.startsWith('* ')
        ? '* '
        : flag === '8'
          ? line.match(/^\d{1,8}\. /)?.[0] || ''
          : '';
    return { start: start + prefix.length, text: line.slice(prefix.length), prefix };
  });
}

export function readDraft({ text, format }: ChatDraft) {
  const lines = chatLines(text, format).map((line) => {
    const runs = [];
    for (let i = 0; i < line.text.length; ) {
      const flags = Number(format[line.start + i]) & 3;
      let end = i + 1;
      while (end < line.text.length && (Number(format[line.start + end]) & 3) === flags) end++;
      const marks = [];
      if (flags & bold) marks.push(chatSchema.marks.bold!.create());
      if (flags & italic) marks.push(chatSchema.marks.italic!.create());
      runs.push(chatSchema.text(line.text.slice(i, end), marks));
      i = end;
    }
    return chatSchema.nodes.line!.create({ prefix: line.prefix }, runs);
  });
  return chatSchema.topNodeType.create(null, lines);
}

export function writeDraft(doc: EditorNode): ChatDraft {
  const lines: string[] = [],
    flags: string[] = [];
  doc.forEach((line) => {
    const prefix: string = line.attrs.prefix;
    lines.push(prefix + line.textContent);
    let row = String(prefix === '* ' ? bullet : numbered).repeat(prefix.length);
    line.forEach((run) => {
      const mask = run.marks.reduce((n, m) => n | (m.type.name === 'bold' ? bold : italic), 0);
      row += String(mask).repeat(run.nodeSize);
    });
    flags.push(row);
  });
  return { text: lines.join('\n'), format: flags.join('0').replace(/0+$/, '') };
}

export function textOffset(doc: EditorNode, position: number) {
  const point = doc.resolve(position);
  let offset = point.parentOffset;
  for (let i = 0; i <= point.index(0) && i < doc.childCount; i++) {
    const line = doc.child(i);
    offset += line.attrs.prefix.length;
    if (i < point.index(0)) offset += line.content.size + 1;
  }
  return offset;
}

export function editorPosition(doc: EditorNode, offset: number) {
  let pos = 1;
  for (let i = 0; i < doc.childCount; i++) {
    const line = doc.child(i);
    offset -= line.attrs.prefix.length;
    if (offset <= line.content.size) return pos + Math.max(0, offset);
    offset -= line.content.size + 1;
    pos += line.nodeSize;
  }
  return doc.content.size - 1;
}
