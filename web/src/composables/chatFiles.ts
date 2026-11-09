export const MAX_CHAT_FILE_BYTES = 20 * 1024 * 1024;
export const MAX_CHAT_FILES = 10;
export function fileSelectionError(files: readonly File[]): string {
  if (!files.length || files.length > MAX_CHAT_FILES)
    return 'Можно прикрепить до 10 файлов к сообщению.';
  if (files.some((file) => !file.size || file.size > MAX_CHAT_FILE_BYTES))
    return 'Выберите непустые файлы до 20 МБ.';
  if (files.reduce((total, file) => total + file.size, 0) > MAX_CHAT_FILE_BYTES)
    return 'Общий размер вложений — до 20 МБ.';
  return '';
}
export function preventFileNavigation(event: DragEvent): boolean {
  const transfer = event.dataTransfer;
  if (!transfer || !Array.from(transfer.types).includes('Files')) return false;
  event.preventDefault();
  return true;
}
