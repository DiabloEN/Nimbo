#pragma once
#include <QCapturableWindow>
#include <QString>
#include <optional>

class QWidget;
struct ShareSource {
    QString screen;
    QCapturableWindow window;
};
std::optional<ShareSource> chooseShareSource(QWidget* parent);
