#pragma once
#include "screen_ink.h"
#include <QWidget>
#include <QTimer>
#include <memory>
#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <windows.h>
#include <dwmapi.h>
#undef SendMessage

class PresenterCanvas final : public QWidget {
public:
    ScreenInk::Strokes strokes;
    QRect source,crop;
protected:
    QRect inkArea()const{
        if(crop.isEmpty())return rect();
        const double sx=double(width())/crop.width(),sy=double(height())/crop.height();
        return QRect(qRound(-crop.x()*sx),qRound(-crop.y()*sy),
                     qRound(source.width()*sx),qRound(source.height()*sy));
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        ScreenInk::paint(painter,inkArea(),strokes);
    }
};

class PresenterInk final : public QObject {
public:
    PresenterInk(QScreen* screen,HWND window):monitor(screen),program(window){
        if(screen){
            connect(screen,&QScreen::geometryChanged,this,[this]{position();});
            connect(screen,&QObject::destroyed,this,[this]{canvas.reset();});
        }
        if(program){
            connect(&follow,&QTimer::timeout,this,[this]{position();});
            follow.start(150);
        }
    }
    ScreenInk::Strokes strokes;
    PresenterCanvas* surface()const{return canvas.get();}
    void refresh(){
        if(canvas){canvas->strokes=strokes;canvas->update();}
        position();
    }
    void position(){
        if(strokes.isEmpty()){canvas.reset();return;}
        RECT bounds{};
        if(program && (!IsWindowVisible(program) || IsIconic(program) ||
           FAILED(DwmGetWindowAttribute(program,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds))))){
            if(canvas)canvas->hide();return;
        }
        if(!program && !monitor)return;
        if(!canvas){
            canvas=std::make_unique<PresenterCanvas>();
            auto flags=Qt::Tool|Qt::FramelessWindowHint|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus;
            if(!program)flags|=Qt::WindowStaysOnTopHint;
            canvas->setWindowFlags(flags);
            canvas->setAttribute(Qt::WA_ShowWithoutActivating);
            canvas->setAttribute(Qt::WA_TranslucentBackground);
            canvas->setAttribute(Qt::WA_QuitOnClose,false);
            if(monitor)canvas->setScreen(monitor);
            canvas->strokes=strokes;
        }
        auto handle=reinterpret_cast<HWND>(canvas->winId());
        const QRect full=program?QRect(bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top)
                                :monitor->geometry();
        canvas->source=QRect(QPoint(),full.size());
        canvas->crop=canvas->source;
        {
            // A passive mark only needs backing pixels around the visible ink.
            const auto map=QTransform::fromScale(full.width(),full.height());
            const int padding=6+full.height()/150;
            QRect marks;
            for(const auto& stroke:strokes){
                auto box=map.mapRect(stroke.path.boundingRect());
                marks|=box.adjusted(-padding,-padding,padding,padding).toAlignedRect();
            }
            canvas->crop=marks.intersected(canvas->source);
        }
        const QRect target=canvas->crop.translated(full.topLeft());
        if(program){
            const bool appearing=!canvas->isVisible();
            if(appearing)canvas->show();
            auto above=GetWindow(program,GW_HWNDPREV);
            if(above!=handle || appearing || target!=lastBounds){
                UINT flags=SWP_NOACTIVATE;
                if(above==handle)flags|=SWP_NOZORDER;
                else if(!above || ((GetWindowLongPtrW(above,GWL_EXSTYLE)&WS_EX_TOPMOST) && !(GetWindowLongPtrW(program,GWL_EXSTYLE)&WS_EX_TOPMOST)))above=HWND_TOP;
                SetWindowPos(handle,above,target.x(),target.y(),target.width(),target.height(),flags);
                lastBounds=target;
            }
        }else canvas->setGeometry(target);
        if(!canvas->isVisible())canvas->show();
    }
private:
    QPointer<QScreen> monitor;
    HWND program;
    QRect lastBounds;
    QTimer follow;
    std::unique_ptr<PresenterCanvas> canvas;
};
