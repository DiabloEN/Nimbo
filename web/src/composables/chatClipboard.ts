import { DOMSerializer, Slice } from 'prosemirror-model';
import type { EditorView } from 'prosemirror-view';
import {
  bold,
  italic,
  bullet,
  numbered,
  chatSchema,
  readDraft,
  type ChatDraft,
} from './chatFormat';

function prefix(number?: number) {
  return number === undefined ? '* ' : String(Math.min(99999999, Math.max(0, number))) + '. ';
}

function plainDraft(text: string): ChatDraft {
  const lines = text.replace(/\r\n?/g, '\n').split('\n');
  const masks = lines.map((line, i) => {
    const match = /^([*•-]|\d{1,8}\.)[ \t]+/.exec(line);
    if (!match) return '0'.repeat(line.length);
    const ordered = /^\d/.test(match[1]!);
    const marker = ordered ? match[1] + ' ' : '* ';
    const body = line.slice(match[0].length);
    lines[i] = marker + body;
    return String(ordered ? numbered : bullet).repeat(marker.length) + '0'.repeat(body.length);
  });
  return { text: lines.join('\n'), format: masks.join('0') };
}

// Traverse an inert template and rebuild only text, line breaks, lists and two marks.
function htmlDraft(fragment: DocumentFragment): ChatDraft {
  const lines: ChatDraft[] = [];
  let line: ChatDraft | undefined;
  function begin(marker = '') {
    line = {
      text: marker,
      format: String(marker === '* ' ? bullet : numbered).repeat(marker.length),
    };
    lines.push(line);
  }
  function visit(node: Node, marks: number, marker?: string) {
    if (node.nodeType === Node.TEXT_NODE) {
      const text = node.textContent || '';
      if (!text || (!line && !text.trim())) return;
      if (!line) begin();
      line!.text += text;
      line!.format += String(marks).repeat(text.length);
      return;
    }
    if (!(node instanceof Element) && node.nodeType !== Node.ELEMENT_NODE) return;
    const el = node as HTMLElement,
      tag = el.localName;
    if (
      [
        'script',
        'style',
        'template',
        'svg',
        'math',
        'iframe',
        'object',
        'head',
        'noscript',
      ].includes(tag)
    )
      return;
    if (tag === 'br') {
      if (!line) begin();
      begin();
      return;
    }
    if (tag === 'ul' || tag === 'ol') {
      let index = Number(el.getAttribute('start') ?? 1);
      for (const child of el.children) {
        if (child.localName !== 'li') continue;
        const n = Number(child.getAttribute('value') ?? index);
        index = Number.isSafeInteger(n) ? n : 1;
        visit(child, marks, prefix(tag === 'ol' ? index++ : undefined));
      }
      line = undefined;
      return;
    }
    const block = ['li', 'p', 'div', 'section', 'blockquote', 'pre'].includes(tag);
    if (block) {
      // Editors often wrap the text of a list item in a paragraph.
      if (
        !(el.parentElement?.localName === 'li' && el === el.parentElement.firstElementChild && line)
      )
        begin(marker);
    }
    const weight = el.style?.fontWeight;
    if (
      tag === 'b' ||
      tag === 'strong' ||
      weight === 'bold' ||
      Number(weight) >= 600 ||
      el.classList.contains('bold')
    )
      marks |= bold;
    if (
      tag === 'i' ||
      tag === 'em' ||
      el.style?.fontStyle === 'italic' ||
      el.classList.contains('italic')
    )
      marks |= italic;
    for (const child of el.childNodes) visit(child, marks);
    if (block) line = undefined;
  }
  for (const node of fragment.childNodes) visit(node, 0);
  return {
    text: lines.map((l) => l.text).join('\n'),
    format: lines.map((l) => l.format).join('0'),
  };
}

function putClipboard(data: DataTransfer, draft: ChatDraft) {
  const root = document.createElement('div');
  let list: HTMLElement | undefined,
    next = 1;
  const serializer = DOMSerializer.fromSchema(chatSchema);
  readDraft(draft).forEach((line) => {
    const marker: string = line.attrs.prefix;
    const row = document.createElement(marker ? 'li' : 'p');
    row.append(serializer.serializeFragment(line.content));
    if (!marker) {
      list = undefined;
      root.append(row);
      return;
    }
    const ordered = marker !== '* ',
      tag = ordered ? 'ol' : 'ul';
    const n = Number.parseInt(marker);
    if (!list || list.localName !== tag || (ordered && next !== n)) {
      list = document.createElement(tag);
      if (ordered) list.setAttribute('start', String(n));
      root.append(list);
    }
    list.append(row);
    next = n + 1;
  });
  data.setData('text/plain', draft.text);
  data.setData('text/html', root.innerHTML);
}

// Use actual text offsets so a partial word never gains a list marker.
export function copyChatSelection(event: ClipboardEvent, scope: HTMLElement) {
  const selection = window.getSelection();
  if (!event.clipboardData || !selection?.rangeCount || selection.isCollapsed) return false;
  const range = selection.getRangeAt(0);
  if (!scope.contains(range.commonAncestorContainer)) return false;
  const rows = scope.querySelectorAll<HTMLElement>('[data-chat-row], [data-marker]');
  const items: ChatDraft[] = [];
  for (const row of rows) {
    if (!range.intersectsNode(row)) continue;
    const part = document.createRange();
    part.selectNodeContents(row);
    if (range.compareBoundaryPoints(Range.START_TO_START, part) > 0)
      part.setStart(range.startContainer, range.startOffset);
    if (range.compareBoundaryPoints(Range.END_TO_END, part) < 0)
      part.setEnd(range.endContainer, range.endOffset);
    if (part.collapsed && row.textContent) continue;
    const before = document.createRange();
    before.selectNodeContents(row);
    before.setEnd(part.startContainer, part.startOffset);
    const raw = before.toString() ? '' : row.dataset.marker || row.dataset.chatRow || '';
    const marker = raw === '• ' ? '* ' : raw;
    const content = part.cloneContents();
    // cloneContents omits shared ancestors, including marks on a selected text node.
    let ancestor = part.commonAncestorContainer;
    if (ancestor.nodeType === Node.TEXT_NODE) ancestor = ancestor.parentNode!;
    for (; ancestor !== row && row.contains(ancestor); ancestor = ancestor.parentNode!) {
      const wrap = ancestor.cloneNode(false);
      wrap.appendChild(content);
      content.appendChild(wrap);
    }
    const value = htmlDraft(content);
    items.push({
      text: marker + value.text,
      format: String(marker === '* ' ? bullet : numbered).repeat(marker.length) + value.format,
    });
  }
  if (!items.length) return false;
  putClipboard(event.clipboardData, {
    text: items.map((i) => i.text).join('\n'),
    format: items.map((i) => i.format).join('0'),
  });
  event.preventDefault();
  return true;
}

export function pasteChat(editor: EditorView, event: ClipboardEvent) {
  const data = event.clipboardData;
  if (!data) return false;
  const text = data.getData('text/plain');
  if (text.length > 16000) return true;
  const html = data.getData('text/html');
  let draft = plainDraft(text);
  if (html && html.length < 256000) {
    const template = document.createElement('template');
    template.innerHTML = html;
    const rich = htmlDraft(template.content);
    if (/[12348]/.test(rich.format)) draft = rich;
  }
  if (!draft.text || draft.text.length > 8000) return true;
  const doc = readDraft(draft);
  const first = doc.firstChild!.attrs.prefix;
  const tr = editor.state.tr,
    point = tr.selection.$from;
  if (first && point.depth && !point.parentOffset)
    tr.setNodeMarkup(point.before(), undefined, { prefix: first });
  const open = first && point.depth && point.parentOffset > 0 ? 0 : 1;
  editor.dispatch(tr.replaceSelection(new Slice(doc.content, open, open)).scrollIntoView());
  return true;
}
