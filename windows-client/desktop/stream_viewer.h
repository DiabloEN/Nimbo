#pragma once
#include "interface_text.h"
#include "ui.h"
#include "ink_input.h"
#include "ink_toolbar.h"
#include <optional>

class StreamSurface final : public InkInput {
public:
    QImage frame;
    QRect inkArea()const override{
        if(frame.isNull())return rect();
        const auto size=frame.size().scaled(this->size(),Qt::KeepAspectRatio);
        return QRect(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHints(QPainter::SmoothPixmapTransform|QPainter::Antialiasing);
        QPainterPath clip;clip.addRoundedRect(rect(),9,9);p.setClipPath(clip);p.fillRect(rect(),QColor("#050505"));
        if(frame.isNull())return;
        p.drawImage(inkArea(),frame);paintDraft(p);
    }
};
class StreamViewer final : public QWidget {
public:
    StreamViewer(QWidget* host,std::function<void()> toggleAudio,std::function<QRect()> bounds):QWidget(host),availableArea(std::move(bounds)){
        setFocusPolicy(Qt::StrongFocus);setObjectName("streamViewer");setAccessibleName(UiText::StreamViewer());setAttribute(Qt::WA_StyledBackground);
        setStyleSheet("QWidget#streamViewer{background:#161616;border-radius:16px}"
                      "QPushButton{background:#242424;border:0;border-radius:9px;padding:0}"
                      "QPushButton:hover,QPushButton:focus{background:#393939}");
        layout=new QVBoxLayout(this);layout->setSpacing(8);
        header=new QHBoxLayout;header->setSpacing(5);layout->addLayout(header);
        title=Ui::text("","font-size:12px;font-weight:500;padding-left:5px");title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);header->addWidget(title,1);
        mode=Ui::action("expand",UiText::ExpandStream(),30);header->addWidget(mode);
        auto* dismiss=Ui::action("close",UiText::CloseStreamViewer(),30);header->addWidget(dismiss);
        connect(mode,&QPushButton::clicked,this,[this]{setMini(!mini,true);});
        connect(dismiss,&QPushButton::clicked,this,[this]{clear();});
        surface=new StreamSurface;surface->setObjectName("streamSurface");layout->addWidget(surface,1);
        auto* guidance=new QVBoxLayout(surface);
        guidance->setContentsMargins(10,8,10,8);guidance->setAlignment(Qt::AlignTop|Qt::AlignHCenter);
        inkHint=new QLabel(surface);inkHint->setObjectName("inkHint");
        inkHint->setWordWrap(true);inkHint->setMaximumWidth(380);
        inkHint->setAttribute(Qt::WA_TransparentForMouseEvents);
        inkHint->setStyleSheet("background:#242424;color:#ededed;border:0;border-radius:8px;padding:6px 10px");
        guidance->addWidget(inkHint);inkHint->hide();
        inkCheck.setSingleShot(true);inkCheck.setInterval(2500);
        connect(&inkCheck,&QTimer::timeout,this,[this]{
            updateInkInput();
            if(onInk && !own && hasTransport && inkSession.isEmpty() && tools && tools->expanded())
                onInk(track.chopped(7),{{"type","query"}});
        });
        surface->submit=[this](QJsonObject command){submitInk(command);};
        sound=Ui::action("sound-off",UiText::StreamAudio(),30);
        header->insertWidget(1,sound);
        connect(sound,&QPushButton::clicked,this,std::move(toggleAudio));
        grip=new QWidget(this);grip->setObjectName("streamResizeGrip");grip->setCursor(Qt::SizeBDiagCursor);
        grip->setToolTip(UiText::Resize());grip->setFixedSize(18,18);
        auto* mark=Ui::text("◿");mark->setParent(grip);mark->setAttribute(Qt::WA_TransparentForMouseEvents);
        for(QWidget* target : QList<QWidget*>{title,surface,grip,this})target->installEventFilter(this);
        surface->setFocusPolicy(Qt::ClickFocus);
        auto* undo=new QShortcut(QKeySequence::Undo,this);
        undo->setContext(Qt::WidgetWithChildrenShortcut);
        connect(undo,&QShortcut::activated,this,[this]{
            if(mini || track.isEmpty() || inkSession.isEmpty() || (!own && !guestAllowed))return;
            surface->finishStroke();
            submitInk({{"type","undo"}});
        });
        auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);
        escape->setContext(Qt::WidgetWithChildrenShortcut);
        connect(escape,&QShortcut::activated,this,[this]{if(tools && tools->expanded())tools->setExpanded(false);else if(mini)clear();else setMini(true,true);});
        hide();
    }
    void open(const QString& key,const QString& name,const QImage& image,bool small,bool local,bool focus){
        if(track!=key){if(tools)tools->setExpanded(false);setInkSession({},false);}
        track=key;own=local;title->setText(name+UiText::Screen());setFrame(image);setMini(small,focus);show();raise();
        if(focus)setFocus(Qt::OtherFocusReason);
    }
    void setFrame(const QImage& image){
        surface->frame=image;
        surface->update();
    }
    void setSound(bool available,bool enabled){
        sound->setEnabled(available);sound->setIcon(Ui::icon(enabled?"sound":"sound-off"));
        sound->setAccessibleName(own?(enabled?UiText::TurnOffSharedAudio():UiText::ShareSystemAudio()):
            (enabled?UiText::MuteStreamForMe():UiText::UnmuteStreamForMe()));
        sound->setToolTip(available?sound->accessibleName():UiText::AudioWasNotEnabledWhenTheScreen());
    }
    void setMini(bool small,bool focus=false){
        mini=small;syncPen();
        mode->setAccessibleName(small?UiText::ExpandStream():UiText::MinimizeStream());
        mode->setToolTip(small?UiText::Expand():UiText::SwitchToMiniPlayer());
        if(small)mode->setIcon(Ui::icon("expand"));
        else{
            QPixmap dash(18,18);dash.fill(Qt::transparent);QPainter p(&dash);p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor("#ededed"),1.2,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(3.75,12.75),QPointF(14.25,12.75));p.end();mode->setIcon(QIcon(dash));
        }
        const int inset=small?8:10;layout->setContentsMargins(inset,inset,inset,inset);
        sound->setVisible(!own || !small);grip->setVisible(small);grip->raise();
        for(QWidget* target : QList<QWidget*>{title,surface})target->setCursor(small?Qt::OpenHandCursor:Qt::ArrowCursor);
        if(surface->drawing)surface->setCursor(Qt::CrossCursor);
        position(withMembers);
        if(focus)setFocus(Qt::OtherFocusReason);
    }
    void position(bool members){
        withMembers=members;
        const auto box=parentWidget()->rect();const bool narrow=box.width()<=800;
        if(mini){
            const QRect area=availableArea().intersected(box);
            if(area.isEmpty())return;
            const int maximum=qMax(1,qMin(area.width(),16+(area.height()-54)*16/9));
            const int w=qBound(1,preferredWidth.value_or(narrow?280:320),maximum);
            const int h=qMin(area.height(),54+qRound(qMax(0,w-16)*9./16));
            const int right=qBound(0,offset.x(),area.width()-w);
            const int top=qBound(0,offset.y(),area.height()-h);
            setGeometry(area.right()-w+1-right,area.top()+top,w,h);
            grip->move(2,height()-grip->height()-2);
        }else setGeometry(box.adjusted(narrow?16:box.width()<=1200?236:266,narrow?170:84,-16,-16));
    }
    void setInkSession(QString session,bool allowed=true){
        if(session!=inkSession && surface->drawing)surface->cancelStroke();
        inkSession=session;guestAllowed=allowed;syncPen();
    }
    void restoreInkColor(const QString& saved){
        surface->inkColor=ScreenInk::colors.contains(saved)?saved:ScreenInk::colors.first();
        if(tools)tools->selectColor(surface->inkColor);
    }
    std::function<void(const QString&)> onInkColorChanged;
    std::function<void(QString,QJsonObject)> onInk;
    void setInkTransport(bool available){hasTransport=available;syncPen();}
    void clear(){track.clear();setInkSession({},false);hide();surface->frame={};mini=false;}
    QString track;
    bool mini=false;
private:
    void submitInk(QJsonObject command){
        if(!onInk || inkSession.isEmpty())return;
        command["session"]=inkSession;onInk(track.chopped(7),command);
    }
    QString inkSession;
    QHBoxLayout* header=nullptr;
    bool guestAllowed=false,hasTransport=false;
    InkToolbar* tools=nullptr;
    QLabel* inkHint=nullptr;
    QTimer inkCheck;
    void updateInkInput(){
        const bool open=tools && tools->expanded() && !mini && !track.isEmpty();
        const bool writable=!inkSession.isEmpty() && (own || (hasTransport && guestAllowed));
        if(surface->drawing && !(open && writable))surface->cancelStroke();
        surface->drawing=open && writable;
        surface->setCursor(surface->drawing?Qt::CrossCursor:mini?Qt::OpenHandCursor:Qt::ArrowCursor);
        if(!open || !inkSession.isEmpty())inkCheck.stop();
        if(open && !writable){
            inkHint->setText(inkCheck.isActive()?UiText::InkChecking():hasTransport && !inkSession.isEmpty()?UiText::InkAskPresenter():UiText::InkUnavailable());
            inkHint->show();
        }else inkHint->hide();
    }
    void syncPen(){
        if(mini || track.isEmpty()){
            if(tools){tools->setExpanded(false);delete tools;tools=nullptr;}
            return;
        }
        if(!tools)createPen();
        tools->configure(own,!inkSession.isEmpty(),guestAllowed,hasTransport);tools->show();updateInkInput();
    }
    void createPen(){
        tools=new InkToolbar(this);header->insertWidget(1,tools);
        tools->onColor=[this](const QString& color){
            if(surface->inkColor==color)return;
            surface->inkColor=color;
            if(onInkColorChanged)onInkColorChanged(color);
        };
        tools->selectColor(surface->inkColor);
        tools->onClear=[this]{submitInk({{"type","clear"}});};
        tools->onExpand=[this](bool expanded){
            if(expanded && !own && hasTransport && inkSession.isEmpty())inkCheck.start();
            updateInkInput();
            if(expanded){
                surface->setFocus(Qt::OtherFocusReason);
                if(!own && hasTransport && !surface->drawing && onInk)
                    onInk(track.chopped(7),{{"type","query"}});
            }
        };
    }
    bool eventFilter(QObject* target,QEvent* event) override {
        if(!mini)return false;
        if(event->type()==QEvent::MouseButtonPress){
            auto* e=static_cast<QMouseEvent*>(event);
            if(e->button()!=Qt::LeftButton)return false;
            const QRect area=availableArea();
            dragging=true;resizing=target==grip;press=e->globalPosition().toPoint();
            startOffset={area.right()-geometry().right(),geometry().top()-area.top()};startWidth=width();
            return true;
        }
        if(dragging && event->type()==QEvent::MouseMove){
            auto* e=static_cast<QMouseEvent*>(event);
            const QPoint delta=e->globalPosition().toPoint()-press;
            const QRect area=availableArea();
            if(resizing){
                const int growth=qAbs(delta.x())>=qAbs(delta.y())?-delta.x():qRound(delta.y()*16./9);
                preferredWidth=qMax(180,qMin(area.width(),startWidth+growth));
            }else offset={qBound(0,startOffset.x()-delta.x(),qMax(0,area.width()-width())),
                         qBound(0,startOffset.y()+delta.y(),qMax(0,area.height()-height()))};
            position(withMembers);return true;
        }
        if(event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::UngrabMouse)dragging=false;
        return false;
    }
    std::function<QRect()> availableArea;
    QPoint offset,press,startOffset;
    std::optional<int> preferredWidth;
    int startWidth=0;
    bool dragging=false,resizing=false;
    QWidget* grip;
    bool own=false,withMembers=false;
    QLabel* title;
    QVBoxLayout* layout;
    QPushButton *mode,*sound;
    StreamSurface* surface;
};
