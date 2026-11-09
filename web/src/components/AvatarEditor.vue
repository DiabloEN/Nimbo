<script setup lang="ts">
import { ref } from 'vue';
import AvatarCropper from './AvatarCropper.vue';
import AppIcon from './AppIcon.vue';
defineProps<{ value: string; fallback: string; disabled?: boolean; square?: boolean }>();
const emit = defineEmits<{ change: [value: string]; remove: [] }>();
const input = ref<HTMLInputElement>(),
  cropFile = ref<File>();
function choose(e: Event) {
  const el = e.target as HTMLInputElement;
  cropFile.value = el.files?.[0];
  el.value = '';
}
</script>
<template>
  <div class="avatar-control" :class="{ square }">
    <button
      type="button"
      class="avatar-upload"
      :disabled="disabled"
      :aria-label="value ? 'Изменить аватар' : 'Загрузить аватар'"
      @click="input?.click()"
    >
      <img v-if="value" :src="value" alt="Аватар" /><span v-else class="avatar-fallback"
        >{{ fallback }}<b v-if="square">.</b></span
      ><span class="avatar-overlay"
        ><AppIcon name="camera" :size="22" /><small>{{
          value ? 'Изменить' : 'Загрузить'
        }}</small></span
      ></button
    ><input
      ref="input"
      type="file"
      accept="image/jpeg,image/png,image/webp"
      :disabled="disabled"
      tabindex="-1"
      class="sr-only"
      aria-label="Файл аватара"
      @change="choose"
    /><button
      v-if="value"
      type="button"
      class="avatar-remove"
      :disabled="disabled"
      aria-label="Удалить аватар"
      title="Удалить аватар"
      @click="emit('remove')"
    >
      <AppIcon name="close" :size="14" />
    </button>
  </div>
  <AvatarCropper
    v-if="cropFile"
    :file="cropFile"
    :square="square"
    @cancel="cropFile = undefined"
    @save="
      (value) => {
        cropFile = undefined;
        emit('change', value);
      }
    "
  />
</template>
<style scoped>
.avatar-control {
  position: relative;
  width: 80px;
  height: 80px;
  flex: none;
}
.avatar-upload {
  position: relative;
  width: 80px;
  height: 80px;
  border-radius: 50%;
  padding: 0;
  overflow: hidden;
  background: #303030;
  color: #ddd;
}
.square .avatar-upload {
  border-radius: 16px;
  background: #ddd;
  color: #111;
}
.avatar-upload img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.avatar-fallback {
  font-size: 25px;
}
.avatar-fallback b {
  font-weight: 400;
  color: #888;
}
.avatar-overlay {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
  align-items: center;
  justify-content: center;
  background: #0009;
  color: #fff;
  opacity: 0;
  transition: opacity 120ms;
}
.avatar-overlay small {
  font-size: 9px;
}
.avatar-control:hover .avatar-overlay,
.avatar-control:focus-within .avatar-overlay {
  opacity: 1;
}
.avatar-remove {
  position: absolute;
  right: -4px;
  top: -4px;
  width: 24px;
  height: 24px;
  min-height: 0;
  padding: 5px;
  border-radius: 50%;
  background: #444;
  color: #fff;
  opacity: 0;
}
.avatar-control:hover .avatar-remove,
.avatar-control:focus-within .avatar-remove {
  opacity: 1;
}
@media (hover: none) {
  .avatar-remove {
    opacity: 1;
  }
  .avatar-overlay {
    opacity: 1;
    background: linear-gradient(transparent 40%, #000a);
    justify-content: flex-end;
    padding-bottom: 8px;
  }
  .avatar-overlay svg {
    display: none;
  }
}
</style>
