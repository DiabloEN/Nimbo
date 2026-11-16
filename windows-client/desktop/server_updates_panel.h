#pragma once
#include "interface_text.h"
#include "api.h"
#include "ui.h"
#include "wrap_layout.h"
#include "desktop_updates.h"

// Only the selected server's owner can issue server requests. Client release state is app-wide.
class ServerUpdatesPanel final : public QWidget {
public:
    using ServerOffer=std::function<QString(const QJsonObject&)>;
    using MarkRead=std::function<void(const QString&,const QString&)>;
    ServerUpdatesPanel(ServerApi& api,DesktopUpdates& client,bool owner,QWidget* viewport,QString knownOffer,ServerOffer updateOffer,MarkRead markRead)
        :api(api),client(client),owner(owner),viewport(viewport),serverTag(std::move(knownOffer)),updateOffer(std::move(updateOffer)),markRead(std::move(markRead)){
        setObjectName("serverUpdates");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
        auto* heading=new QHBoxLayout;heading->setSpacing(6);
        heading->addWidget(Ui::text(UiText::Updates(),"font-size:14px;font-weight:500"));
        dot=new QLabel;dot->setObjectName("availableUpdateDot");dot->setFixedSize(6,6);
        dot->setAccessibleName(UiText::AnUpdateIsAvailable());dot->setToolTip(UiText::AnUpdateIsAvailable());
        dot->setStyleSheet("background:#64aaf0;border-radius:3px");dot->installEventFilter(this);
        connect(qApp,&QApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
            if(state==Qt::ApplicationActive)dot->update();
        });
        heading->addWidget(dot);heading->addStretch();layout->addLayout(heading);
        for(auto** field:{&installed,&detail,&desktop}){*field=Ui::text("","color:#aaa;font-size:12px");(*field)->setTextFormat(Qt::PlainText);(*field)->setWordWrap(true);layout->addWidget(*field);}
        installed->setVisible(owner);installed->setText(UiText::ServerVersionChecking());detail->hide();
        check=new QPushButton(UiText::CheckForUpdates());install=new QPushButton(UiText::Update());rollback=new QPushButton(UiText::RollBack());
        auto* buttons=new WrapLayout;buttons->setSpacing(10);
        for(auto* b:{check,install,rollback}){Ui::compactButton(b);buttons->addWidget(b);}layout->addLayout(buttons);
        timer.setSingleShot(true);
        connect(&timer,&QTimer::timeout,this,[this]{request("GET","/api/server-update",{});});
        connect(check,&QPushButton::clicked,this,[this]{clientCheck();});
        connect(install,&QPushButton::clicked,this,[this]{
            if(serverReady())request("POST","/api/server-update",{{"tag",state["available"]}},this->client.available());
            else this->client.install();
        });
        connect(rollback,&QPushButton::clicked,this,[this]{request("POST","/api/server-update/rollback",{{"tag",state["previous"]}});});
        client.observe(this,[this]{render();});render();
        if(owner)request("GET","/api/server-update",{});
    }
    void languageChanged(){error=UiText::knownMessage(error);render();}
private:
    ServerApi& api;DesktopUpdates& client;bool owner,posting=false;int generation=0;
    QWidget* viewport;
    QLabel* dot;
    QString serverTag,shownClient,shownServer;
    ServerOffer updateOffer;
    MarkRead markRead;
    bool eventFilter(QObject* object,QEvent* event)override{
        if(object==dot && event->type()==QEvent::Paint && !window()->isMinimized() &&
           window()->isActiveWindow() && viewport->rect().contains(dot->mapTo(viewport,dot->rect().center())) &&
           dot->visibleRegion().contains(dot->rect().center())){
            const auto tag=client.offeredVersion();
            if(tag!=shownClient || serverTag!=shownServer){
                shownClient=tag;shownServer=serverTag;
                markRead(tag,serverTag);
            }
        }
        return QWidget::eventFilter(object,event);
    }
    QJsonObject state;QTimer timer;QString error;
    QLabel *installed,*detail,*desktop;QPushButton *check,*install,*rollback;
    bool managed()const{return owner&&!state.isEmpty()&&state["phase"]!="unmanaged";}
    bool busy()const{return QStringList{"checking","queued","waiting","installing"}.contains(state["phase"].toString());}
    bool serverReady()const{return managed() && !busy() && state["canInstall"].toBool() && !state["available"].toString().isEmpty();}
    void clientCheck(){if(managed()&&!busy())request("POST","/api/server-update/check",{});else if(owner)request("GET","/api/server-update",{});client.check();}
    void render(){
        dot->setVisible(client.available() || !serverTag.isEmpty());dot->update();
        const auto phase=state["phase"].toString();
        const QHash<QString,QString> labels{
                {"unmanaged",UiText::UpdatesFromThisPanelHaveNotBeen()},{"checking",UiText::CheckingGithubReleases()},
                {"empty",UiText::NoUpdatesAvailable()},{"current",UiText::YouHaveTheLatestVersion()},{"available",UiText::AnUpdateIsAvailable()},
                {"queued",UiText::UpdateQueued()},{"waiting",UiText::WaitingForInstallation()},{"installing",UiText::InstallingUpdate()},
                {"restored",UiText::PreviousVersionRestored()},{"error",UiText::CouldNotCompleteTheOperation()}};

        if(!state.isEmpty()){
            auto line=UiText::ServerVersion()+state["installed"].toString();
            const auto ts=state["checkedAt"].toInteger();if(ts)line+=UiText::Checked()+QDateTime::fromSecsSinceEpoch(ts).toLocalTime().toString("dd.MM.yyyy, HH:mm");
            installed->setText(line);
        }
        auto msg=error.isEmpty()?UiText::serverMessage(state["detail"].toString()):error;
        if(msg.isEmpty())msg=labels.value(phase);
        if(serverReady())msg+=" "+state["available"].toString();
        detail->setText(msg);detail->setVisible(owner&&!msg.isEmpty());desktop->setText(client.status());
        install->setVisible(serverReady()||client.available());install->setEnabled(!posting && !client.active() && (!owner || !state.isEmpty()));
        rollback->setText(UiText::RollBackTo()+state["previous"].toString());rollback->setVisible(managed()&&!state["previous"].toString().isEmpty());rollback->setEnabled(!posting&&!busy());
        check->setEnabled(!posting&&!client.active()&&!busy());
    }
    void request(QByteArray method,QString path,QJsonObject body,bool alsoClient=false){
        if(!owner||posting)return;
        timer.stop();posting=method=="POST";const int sequence=++generation;render();
        // The accepted click survives leaving Settings. ServerApi ignores replies from a previous server/session.
        api.request(method,path,body,[guard=QPointer<ServerUpdatesPanel>(this),updates=QPointer<DesktopUpdates>(&client),sequence,alsoClient](QJsonObject data,QString issue){
            if(guard&&guard->generation==sequence){
                guard->posting=false;guard->error=issue;
                if(issue.isEmpty()){guard->state=data;guard->serverTag=guard->updateOffer(data);}
                else if(guard->state.isEmpty()&&issue.contains("404"))guard->state={{"phase","unmanaged"}};
                guard->render();guard->timer.start(guard->state["phase"]=="checking"?350:5000);
            }
            if(alsoClient && updates && issue.isEmpty() && data["phase"]!="error")updates->install();
        });
    }
};
