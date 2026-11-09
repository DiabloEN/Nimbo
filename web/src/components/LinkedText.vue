<script setup lang="ts">
import { linkParts } from '../composables/chatLinks';
defineProps<{ text: string }>();
</script>
<template>
  <template v-for="(part, i) in linkParts(text)" :key="i"
    ><a
      v-if="part.href"
      :href="part.href"
      target="_blank"
      rel="noopener noreferrer nofollow"
      referrerpolicy="no-referrer"
      @click.stop
      >{{ part.text }}</a
    ><template v-else>{{ part.text }}</template></template
  >
</template>
<style scoped>
a {
  color: #8fc4f5;
  text-decoration: underline;
  text-underline-offset: 3px;
  overflow-wrap: anywhere;
}
a:hover {
  color: #c8e3ff;
}
</style>
