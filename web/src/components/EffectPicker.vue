<script setup lang="ts">
import AppIcon from './AppIcon.vue';
defineProps<{ enabled: boolean; busy: boolean; strength?: number }>();
const emit = defineEmits<{ strength: [value: number]; select: [value: boolean] }>();
</script>
<template>
  <div class="effect-options" role="group" aria-label="Эффекты камеры">
    <div v-for="blur in [false, true]" :key="String(blur)" class="effect-choice">
      <button
        type="button"
        class="effect-option"
        :class="{ selected: enabled === blur }"
        :aria-pressed="enabled === blur"
        :disabled="busy"
        @click="emit('select', blur)"
      >
        <span class="effect-photo" :class="{ blur }" aria-hidden="true"></span
        ><span class="effect-option-caption"
          ><strong>{{ blur ? 'Размытый фон' : 'Без эффектов' }}</strong
          ><AppIcon v-if="enabled === blur" name="check" :size="17"
        /></span></button
      ><label v-if="blur && enabled" class="blur-strength" title="Сила размытия"
        ><span class="sr-only">Сила размытия</span
        ><input
          type="range"
          min="1"
          max="60"
          step="1"
          :value="strength || 12"
          :disabled="busy"
          @change="emit('strength', Number(($event.target as HTMLInputElement).value))"
        /><output aria-hidden="true">{{ strength || 12 }}</output></label
      >
    </div>
  </div>
</template>
<style scoped>
.effect-choice {
  position: relative;
  min-width: 0;
}
.blur-strength {
  position: absolute;
  left: 10px;
  right: 10px;
  bottom: 56px;
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 0;
  padding: 7px 9px;
  border-radius: 9px;
  background: #141414e8;
  color: #fff;
}
.blur-strength input {
  flex: 1;
  min-width: 0;
  width: 0;
  height: 18px;
  padding: 0;
  margin: 0;
  accent-color: #66aaf0;
  cursor: pointer;
}
.blur-strength output {
  font-size: 10px;
  font-variant-numeric: tabular-nums;
  min-width: 14px;
  text-align: right;
}

.effect-options {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 220px));
  gap: 12px;
}
.effect-option {
  display: block !important;
  width: 100% !important;
  height: auto !important;
  padding: 0 !important;
  background: #1a1a1a !important;
  color: #ddd !important;
  border-radius: 14px !important;
  overflow: hidden;
  text-align: left;
  box-shadow: none;
}
.effect-option.selected {
  box-shadow: none;
  background: #29302e !important;
}
.effect-photo {
  display: block;
  width: 100%;
  aspect-ratio: 1;
  background-image: url('/effects/room-preview.png');
  background-size: 200% 100%;
  background-position: left center;
}
.effect-photo.blur {
  background-position: right center;
}
.effect-option-caption {
  min-height: 46px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  padding: 12px;
  font-size: 12px;
}
.effect-option-caption strong {
  font-weight: 500;
}
.effect-option-caption svg {
  flex-shrink: 0;
}
</style>
