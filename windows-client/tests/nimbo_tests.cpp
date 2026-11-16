#include "audio_probe.h"
#include "local_store.h"
#include "main_window.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QListView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

class NimboTests : public QObject {
    Q_OBJECT
    QTemporaryDir data{QDir::currentPath() + "/nimbo-test-XXXXXX"};
private slots:
    void initTestCase() {
        QVERIFY(data.isValid());
        qputenv("NIMBO_DATA_DIR", data.path().toUtf8());
        QCoreApplication::setOrganizationName("NimboTests");
        QCoreApplication::setApplicationName("NimboTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
        QApplication::setStyle("Fusion");
        const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        for (const auto *file : {"segoeui.ttf", "segoeuib.ttf", "seguisb.ttf"})
            QFontDatabase::addApplicationFont(fonts + file);
        QApplication::setFont(QFont("Segoe UI", 10));
    }

    void historySurvivesRestart() {
        int seedCount = 0;
        const QString body = QStringLiteral("Привет, Nimbo!\n' ; DROP TABLE messages; -- <b>текст</b>");
        {
            LocalStore store;
            QVERIFY2(store.open(), qPrintable(store.errorString()));
            seedCount = store.messages("general").size();
            QVERIFY(seedCount > 0);
            QVERIFY(!store.messages("general").first().own);
            store.setProfileName(QStringLiteral("Аня"));
            QVERIFY(store.errorString().isEmpty());
            QVERIFY(store.addMessage("qa-history", store.profileName(), body));
        }
        LocalStore reopened;
        QVERIFY(reopened.open());
        QCOMPARE(reopened.profileName(), QStringLiteral("Аня"));
        QCOMPARE(reopened.messages("general").size(), seedCount);
        const auto messages = reopened.messages("qa-history");
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().body, body);
        QCOMPARE(messages.first().author, QStringLiteral("Аня"));
        QVERIFY(messages.first().own);
        QVERIFY(messages.first().createdAt.isValid());
        QVERIFY(reopened.messages("other-channel").isEmpty());
    }

    void inputBoundsAndHistoryWindow() {
        LocalStore store;
        QVERIFY(store.open());
        QVERIFY(!store.addMessage("qa-bounds", "a", " \n\t "));
        QVERIFY(!store.errorString().isEmpty());
        QVERIFY(!store.addMessage("qa-bounds", "a", QString(4001, 'x')));
        QVERIFY(store.addMessage("qa-bounds", "a", QString(4000, 'x')));
        for (int i = 0; i < 302; ++i)
            QVERIFY(store.addMessage("qa-window", "a", QString::number(i)));
        const auto messages = store.messages("qa-window");
        QCOMPARE(messages.size(), 300);
        QCOMPARE(messages.first().body, QStringLiteral("2"));
        QCOMPARE(messages.last().body, QStringLiteral("301"));
    }

    void chatNavigationAndSend() {
        MainWindow window;
        window.show();
        auto *composer = window.findChild<QPlainTextEdit *>("messageComposer");
        auto *send = window.findChild<QPushButton *>("sendButton");
        auto *ideas = window.findChild<QPushButton *>("channelIdeas");
        auto *general = window.findChild<QPushButton *>("channelGeneral");
        auto *list = window.findChild<QListView *>("messageList");
        QVERIFY(composer && send && ideas && general && list);
        composer->setPlainText(QStringLiteral("Черновик общего"));
        QTest::mouseClick(ideas, Qt::LeftButton);
        QVERIFY(composer->toPlainText().isEmpty());
        composer->setPlainText(QStringLiteral("Черновик идей"));
        QTest::mouseClick(general, Qt::LeftButton);
        QCOMPARE(composer->toPlainText(), QStringLiteral("Черновик общего"));
        composer->setPlainText(QStringLiteral("Отправлено через интерфейс"));
        QTest::keyClick(composer, Qt::Key_Return);
        QVERIFY(composer->toPlainText().isEmpty());
        LocalStore reader;
        QVERIFY(reader.open());
        QCOMPARE(reader.messages("general").last().body, QStringLiteral("Отправлено через интерфейс"));
        QTest::mouseClick(ideas, Qt::LeftButton);
        QCOMPARE(composer->toPlainText(), QStringLiteral("Черновик идей"));
        QTest::keyClick(composer, Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(composer->toPlainText().contains('\n'));
        QTest::mouseClick(send, Qt::LeftButton);
        QVERIFY(composer->toPlainText().isEmpty());
        QCOMPARE(reader.messages("ideas").last().body, QStringLiteral("Черновик идей"));
    }

    void settingsDoNotStartMicrophone() {
        MainWindow window;
        window.show();
        auto *button = window.findChild<QToolButton *>("settingsButton");
        QVERIFY(button);
        bool checked = false;
        bool idle = false;
        bool devicesPresent = false;
        QTimer::singleShot(150, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *probe = dialog->findChild<AudioProbe *>();
            checked = true;
            idle = probe && !probe->isRunning();
            devicesPresent = dialog->findChildren<QComboBox *>().size() >= 2;
            const auto artifacts = QCoreApplication::applicationDirPath() + "/../artifacts";
            QDir().mkpath(artifacts);
            dialog->grab().save(artifacts + "/settings.png");
            auto *tabs = dialog->findChild<QTabWidget *>();
            auto *name = dialog->findChild<QLineEdit *>();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (tabs && name && buttons) {
                tabs->setCurrentIndex(1);
                name->setText(QStringLiteral("Саша"));
                for (auto *action : buttons->buttons()) {
                    if (buttons->buttonRole(action) == QDialogButtonBox::AcceptRole) {
                        QTest::mouseClick(action, Qt::LeftButton);
                        return;
                    }
                }
            }
            dialog->reject();
        });
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(checked);
        QVERIFY(idle);
        QVERIFY(devicesPresent);
        LocalStore store;
        QVERIFY(store.open());
        QCOMPARE(store.profileName(), QStringLiteral("Саша"));
    }

    void missingAudioDeviceFailsCleanly() {
        AudioProbe probe;
        QSignalSpy errors(&probe, &AudioProbe::errorOccurred);
        QVERIFY(!probe.isRunning());
        QVERIFY(!probe.start("nimbo-device-that-does-not-exist"));
        QVERIFY(!probe.isRunning());
        QCOMPARE(errors.size(), 1);
        probe.stop();
        probe.stop();
        QVERIFY(!probe.isRunning());
    }
};

QTEST_MAIN(NimboTests)
#include "nimbo_tests.moc"
