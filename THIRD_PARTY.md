# Сторонние компоненты

MIT в корне относится к собственному коду Nimbo. Зависимости сохраняют свои лицензии.

- Vue, Vite, TypeScript, Vitest и LiveKit JS: точные версии зафиксированы в `web/package-lock.json`; Vite собирает уведомления в `assets/licenses.md`.
- LiveKit Server 1.13.10 — Apache-2.0, https://github.com/livekit/livekit. Образ и Caddy с layer4 закреплены digest в установщике.
- Caddy — Apache-2.0, https://github.com/caddyserver/caddy. Layer4: https://github.com/mholt/caddy-l4.
- Roboto — SIL OFL 1.1. Самостоятельно размещённые шрифты и текст лицензии находятся в `web/src/assets/fonts/`.
- Iconsax Linear — Vuesax / Lusaxweb. Текст условий в `web/public/licenses/iconsax.txt`; ссылки на источник в `web/src/composables/iconsax.ts`.
- `@livekit/track-processors` 0.8.2 и MediaPipe Tasks Vision 0.10.14 — Apache-2.0. Локальные WASM-ресурсы взяты из закреплённого пакета. Модель Google MediaPipe selfie_segmenter float16 v1: https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter/float16/1/selfie_segmenter.tflite .
- `@sapphi-red/web-noise-suppressor` 0.4.1 — MIT; его `@shiguredo/rnnoise-wasm` 2022.2.0 — Apache-2.0; Xiph RNNoise — BSD-3-Clause. Уведомления в `web/public/licenses/rnnoise/`. Обработка выполняется локально.
- ClamAV устанавливается администратором отдельно; его бинарники и базы в репозитории отсутствуют. https://www.clamav.net/.
- Экспериментальный выбор шумоподавления также включает [GTCRN WASM](https://github.com/sapphi-red/gtcrn-wasm). Обёртки и модель GTCRN распространяются по MIT. Уведомления GTCRN и pffft сохранены в worklet, лицензия обёртки — в `web/public/licenses/gtcrn/`.

Демонстрационное фотографическое превью камеры `web/public/effects/room-preview.png` создано для проекта через imagegen; это согласованный ресурс интерфейса, а не пользовательская фотография. Оригинал и контрольная сумма описаны в `docs/UI-CONTENT.md`. Перед публичным выпуском требуется окончательная сверка лицензий и уведомлений всех распространяемых ресурсов. Нативные Qt/FFmpeg/C++ бинарники в этот снимок не входят.
