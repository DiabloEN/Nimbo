#pragma once
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QRasterWindow>
#include <QTimer>

// An artificial, repeatable capture target. The low byte of the pan offset is
// visible in the eight squares, so a decoded frame can be compared to its source.
class QualityWindow final : public QRasterWindow {
public:
    explicit QualityWindow(const QDir& fixture)
        : texture(fixture.filePath("texture.png")), hud(fixture.filePath("hud.png")),
          motionFile(fixture.filePath("motion")) {
        setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnBottomHint);
        setTitle("Nimbo QA "+fixture.dirName());
        resize(texture.size()/devicePixelRatio());
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer,&QTimer::timeout,this,[this]{
            if(!motion.isValid() && QFileInfo::exists(motionFile))motion.start();
            update();
        });
        timer.start(16);
    }
    bool valid()const{return !texture.isNull() && !hud.isNull();}
protected:
    void paintEvent(QPaintEvent*) override{
        const int sequence=motion.isValid()?int(motion.elapsed()*30/1000)%256:0;
        const int offset=sequence*8;
        QPainter p(this);p.setWindow(texture.rect());
        p.drawImage(-offset,0,texture);
        if(offset)p.drawImage(texture.width()-offset,0,texture);
        p.drawImage(30,60,hud);
        for(int bit=0;bit<8;++bit)
            p.fillRect(32+bit*32,12,28,28,sequence&(1<<bit)?Qt::white:Qt::black);
    }
private:
    QImage texture,hud;
    QString motionFile;
    QTimer timer;
    QElapsedTimer motion;
};
