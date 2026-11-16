#pragma once
#include "interface_text.h"
#include "ui.h"

// The media surface and avatar are painted within the card's rounded clip.
// Child controls overlay them without contributing to the card's size hint.
class VoiceCard final : public QWidget {
public:
    VoiceCard(int width,QString name,bool local,bool speaking,bool micOff,QPixmap photo,bool visual,bool screen=false,bool grouped=false,bool narrow=false):
        name(std::move(name)),photo(std::move(photo)),mine(local),speaks(speaking),quiet(micOff),visual(visual),screen(screen),grouped(grouped),narrow(narrow){
        setAutoFillBackground(false);setStyleSheet("QWidget#screenCard,QWidget#cameraCard,QWidget#voiceCard{background:transparent}");
        setObjectName(screen?"screenCard":visual?"cameraCard":"voiceCard");
        int h=visual||grouped?qRound(width*9./16)+(grouped?48:47):(narrow?170:190);
        setFixedSize(width,h);setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        setAccessibleName(this->name+(mine?UiText::You():""));setMouseTracking(true);
        state=new QLabel(this);state->setAttribute(Qt::WA_TransparentForMouseEvents);state->setStyleSheet("background:transparent");if(quiet)state->setPixmap(Ui::icon("mic-off").pixmap(14,14));
        state->setGeometry(width-31,15,17,18);state->setVisible(!speaks&&quiet);
        connect(qApp,&QApplication::focusChanged,this,[this]{updateOverlays();});
    }
    void setMicrophone(bool speaking,bool micOff) {
        if(speaks==speaking && quiet==micOff)return;
        speaks=speaking;quiet=micOff;
        if(quiet)state->setPixmap(Ui::icon("mic-off").pixmap(14,14));
        state->setVisible(!speaks&&quiet);
        update();
    }
    void setFrame(const QImage& img){frame=img;update();}
    void addOverlay(QPushButton* button,bool always=false){
        button->setParent(this);button->move(screen?width()-button->width()-10:10,10);
        if(always)button->show();else addHover(button,140);
    }
    void addModeration(QPushButton* button) {
        if(!tools){
            tools=new QWidget(this);tools->setAttribute(Qt::WA_StyledBackground);tools->setObjectName("mediaAdminActions");
            tools->setStyleSheet("QWidget#mediaAdminActions{background:rgba(32,32,32,232);border-radius:10px} "
                                 "QPushButton{background:transparent;border:0;border-radius:8px;padding:0} QPushButton:hover{background:#393939}");
            auto* row=new QHBoxLayout(tools);row->setContentsMargins(4,4,4,4);row->setSpacing(4);
            addHover(tools,100);
        }
        button->setIconSize({18,18});button->setStyleSheet("QPushButton{padding:0;background:transparent;border:0;border-radius:8px} QPushButton:hover{background:#393939}");
        tools->layout()->addWidget(button);tools->adjustSize();
        tools->move(screen?10:width()-10-tools->width(),height()-tools->height()-(screen?48:40));
    }
protected:
    void enterEvent(QEnterEvent* e)override{QWidget::enterEvent(e);updateOverlays();}
    void leaveEvent(QEvent* e)override{QWidget::leaveEvent(e);updateOverlays();}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);QPainterPath shape;shape.addRoundedRect(rect(),18,18);p.setClipPath(shape);p.fillRect(rect(),QColor(speaks?"#242424":"#151515"));
        int top=0;bool hasSlot=visual||grouped;
        if(hasSlot){
            top=qRound(width()*9./16);QRect slot(0,0,width(),top);p.fillRect(slot,QColor(visual?"#050505":"#202020"));
            if(!frame.isNull()){
                if(screen){auto size=frame.size().scaled(slot.size(),Qt::KeepAspectRatio);p.drawImage(QRect(QPoint((width()-size.width())/2,(top-size.height())/2),size),frame);}
                else{p.save();p.translate(slot.width(),0);p.scale(-1,1);auto crop=frame.size().scaled(slot.size(),Qt::KeepAspectRatioByExpanding);double factor=double(frame.width())/crop.width();QSizeF source(width()*factor,top*factor);p.drawImage(slot,frame,QRectF((frame.width()-source.width())/2,(frame.height()-source.height())/2,source.width(),source.height()));p.restore();}
            }
        }
        if(!visual){int d=narrow?45:54;auto pix=Ui::avatar(name,photo,d);auto y=hasSlot?(top-d)/2:narrow?33:38;p.drawPixmap((width()-d)/2,y,pix);}
        QFont f("Roboto");f.setPixelSize(13);f.setWeight(QFont::Medium);p.setFont(f);p.setPen(QColor("#ededed"));
        const QString label=(screen?UiText::Screen2():"")+name;auto mineMark=mine&&!screen?QString::fromUtf8(UiText::You()):QString();
        int margin=hasSlot?18:20;int markWidth=mineMark.isEmpty()?0:27;
        QString elided=p.fontMetrics().elidedText(label,Qt::ElideRight,qMax(1,width()-margin*2-markWidth));
        int tw=p.fontMetrics().horizontalAdvance(elided);int x=hasSlot?margin:(width()-tw-markWidth)/2;
        int cy=hasSlot?top+(grouped?24:23):(narrow?105:119);
        p.drawText(QRect(x,cy-10,tw+1,20),Qt::AlignVCenter|Qt::AlignLeft,elided);
        if(!mineMark.isEmpty()){f.setPixelSize(11);p.setFont(f);p.setPen(QColor("#a0a0a0"));p.drawText(QRect(x+tw,cy-10,32,20),Qt::AlignVCenter|Qt::AlignLeft,mineMark);}
        if(speaks && !screen){
            const int inset=hasSlot?12:16, padding=hasSlot?7:0;
            const QRectF badge(width()-inset-14-2*padding,inset,14+2*padding,16+2*padding);
            p.setPen(Qt::NoPen);
            if(hasSlot){p.setBrush(QColor(17,17,17,204));p.drawRoundedRect(badge,9,9);}
            p.setBrush(QColor("#ccc"));
            constexpr int bars[]={7,14,10,7};
            for(int i=0;i<4;++i){
                const qreal x=badge.left()+padding+i*4, y=badge.center().y()-bars[i]/2.;
                p.drawRoundedRect(QRectF(x,y,2,bars[i]),1,1);
            }
        }
    }
private:
    QString name;QPixmap photo;QImage frame;bool mine,speaks,quiet,visual,screen,grouped,narrow;QLabel* state;QWidget* tools=nullptr;
    QList<QPropertyAnimation*> fades;
    void addHover(QWidget* widget,int milliseconds){
        auto* opacity=new QGraphicsOpacityEffect(widget);widget->setGraphicsEffect(opacity);
        const bool visible=underMouse();opacity->setOpacity(visible?1.:0.);
        widget->setAttribute(Qt::WA_TransparentForMouseEvents,!visible);
        auto* fade=new QPropertyAnimation(opacity,"opacity",widget);fade->setDuration(Ui::animationsEnabled()?milliseconds:0);fades.append(fade);widget->show();
    }
    void updateOverlays(){
        const bool on=underMouse() || isAncestorOf(QApplication::focusWidget());
        for(auto* fade:fades){
            auto* effect=static_cast<QGraphicsOpacityEffect*>(fade->targetObject());
            static_cast<QWidget*>(fade->parent())->setAttribute(Qt::WA_TransparentForMouseEvents,!on);
            const qreal target=on?1.:0.;
            if(fade->state()==QAbstractAnimation::Running && fade->endValue().toReal()==target)continue;
            fade->stop();
            if(qFuzzyCompare(effect->opacity()+1,target+1))continue;
            fade->setStartValue(effect->opacity());fade->setEndValue(target);fade->start();
        }
    }
};
