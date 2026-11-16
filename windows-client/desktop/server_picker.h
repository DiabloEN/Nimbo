#pragma once
#include "interface_text.h"
#include "ui.h"
#include "profile.h"
#include <algorithm>

class ServerCardButton final : public QPushButton {
public:
    explicit ServerCardButton(bool active):rest(active?"#343c39":"#252525"),fill(rest),active(active){
        setAttribute(Qt::WA_TranslucentBackground);setAutoFillBackground(false);
        transition.setDuration(Ui::animationsEnabled()?160:0);
        QEasingCurve ease(QEasingCurve::BezierSpline);ease.addCubicBezierSegment({.25,.1},{.25,1},{1,1});transition.setEasingCurve(ease);
        connect(&transition,&QVariantAnimation::valueChanged,this,[this](const QVariant& value){fill=value.value<QColor>();update();});
    }
    void emphasize(bool on){
        QColor target=on&&!active?QColor("#373737"):rest;
        transition.stop();if(target==fill)return;
        transition.setStartValue(fill);transition.setEndValue(target);transition.start();
    }
protected:
    void paintEvent(QPaintEvent*) override{
        QPainter paint(this);paint.setRenderHint(QPainter::Antialiasing);
        paint.setPen(Qt::NoPen);paint.setBrush(fill);paint.drawRoundedRect(QRectF(rect()),12,12);
    }
private:
    QColor rest,fill;bool active;QVariantAnimation transition;
};

class ServerCard final : public QGraphicsProxyWidget {
public:
    explicit ServerCard(int index):index(index),angle(qMin(index,3)*4),motion(this,"rotation") {
        QEasingCurve easing(QEasingCurve::BezierSpline);easing.addCubicBezierSegment({.25,.1},{.25,1},{1,1});
        motion.setDuration(Ui::animationsEnabled()?160:0);motion.setEasingCurve(easing);
        setTransformOriginPoint(24,0);setRotation(angle);setAcceptHoverEvents(true);
    }
    void paint(QPainter* painter,const QStyleOptionGraphicsItem* option,QWidget* target) override {
        if(qFuzzyIsNull(rotation()) || !widget()){
            QGraphicsProxyWidget::paint(painter,option,target);
            return;
        }
        // Rasterize before rotating so the transparent edge gets fractional pixel coverage.
        const qreal scale=qMax(2.,painter->device()->devicePixelRatioF()*2.);
        const QSize logical=widget()->size()+QSize(4,4);
        QImage image((QSizeF(logical)*scale).toSize(),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(scale);image.fill(Qt::transparent);
        QPainter surface(&image);surface.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        widget()->render(&surface,QPoint(2,2),QRegion(),QWidget::DrawChildren);
        surface.end();painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(QPointF(-2,-2),image);
    }
    void mousePressEvent(QGraphicsSceneMouseEvent* e)override{
        if(e->button()!=Qt::LeftButton){e->ignore();return;}
        pressed=true;pressPoint=e->screenPos();e->accept();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e)override{
        if(e->button()!=Qt::LeftButton || !pressed){e->ignore();return;}
        pressed=false;e->accept();
        // Hit-test the whole card, including its child labels, while its tilt is animating.
        if(boundingRect().contains(e->pos()) || (e->screenPos()-pressPoint).manhattanLength()<QApplication::startDragDistance())
            static_cast<QPushButton*>(widget())->click();
    }
    void updateScroll(int scroll){angle=qBound(0.,index-scroll/66.,3.)*4;if(!isUnderMouse()&&!hasFocus())setRotation(angle);}
    void hoverEnterEvent(QGraphicsSceneHoverEvent* e) override { animate(true);QGraphicsProxyWidget::hoverEnterEvent(e); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override { animate(hasFocus());QGraphicsProxyWidget::hoverLeaveEvent(e); }
    void focusInEvent(QFocusEvent* e) override {animate(true);QGraphicsProxyWidget::focusInEvent(e);}
    void focusOutEvent(QFocusEvent* e) override {animate(false);QGraphicsProxyWidget::focusOutEvent(e);}
private:
    void animate(bool straight){
        if(auto* button=static_cast<ServerCardButton*>(widget()))button->emphasize(straight);
        motion.stop();motion.setStartValue(rotation());motion.setEndValue(straight?0:angle);setZValue(straight?10:0);motion.start();}
    int index; qreal angle;QPropertyAnimation motion;bool pressed=false;QPoint pressPoint;
};

class ServerPicker final : public QGraphicsView {
public:
    ServerPicker(QPushButton* trigger,const QJsonArray& entries,const QUrl& current,std::function<void(QJsonObject)> select):QGraphicsView(trigger->window()),trigger(trigger) {
        setAttribute(Qt::WA_DeleteOnClose);setFocusPolicy(Qt::StrongFocus);
        setObjectName("serverMenu");setAccessibleName(UiText::MyServers()+current.authority());setFrameShape(QFrame::NoFrame);setStyleSheet("QGraphicsView#serverMenu{background:transparent;border:0}");
        auto palette=viewport()->palette();palette.setBrush(QPalette::Base,Qt::transparent);viewport()->setPalette(palette);viewport()->setAutoFillBackground(false);setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* scene=new QGraphicsScene(this);setScene(scene);setAlignment(Qt::AlignTop|Qt::AlignLeft);
        QList<QJsonObject> sorted;for(auto v:entries)sorted.append(v.toObject());
        auto priority=[&](const auto& s){return DesktopProfile::origin(s["url"].toString())==current?2:int(s["pinned"].toBool());};
        std::stable_sort(sorted.begin(),sorted.end(),[&](const auto& a,const auto& b){return priority(a)>priority(b);});
        for(int i=0;i<sorted.size();++i){
            auto s=sorted[i];QString name=s["name"].toString(),address=QUrl(s["url"].toString()).authority();
            const bool active=priority(s)==2;
            auto* b=new ServerCardButton(active);b->setFixedSize(264,64);b->setCursor(Qt::PointingHandCursor);b->setAccessibleName(name+" "+address);
            b->setProperty("currentServer",active);
            auto* r=new QHBoxLayout(b);r->setContentsMargins(12,10,12,10);r->setSpacing(10);
            auto* initial=Ui::text(name.left(1));initial->setAlignment(Qt::AlignCenter);initial->setFixedSize(34,34);initial->setStyleSheet("background:#ddd;color:#222;font-size:16px;border-radius:10px");r->addWidget(initial);
            auto* labels=new QVBoxLayout;labels->setSpacing(4);labels->setAlignment(Qt::AlignVCenter);r->addLayout(labels,1);
            for(auto item:{qMakePair(name,12),qMakePair(address,10)}){auto* l=Ui::text(item.first,QString("font-weight:%1;font-size:%2px;color:%3").arg(item.second==12?500:400).arg(item.second).arg(item.second==12?"#eee":"#999"));l->setText(l->fontMetrics().elidedText(item.first,Qt::ElideRight,168));labels->addWidget(l);}
            if(s["pinned"].toBool()){auto* pin=Ui::text("");pin->setPixmap(Ui::icon("pin").pixmap(14,14));r->addWidget(pin);}
            auto* card=new ServerCard(i);scene->addItem(card);card->setWidget(b);cards.append(card);auto* shadow=new QGraphicsDropShadowEffect;shadow->setOffset(0,3);shadow->setBlurRadius(12);shadow->setColor(QColor(0,0,0,85));card->setGraphicsEffect(shadow);card->setPos(8,8+i*66);
            connect(b,&QPushButton::clicked,this,[this,select,s]{close();select(s);});
        }
        const int height=42+66*sorted.size();scene->setSceneRect(0,0,306,height);setFixedSize(306,qMin(height,270));
        connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this](int y){for(auto* card:cards)card->updateScroll(y);});
        trigger->setProperty("menuOpen",true);
        trigger->style()->unpolish(trigger);trigger->style()->polish(trigger);
        qApp->installEventFilter(this);
        effect=new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
        motion.setDuration(Ui::animationsEnabled()?140:0);
        QEasingCurve ease(QEasingCurve::BezierSpline);
        ease.addCubicBezierSegment({.25,.1},{.25,1},{1,1});motion.setEasingCurve(ease);
        motion.setStartValue(0.);
        motion.setEndValue(1.);
        connect(&motion,&QVariantAnimation::valueChanged,this,[this](QVariant value){place(value.toReal());});
        connect(&motion,&QVariantAnimation::finished,this,[this]{if(motion.direction()==QAbstractAnimation::Backward)close();});
        place(0);
        show();
        raise();
        setFocus();
        motion.start();
    }
    void closeEvent(QCloseEvent* event) override {
        trigger->setProperty("menuOpen",false);
        trigger->style()->unpolish(trigger);trigger->style()->polish(trigger);
        QGraphicsView::closeEvent(event);
        if(restoreTriggerFocus){
            auto* button=trigger;
            QTimer::singleShot(0,button,[button]{if(button->isVisible())button->setFocus(Qt::ShortcutFocusReason);});
        }
    }
    void dismiss(){
        motion.setDirection(QAbstractAnimation::Backward);
        if(motion.state()!=QAbstractAnimation::Running)motion.start();
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(!isVisible())return false;
        if((object==parentWidget() && event->type()==QEvent::Resize) ||
           (object==trigger && event->type()==QEvent::Move))
            QTimer::singleShot(0,this,[this]{place(motion.currentValue().toReal());});
        if(event->type()==QEvent::MouseButtonPress){
            const auto global=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            if(!rect().contains(mapFromGlobal(global)) && !trigger->rect().contains(trigger->mapFromGlobal(global)))dismiss();
        }
        if(event->type()==QEvent::KeyPress && window()->isActiveWindow()){
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Escape){
                restoreTriggerFocus=true;dismiss();return true;
            }
            if(key->key()==Qt::Key_Tab || key->key()==Qt::Key_Backtab){
                bool backwards=key->key()==Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier);
                const int next=focusIndex+(backwards?-1:1);
                if(next<0 || next>=cards.size()){
                    close();trigger->setFocus(Qt::TabFocusReason);
                    return false;
                }
                focusIndex=next;
                scene()->setFocusItem(cards[next],Qt::TabFocusReason);
                cards[next]->widget()->setFocus(Qt::TabFocusReason);
                ensureVisible(cards[next],0,0);
                return true;
            }
            if(focusIndex>=0 && (key->key()==Qt::Key_Enter || key->key()==Qt::Key_Return || key->key()==Qt::Key_Space)){
                static_cast<QPushButton*>(cards[focusIndex]->widget())->click();return true;
            }
        }
        return false;
    }
    void keyPressEvent(QKeyEvent* e) override {
        if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter){
            for(auto* card:cards)if(card->hasFocus()){
                static_cast<QPushButton*>(card->widget())->click();return;
            }
        }
        QGraphicsView::keyPressEvent(e);
    }
private:
    void place(qreal progress){
        QPoint offset=parentWidget()->width()<=800 ? QPoint(trigger->width()-width()+8,trigger->height()+6) : QPoint(trigger->width()+10,-8);
        auto pos=trigger->mapTo(parentWidget(),offset);
        move(qBound(0,pos.x(),qMax(0,parentWidget()->width()-width()))-qRound(6*(1-progress)),pos.y());
        effect->setOpacity(progress);
        setTransform(QTransform::fromScale(.97+.03*progress,.97+.03*progress));
    }
    QPushButton* trigger;
    QList<ServerCard*> cards;
    int focusIndex=-1;
    bool restoreTriggerFocus=false;
    QGraphicsOpacityEffect* effect;
    QVariantAnimation motion;
};
