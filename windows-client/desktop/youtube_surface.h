#pragma once
#include <QWidget>
#include <QPointer>
#include <QImage>
#include <functional>
#include <memory>

class YoutubeSurface final : public QWidget {
public:
    YoutubeSurface(QWidget* parent,const QString& videoId,int startMs,const QString& playerFolder={});
    ~YoutubeSurface() override;
    quint32 browserProcessId() const;
    void trackCard();
    std::function<void()> started;
    std::function<void(int,QString,QImage)> finished;
protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    struct Browser;
    std::unique_ptr<Browser> browser;
    QPointer<QWidget> host;
    QPointer<QWidget> clipViewport;
    bool fullscreen=false;
    Qt::WindowStates savedState;
    QList<QPointer<QWidget>> ancestors;
    QMetaObject::Connection scrollConnection;
    void fit();
    void setFullscreen(bool enabled);
    void exitFullscreen();
};
