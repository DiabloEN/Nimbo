#pragma once
#include "interface_text.h"
#include "ui.h"

class OrderHandle final : public QPushButton {
public:
    OrderHandle(QWidget* block,QVBoxLayout* peers,QString title,QStringList channels,std::function<void(int)> step)
        :block(block),peers(peers),title(std::move(title)),channels(std::move(channels)),step(std::move(step)){
        setIcon(Ui::tintedIcon("drag",QColor("#777")));setIconSize({20,20});setFixedSize(28,28);
        setToolTip(UiText::DragOrUseTheUpAndDown());setCursor(Qt::OpenHandCursor);
    }
    ~OrderHandle() override {delete ghost.data();}
protected:
    void keyPressEvent(QKeyEvent* e) override{
        if(e->key()==Qt::Key_Escape && pressed){finish(false);e->accept();return;}
        if(!pressed && (e->key()==Qt::Key_Up || e->key()==Qt::Key_Down)){
            step(e->key()==Qt::Key_Up?-1:1);e->accept();return;
        }
        QPushButton::keyPressEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override{
        if(e->button()!=Qt::LeftButton)return;
        pressed=true;setFocus(Qt::MouseFocusReason);start=e->globalPosition().toPoint();
        offset=block->mapFromGlobal(start);original=peers->indexOf(block);e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e) override{
        if(!pressed || !(e->buttons()&Qt::LeftButton))return;
        const auto global=e->globalPosition().toPoint();
        if(!ghost){
            if(QLineF(start,global).length()<5)return;
            showGhost();setCursor(Qt::ClosedHandCursor);
            auto* opacity=new QGraphicsOpacityEffect(block);opacity->setOpacity(.35);block->setGraphicsEffect(opacity);
        }
        ghost->move(window()->mapFromGlobal(global)-offset);ghost->raise();
        const int from=peers->indexOf(block);
        const auto pt=peers->parentWidget()->mapFromGlobal(global);
        for(int i=0;i<peers->count();++i){
            auto* target=peers->itemAt(i)->widget();
            if(!target || target==block || !target->property("orderId").isValid())continue;
            const auto bounds=slotsByWidget.value(target,target->geometry());
            if(!bounds.contains(pt))continue;
            const auto inside=pt.y()-bounds.top();
            if((i>from && inside>=bounds.height()*.55)||(i<from && inside<=bounds.height()*.45))moveTo(i);
            break;
        }
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override{
        if(e->button()==Qt::LeftButton && pressed){finish(true);e->accept();}
    }
    bool event(QEvent* e) override{
        if(pressed && (e->type()==QEvent::UngrabMouse || e->type()==QEvent::WindowDeactivate))finish(false);
        return QPushButton::event(e);
    }
private:
    void showGhost(){
        ghost=new QFrame(window());ghost->setObjectName("channelDragGhost");
        ghost->setAttribute(Qt::WA_TransparentForMouseEvents);ghost->setAttribute(Qt::WA_StyledBackground);
        ghost->setStyleSheet("#channelDragGhost{background:#292929;border-radius:12px} QLabel{background:transparent;color:#eee}");
        auto* layout=new QVBoxLayout(ghost);layout->setContentsMargins(16,16,16,16);layout->setSpacing(12);
        auto* header=new QHBoxLayout;auto* grip=new QLabel;grip->setPixmap(Ui::icon("drag").pixmap(18,18));
        header->addWidget(grip);header->addWidget(Ui::text(title,"font-weight:500"),1);layout->addLayout(header);
        for(const auto& name:channels){
            auto* row=new QHBoxLayout;row->setContentsMargins(26,0,0,0);
            row->addWidget(Ui::text(name,"background:#202020;border-radius:8px;padding:10px"));layout->addLayout(row);
        }
        auto* shadow=new QGraphicsDropShadowEffect(ghost);shadow->setOffset(0,12);shadow->setBlurRadius(32);shadow->setColor(QColor(0,0,0,136));ghost->setGraphicsEffect(shadow);
        ghost->setFixedWidth(block->width());ghost->adjustSize();ghost->show();
    }
    void moveTo(int index){
        QHash<QWidget*,QPoint> before;
        for(int i=0;i<peers->count();++i){
            auto* w=peers->itemAt(i)->widget();if(!w || !w->property("orderId").isValid())continue;
            if(auto* old=w->findChild<QPropertyAnimation*>("orderMotion",Qt::FindDirectChildrenOnly))delete old;
            before.insert(w,w->pos());
        }
        peers->removeWidget(block);peers->insertWidget(index,block);peers->activate();
        for(auto it=before.cbegin();it!=before.cend();++it){
            auto* w=it.key();slotsByWidget[w]=w->geometry();
            if(it.value()==w->pos() || !Ui::animationsEnabled())continue;
            auto* fade=new QPropertyAnimation(w,"pos",w);fade->setObjectName("orderMotion");
            QEasingCurve ease(QEasingCurve::BezierSpline);
            ease.addCubicBezierSegment({.25,.1},{.25,1.},{1.,1.});
            fade->setDuration(140);fade->setStartValue(it.value());fade->setEndValue(w->pos());fade->setEasingCurve(ease);
            fade->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
    void finish(bool commit){
        pressed=false;setCursor(Qt::OpenHandCursor);
        if(!ghost)return;
        delete ghost.data();block->setGraphicsEffect(nullptr);slotsByWidget.clear();
        const auto delta=peers->indexOf(block)-original;
        if(!delta)return;
        if(commit)step(delta);else moveTo(original);
        slotsByWidget.clear();
    }
    QWidget* block;QVBoxLayout* peers;QString title;QStringList channels;
    std::function<void(int)> step;QPointer<QFrame> ghost;
    QHash<QWidget*,QRect> slotsByWidget;QPoint start,offset;int original=0;bool pressed=false;
};
