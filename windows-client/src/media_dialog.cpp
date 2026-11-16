#include "media_dialog.h"
#include "media_session.h"
#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QPushButton>
#include <QVBoxLayout>

void showMediaDialog(QWidget* parent) {
    QDialog dialog(parent);
    if (parent && parent->testAttribute(Qt::WA_DontShowOnScreen)) dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.setWindowTitle(QStringLiteral("Nimbo · проверка связи"));
    dialog.resize(700, 680);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20); layout->setSpacing(10);
    auto* hint = new QLabel(QStringLiteral("Откройте приглашение от компьютера, на котором запущен сервер. На каждом компьютере используйте своё приглашение. Для первого созвона подключите наушники: эхоподавление и шумодав ещё не включены."));
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* load = new QPushButton(QStringLiteral("Открыть приглашение…")); layout->addWidget(load);
    load->setObjectName("secondary");
    auto* form = new QFormLayout;
    auto* url = new QLineEdit; url->setPlaceholderText("ws://192.168.1.10:7880");
    auto* input = new QComboBox; auto* output = new QComboBox;
    input->addItem(QStringLiteral("Микрофон по умолчанию"), QByteArray{});
    output->addItem(QStringLiteral("Выход по умолчанию"), QByteArray{});
    for (const auto& device : QMediaDevices::audioInputs()) input->addItem(device.description(), device.id());
    for (const auto& device : QMediaDevices::audioOutputs()) output->addItem(device.description(), device.id());
    auto* video = new QComboBox;
    for (auto* combo : {input, output, video}) combo->setMinimumHeight(40);
    url->setMinimumHeight(40); form->setVerticalSpacing(8);
    video->addItems({QStringLiteral("Выключено"), QStringLiteral("Тестовая картинка"), QStringLiteral("Камера по умолчанию"), QStringLiteral("Основной экран целиком")});
    form->addRow(QStringLiteral("Сервер"), url); form->addRow(QStringLiteral("Микрофон"), input);
    form->addRow(QStringLiteral("Наушники / динамики"), output); form->addRow(QStringLiteral("Видео"), video);
    layout->addLayout(form);
    auto* hardware = new QCheckBox(QStringLiteral("Пробовать аппаратный H.264 (эксперимент)")); layout->addWidget(hardware);
    auto* privacy = new QLabel(QStringLiteral("Микрофон и выбранная камера или экран передаются только после нажатия «Подключиться». Закрытие этого окна завершает передачу."));
    privacy->setWordWrap(true); layout->addWidget(privacy);
    auto* buttons = new QHBoxLayout;
    auto* join = new QPushButton(QStringLiteral("Подключиться")); join->setEnabled(false);
    auto* leave = new QPushButton(QStringLiteral("Отключиться")); leave->setEnabled(false);
    join->setObjectName("primary"); leave->setObjectName("secondary");
    auto* mute = new QCheckBox(QStringLiteral("Не передавать мой голос")); mute->setEnabled(false);
    buttons->addWidget(join); buttons->addWidget(leave); buttons->addWidget(mute); layout->addLayout(buttons);
    auto* status = new QLabel(QStringLiteral("Приглашение не загружено")); status->setWordWrap(true); layout->addWidget(status);
    auto* stats = new QLabel; layout->addWidget(stats);
    auto* encoder = new QLabel; layout->addWidget(encoder);
    auto* preview = new QLabel(QStringLiteral("Видео собеседника появится здесь"));
    preview->setAlignment(Qt::AlignCenter); preview->setMinimumSize(320, 120); layout->addWidget(preview, 1);
    MediaSession session;
    QString token;
    bool active = false;
    auto setBusy = [&](bool busy) {
        active = busy; join->setEnabled(!busy && !token.isEmpty()); leave->setEnabled(busy);
        load->setEnabled(!busy); url->setEnabled(!busy); input->setEnabled(!busy); output->setEnabled(!busy);
        video->setEnabled(!busy); hardware->setEnabled(!busy); mute->setEnabled(busy);
    };
    QObject::connect(load, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("Приглашение Nimbo"), {}, "Nimbo (*.nimbo)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) { status->setText(QStringLiteral("Не удалось прочитать приглашение")); return; }
        const auto obj = QJsonDocument::fromJson(file.readAll()).object();
        if (!obj.value("url").isString() || !obj.value("token").isString() || obj.value("token").toString().isEmpty()) {
            status->setText(QStringLiteral("Некорректное приглашение")); return;
        }
        token = obj.value("token").toString(); url->setText(obj.value("url").toString());
        status->setText(QStringLiteral("Приглашение загружено. Срок действия — 8 часов с момента создания."));
        setBusy(false);
    });
    QObject::connect(join, &QPushButton::clicked, &dialog, [&] {
        if (active) return;
        MediaOptions options; options.url = url->text().trimmed(); options.token = token;
        options.inputId = input->currentData().toByteArray(); options.outputId = output->currentData().toByteArray();
        options.videoMode = video->currentIndex(); options.hardwareVideo = hardware->isChecked();
        mute->setChecked(false); stats->clear(); preview->clear(); setBusy(true); session.start(options);
    });
    QObject::connect(leave, &QPushButton::clicked, &session, &MediaSession::stop);
    QObject::connect(mute, &QCheckBox::toggled, &session, &MediaSession::setMuted);
    QObject::connect(&session, &MediaSession::connectedChanged, &dialog, [&](bool connected) { setBusy(connected); });
    QObject::connect(&session, &MediaSession::statusChanged, status, &QLabel::setText);
    QObject::connect(&session, &MediaSession::audioWarning, status, &QLabel::setText);
    QObject::connect(&session, &MediaSession::encoderReported, &dialog, [=](const QString& implementation, bool) { encoder->setText(QStringLiteral("Кодировщик: ") + implementation); });
    QObject::connect(&session, &MediaSession::failed, &dialog, [&](const QString& message) { setBusy(false); status->setText(message); });
    QObject::connect(&session, &MediaSession::statistics, &dialog, [=](quint64 tx, quint64 rx, quint64 frames, double rms) {
        stats->setText(QStringLiteral("Аудио: отправлено %1 · получено %2 · уровень %3%\nКадров видео получено: %4")
            .arg(tx).arg(rx).arg(rms * 100, 0, 'f', 1).arg(frames));
    });
    QObject::connect(&session, &MediaSession::videoReceived, &dialog, [=](const QImage& frame) {
        preview->setPixmap(QPixmap::fromImage(frame).scaled(preview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    });
    dialog.exec();
}
