<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { copyChatSelection, pasteChat } from '../composables/chatClipboard';
import { EditorState, Plugin, type Command } from 'prosemirror-state';
import { EditorView } from 'prosemirror-view';
import { baseKeymap, toggleMark } from 'prosemirror-commands';
import { keymap } from 'prosemirror-keymap';
import { history, undo, redo } from 'prosemirror-history';
import 'prosemirror-view/style/prosemirror.css';
import {
  chatSchema,
  editorPosition,
  readDraft,
  textOffset,
  writeDraft,
  type ChatDraft,
} from '../composables/chatFormat';

const props = defineProps<{
  draft: ChatDraft;
  disabled?: boolean;
  label: string;
  placeholder?: string;
  inputId?: string;
  editing?: boolean;
  expanded?: boolean;
  option?: string;
}>();
const emit = defineEmits<{
  change: [draft: ChatDraft];
  selection: [];
  submit: [];
  cancel: [];
  key: [event: KeyboardEvent];
  blur: [];
}>();
const host = ref<HTMLElement>();
let view: EditorView | undefined;
const newLine: Command = (state, dispatch) => {
  const { $from, empty } = state.selection;
  const prefix: string = $from.parent.attrs.prefix;
  if (prefix && empty && !$from.parent.content.size) {
    dispatch?.(state.tr.setNodeMarkup($from.before(), undefined, { prefix: '' }));
  } else if (prefix) {
    const next = prefix === '* ' ? prefix : Number.parseInt(prefix) + 1 + '. ';
    if (next.length > 10) return true;
    const tr = state.tr.deleteSelection();
    dispatch?.(
      tr
        .split(tr.selection.from, 1, [{ type: chatSchema.nodes.line!, attrs: { prefix: next } }])
        .scrollIntoView(),
    );
  } else return baseKeymap.Enter!(state, dispatch);
  return true;
};
function attributes() {
  return {
    ...(props.inputId ? { id: props.inputId } : {}),
    role: 'textbox',
    'aria-multiline': 'true',
    'aria-label': props.label,
    'aria-disabled': String(!!props.disabled),
    'data-placeholder': props.placeholder || '',
    ...(props.expanded
      ? { 'aria-controls': 'mention-options', 'aria-activedescendant': props.option || '' }
      : {}),
  };
}
function selection() {
  if (!view) return { start: 0, end: 0 };
  const { doc, selection } = view.state;
  return { start: textOffset(doc, selection.from), end: textOffset(doc, selection.to) };
}
function replace(start: number, end: number, text: string) {
  if (!view || props.disabled) return;
  const { doc } = view.state;
  view.dispatch(
    view.state.tr
      .insertText(text, editorPosition(doc, start), editorPosition(doc, end))
      .scrollIntoView(),
  );
  view.focus();
}
const plugins = [
  keymap({
    'Mod-b': toggleMark(chatSchema.marks.bold!),
    'Mod-i': toggleMark(chatSchema.marks.italic!),
    'Mod-z': undo,
    'Mod-Shift-z': redo,
    'Mod-y': redo,
    Enter: (state, dispatch) => {
      if (props.editing) return newLine(state, dispatch);
      emit('submit');
      return true;
    },
    'Shift-Enter': newLine,
    'Mod-Enter': () => {
      emit('submit');
      return true;
    },
    Escape: () => {
      if (!props.editing) return false;
      emit('cancel');
      return true;
    },
    Backspace: (state, dispatch) => {
      const { $from, empty } = state.selection;
      if (!empty || $from.parentOffset || !$from.parent.attrs.prefix) return false;
      dispatch?.(state.tr.setNodeMarkup($from.before(), undefined, { prefix: '' }));
      return true;
    },
  }),
  keymap(baseKeymap),
  history(),
  new Plugin({
    filterTransaction: (tr) => !tr.docChanged || writeDraft(tr.doc).text.length <= 8000,
  }),
];
function stateFor(draft: ChatDraft) {
  return EditorState.create({ doc: readDraft(draft), plugins });
}
onMounted(() => {
  view = new EditorView(host.value!, {
    state: stateFor(props.draft),
    attributes,
    editable: () => !props.disabled,
    handleKeyDown: (_, event) => {
      if (event.isComposing || event.keyCode === 229) return false;
      emit('key', event);
      return event.defaultPrevented;
    },
    handleTextInput: (editor, from, to, inserted) => {
      const point = editor.state.doc.resolve(from);
      if (inserted !== ' ' || point.parent.attrs.prefix || from !== to) return false;
      const marker = point.parent.textContent.slice(0, point.parentOffset);
      if (!/^(\*|1|\d{1,8}\.)$/.test(marker)) return false;
      editor.dispatch(
        editor.state.tr
          .delete(point.start(), from)
          .setNodeMarkup(point.before(), undefined, {
            prefix: marker === '1' ? '1. ' : marker + ' ',
          }),
      );
      return true;
    },
    handlePaste: pasteChat,
    handleDOMEvents: {
      copy: (editor, event) => copyChatSelection(event, editor.dom),
      cut: (editor, event) => {
        if (!editor.editable || !copyChatSelection(event, editor.dom)) return false;
        editor.dispatch(editor.state.tr.deleteSelection().scrollIntoView());
        return true;
      },
      blur: () => {
        emit('blur');
        return false;
      },
    },
    dispatchTransaction(tr) {
      if (!view) return;
      const previous = view.state.doc;
      view.updateState(view.state.apply(tr));
      if (!previous.eq(view.state.doc)) emit('change', writeDraft(view.state.doc));
      emit('selection');
    },
  });
});
watch(
  () => props.draft,
  (draft) => {
    if (!view) return;
    const current = writeDraft(view.state.doc);
    if (current.text !== draft.text || current.format !== draft.format)
      view.updateState(stateFor(draft));
  },
);
watch(
  () => [props.disabled, props.expanded, props.option, props.placeholder],
  () => view?.setProps({ attributes, editable: () => !props.disabled }),
);
onBeforeUnmount(() => view?.destroy());
defineExpose({
  focus: () => view?.focus(),
  selection,
  replace,
  text: () => (view ? writeDraft(view.state.doc).text : ''),
});
</script>
<template><div ref="host" class="chat-editor" /></template>
<style scoped>
.chat-editor {
  min-width: 0;
  font-synthesis: style;
}
.chat-editor :deep(strong) {
  font-weight: 700;
}
.chat-editor :deep(.ProseMirror) {
  outline: none;
  overflow-wrap: anywhere;
  font: inherit;
  white-space: pre-wrap;
}
.chat-editor :deep([data-marker]:not([data-marker=''])) {
  padding-inline-start: 1.75em;
  position: relative;
}
.chat-editor :deep([data-marker]:not([data-marker=''])::before) {
  content: attr(data-marker);
  position: absolute;
  right: calc(100% - 1.5em);
  white-space: pre;
}
.chat-editor
  :deep(.ProseMirror:has(> div:only-child > br:only-child):has(> [data-marker=''])::before) {
  content: attr(data-placeholder);
  color: #858585;
  pointer-events: none;
  position: absolute;
}
</style>
