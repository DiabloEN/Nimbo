<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue';
import AppIcon from './AppIcon.vue';
import {
  hotkeyActions,
  modifierKey,
  readBinding,
  readMouseBinding,
  bindingLabel,
  type Binding,
  type Bindings,
  type HotkeyAction,
} from '../composables/useHotkeys';
const props = defineProps<{
  bindings: Bindings;
  note: string;
  set: (id: HotkeyAction, b?: Binding) => boolean;
  reset: () => void;
}>();
const recording = ref<HotkeyAction>(),
  hint = ref('');
let pendingModifier: Binding | undefined,
  consumedKey = '';
function stopRecording() {
  recording.value = undefined;
  pendingModifier = undefined;
  hint.value = '';
}
function record(e: KeyboardEvent, id: HotkeyAction) {
  if (e.type === 'keyup' && e.code === consumedKey) {
    e.preventDefault();
    e.stopPropagation();
    consumedKey = '';
    return;
  }
  if (recording.value !== id) return;
  e.preventDefault();
  e.stopPropagation();
  if (e.repeat || e.isComposing) return;
  const b = readBinding(e);
  if (e.type === 'keydown') {
    pendingModifier = undefined;
    if (modifierKey(e.code)) {
      pendingModifier = b;
      return;
    }
    consumedKey = e.code;
  } else {
    const pending = pendingModifier;
    pendingModifier = undefined;
    if (!pending || pending.code !== e.code) return;
    if (props.set(id, pending)) stopRecording();
    return;
  }
  if (!b) {
    hint.value = 'Браузер не распознал эту клавишу. Попробуйте другую.';
    return;
  }
  if (props.set(id, b)) stopRecording();
}
let captured = -1,
  timer: ReturnType<typeof setTimeout> | undefined;
function mouse(e: MouseEvent) {
  const id = recording.value,
    b = readMouseBinding(e);
  if (!id || !b) return;
  e.preventDefault();
  e.stopImmediatePropagation();
  captured = e.button;
  clearTimeout(timer);
  timer = setTimeout(() => (captured = -1), 1500);
  if (props.set(id, b)) {
    recording.value = undefined;
    hint.value = '';
  }
}
function release(e: MouseEvent) {
  if (e.button === captured) {
    e.preventDefault();
    e.stopImmediatePropagation();
  }
}
onMounted(() => {
  window.addEventListener('mousedown', mouse, true);
  window.addEventListener('mouseup', release, true);
  window.addEventListener('auxclick', release, true);
});
onBeforeUnmount(() => {
  window.removeEventListener('mousedown', mouse, true);
  window.removeEventListener('mouseup', release, true);
  window.removeEventListener('auxclick', release, true);
  clearTimeout(timer);
});
</script>
<template>
  <section>
    <p class="field-help">
      Работают в активной вкладке Nimbo, когда вы не вводите текст. Сохраняются для вашего профиля в
      этом браузере. Системные сочетания могут перехватываться браузером или ОС.
    </p>
    <ul class="managed-list">
      <li v-for="(label, id) in hotkeyActions" :key="id">
        <div>
          <strong>{{ label }}</strong>
        </div>
        <button
          type="button"
          class="soft-button"
          data-hotkey-capture
          :aria-label="'Сочетание: ' + label"
          :aria-pressed="recording === id"
          @click="
            recording = id;
            pendingModifier = undefined;
            hint = '';
          "
          @blur="stopRecording"
          @keydown="record($event, id)"
          @keyup="record($event, id)"
        >
          {{
            recording === id ? 'Нажмите клавишу или кнопку…' : bindingLabel(bindings[id])
          }}</button
        ><button v-if="recording === id" type="button" class="soft-button" @click="stopRecording">
          Отмена</button
        ><button
          type="button"
          class="icon-button hotkey-remove"
          :disabled="!bindings[id]"
          :aria-label="'Убрать сочетание: ' + label"
          title="Убрать сочетание"
          @click="set(id)"
        >
          <AppIcon name="close" :size="18" />
        </button>
      </li>
    </ul>
    <p class="field-help">
      Нажмите любую клавишу, сочетание или боковую кнопку мыши. Отдельные Ctrl, Alt и Shift
      назначаются при отпускании. Крестик убирает назначение.
    </p>
    <p v-if="hint || note" class="form-error" role="alert">{{ hint || note }}</p>
    <button class="soft-button" @click="reset">Вернуть стандартные</button>
  </section>
</template>
