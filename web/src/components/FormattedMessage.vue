<script setup lang="ts">
import { computed } from 'vue';
import { chatLines } from '../composables/chatFormat';
import { linkParts } from '../composables/chatLinks';
import { messageParts, type Mention } from '../composables/mentions';
import type { Member } from '../composables/useWorkspace';
const props = defineProps<{ text: string; format?: string; mentions?: Mention[]; self?: Member }>();
function styled(text: string, start: number) {
  const spans = [];
  for (let i = 0; i < text.length; ) {
    const flags = Number(props.format?.[start + i]) & 3;
    let end = i + 1;
    while (end < text.length && (Number(props.format?.[start + end]) & 3) === flags) end++;
    spans.push({ text: text.slice(i, end), flags });
    i = end;
  }
  return spans;
}
const blocks = computed(() => {
  function row(line: ReturnType<typeof chatLines>[number]) {
    const mentions = props.mentions
      ?.filter((m) => m.start >= line.start && m.end <= line.start + line.text.length)
      .map((m) => ({ ...m, start: m.start - line.start, end: m.end - line.start }));
    let pos = line.start;
    return messageParts(line.text, mentions, props.self).flatMap((part) => {
      const links = part.mention ? [{ text: part.text, href: undefined }] : linkParts(part.text);
      return links.map((link) => {
        const spans = styled(link.text, pos);
        pos += link.text.length;
        return { ...part, href: link.href, spans };
      });
    });
  }
  const result: { tag: 'p' | 'ul' | 'ol'; start?: number; rows: ReturnType<typeof row>[] }[] = [];
  for (const line of chatLines(props.text, props.format)) {
    const tag = !line.prefix ? 'p' : line.prefix === '* ' ? 'ul' : 'ol';
    const start = tag === 'ol' ? Number.parseInt(line.prefix) : undefined;
    let block = result.at(-1);
    if (
      !block ||
      block.tag !== tag ||
      (tag === 'ol' && start !== block.start! + block.rows.length)
    ) {
      block = { tag, start, rows: [] };
      result.push(block);
    }
    block.rows.push(row(line));
  }
  return result;
});
</script>
<template>
  <div class="formatted-message">
    <component v-for="(block, i) in blocks" :key="i" :is="block.tag" :start="block.start">
      <component
        v-for="(row, j) in block.rows"
        :key="j"
        :is="block.tag === 'p' ? 'span' : 'li'"
        :data-chat-row="
          block.tag === 'ol' ? block.start! + j + '. ' : block.tag === 'ul' ? '* ' : ''
        "
      >
        <component
          v-for="(part, k) in row"
          :key="k"
          :is="part.href ? 'a' : 'span'"
          :href="part.href"
          :target="part.href ? '_blank' : undefined"
          :rel="part.href ? 'noopener noreferrer nofollow' : undefined"
          :referrerpolicy="part.href ? 'no-referrer' : undefined"
          :class="{ 'message-mention': part.mention, 'personal-mention': part.personal }"
          :title="part.personal ? 'Упоминание для вас' : undefined"
          ><span
            v-for="(span, n) in part.spans"
            :key="n"
            :class="{ bold: span.flags & 1, italic: span.flags & 2 }"
            >{{ span.text }}</span
          ></component
        >
      </component>
    </component>
  </div>
</template>
<style scoped>
.formatted-message {
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  padding-right: 10px;
}
p,
ul,
ol {
  margin: 4px 0 0;
}
ul,
ol {
  padding-inline-start: 1.75em;
}
ul {
  list-style-type: disc;
}
ol {
  list-style-type: decimal;
}
p > [data-chat-row] {
  display: block;
  min-height: 1lh;
}
.bold {
  font-weight: 700;
}
.italic {
  font-style: italic;
  font-synthesis: style;
}
a {
  color: #8fc4f5;
  text-decoration: underline;
  text-underline-offset: 3px;
  overflow-wrap: anywhere;
}
a:hover {
  color: #c8e3ff;
}
.message-mention {
  color: #c3e5df;
  background: #2d403d;
  border-radius: 4px;
  padding: 1px 2px;
}
.personal-mention {
  background: #285b9d;
  color: #f1f7ff;
  font-weight: 500;
}
</style>
