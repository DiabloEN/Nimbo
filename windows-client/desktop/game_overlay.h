#pragma once
#include "api.h"
#include <memory>

#include <QJsonArray>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QWidget>

struct OverlayPreferences {
    bool enabled = false;
    int opacity = 85;
    int backgroundOpacity = 0;
    QPoint position{0, 0}; // Percentage of the available travel on the chosen screen.
    QString screen;
};

class GameOverlay final : public QWidget {
public:
    explicit GameOverlay(QWidget& mainWindow);
    ~GameOverlay() override;
    void configure(const OverlayPreferences& preferences);
    void setPeople(const QJsonObject& local, const QJsonArray& peers);
    void syncPortraits(const QHash<QString,QPixmap>* roomAvatars, const ServerApi& call);
    void languageChanged();

private:
    struct Person {
        QString id, name;
        bool speaking;
        bool operator==(const Person&) const = default;
    };
    void paintEvent(QPaintEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;
    void place();
    int visibleRows() const;
    QList<Person> visiblePeople() const;
    OverlayPreferences preferences;
    QWidget& mainWindow;
    void* foregroundHook=nullptr;
    QList<Person> people;
    QPointer<QScreen> target;
    QMetaObject::Connection geometryChanged;
    struct Portrait { QString name; qint64 sourceKey = 0; QPixmap image; };
    QHash<QString,Portrait> portraits;
    QSet<QString> requested;
    std::unique_ptr<ServerApi> portraitApi;
};
