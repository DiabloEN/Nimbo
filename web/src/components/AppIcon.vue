<script setup lang="ts">
import {iconsax} from '../composables/iconsax';
defineProps<{name:string;size?:number}>();
</script>
<template><svg :width="size||20" :height="size||20" viewBox="0 0 24 24" fill="none" aria-hidden="true" focusable="false"><component v-for="(node,i) in iconsax[name]||iconsax.sound" :key="i" :is="node.tag" v-bind="node.attrs"/></svg></template>
