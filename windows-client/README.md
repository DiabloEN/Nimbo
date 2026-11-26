# Nimbo для Windows x64

[Установить Nimbo 0.3.58](https://github.com/DiabloEN/Nimbo/releases/download/desktop-v0.3.58/Nimbo-0.3.58-Setup.exe)

Запустите Setup под своей учётной записью Windows. Мастер добавит приложение в меню «Пуск». Последующие версии устанавливаются в «Настройки → Общие → Обновление». Когда клиент обновится, он сам откроется на том же сервере, в выбранном канале и открытом разделе настроек. При первом обновлении установленной версии 0.3.35 восстановятся сервер и общие настройки. [Порядок выпуска и проверки обновлений](updates/README.md)

## Сборка из исходников

Потребуются Windows x64, Visual Studio Build Tools 2022 с C++ и Windows SDK, Python 3.12, Node.js 24 и NSIS 3 с Modern UI 2. Команды выполняются в PowerShell из корня репозитория.

```powershell
npm ci --prefix web
cd windows-client
.\scripts\bootstrap-media.ps1
.\.tools\python\Scripts\python.exe scripts/setup-native-effects.py
.\scripts\build-media.ps1
.\.tools\python\Scripts\python.exe -m pip install resvg-py==0.5.0 fonttools==4.68.0 brotli==1.2.0 pillow==13.0.0
.\.tools\python\Scripts\python.exe scripts/package-native-desktop.py --web ../web --nsis 'C:\Program Files (x86)\NSIS\makensis.exe'
```

Сборщик берёт версию из `desktop/version.h` и создаёт Setup, архив для обновления и манифест в `dist`. Сведения о компонентах и лицензиях поставляются с приложением. [Сторонние компоненты](desktop/THIRD_PARTY.md)

Для полного прогона `scripts/test-native-desktop.py` нужны собранные Qt-тесты, отдельный бинарник API для параметра `--server` и LiveKit Server в `.tools/media/server/livekit-server.exe`. Сценарии вложений и замера памяти используют тестовый ролик в `artifacts/oct30-qa/preview-flower.mp4`. Его SHA-256 — `0cd83d944a6ca7822b4a8306cecc60a36e859b041f6702c6a1ad9ead78924451`. [Тестовый ролик MDN, CC0](https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4)
