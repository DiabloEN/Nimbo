<script setup lang="ts">
import {ref,onMounted,onBeforeUnmount,computed,watch} from 'vue';import type {Stroke} from '../composables/useAnnotations';
const props=defineProps<{strokes:Stroke[];drawing?:boolean;color?:string;ratio?:number}>();const emit=defineEmits<{stroke:[{color:string;points:[number,number][]}]}>();
const root=ref<HTMLElement>(),size=ref({w:0,h:0}),points=ref<[number,number][]>([]);let pointer:number|undefined,observer:ResizeObserver|undefined;
const bounds=computed(()=>{const {w,h}=size.value,r=props.ratio||16/9;const width=Math.min(w,h*r),height=width/r;return {width:`${width}px`,height:`${height}px`,left:`${(w-width)/2}px`,top:`${(h-height)/2}px`};});
const path=(p:[number,number][])=>p.map(([x,y],i)=>`${i?'L':'M'}${x*1000},${y*1000}`).join(' ');
function point(e:PointerEvent){const rect=(e.currentTarget as SVGElement).getBoundingClientRect();return [Math.max(0,Math.min(1,(e.clientX-rect.left)/rect.width)),Math.max(0,Math.min(1,(e.clientY-rect.top)/rect.height))] as [number,number];}
function start(e:PointerEvent){if(!props.drawing||pointer!==undefined||e.button!==0)return;pointer=e.pointerId;(e.currentTarget as SVGElement).setPointerCapture(e.pointerId);points.value=[point(e)];}
function move(e:PointerEvent){if(pointer!==e.pointerId)return;if(points.value.length<350)points.value.push(point(e));}
function end(e:PointerEvent){if(pointer!==e.pointerId)return;if(points.value.length===1)points.value.push(point(e));emit('stroke',{color:props.color||'#f5c542',points:[...points.value]});points.value=[];pointer=undefined;}
function cancel(){points.value=[];pointer=undefined;}
watch(()=>props.drawing,cancel);
onMounted(()=>{observer=new ResizeObserver(entries=>{const box=entries[0]?.contentRect;if(box)size.value={w:box.width,h:box.height};});if(root.value)observer.observe(root.value);});onBeforeUnmount(()=>observer?.disconnect());
</script>
<template><div ref="root" class="annotation-layer"><svg :style="bounds" viewBox="0 0 1000 1000" preserveAspectRatio="none" :class="{drawing}" @pointerdown="start" @pointermove="move" @pointerup="end" @pointercancel="cancel"><path v-for="stroke in strokes" :key="stroke.author+stroke.id" :d="path(stroke.points)" :stroke="stroke.color"/><path v-if="points.length" :d="path(points)" :stroke="color||'#f5c542'"/></svg></div></template>
