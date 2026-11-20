#pragma once
#include "interface_text.h"
#include "api.h"
#include "ui.h"

class AntivirusSettings final : public QWidget {
public:
    explicit AntivirusSettings(ServerApi& server): api(server) {
        setObjectName("antivirusSettings");setAttribute(Qt::WA_StyledBackground);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
        auto* row=new QHBoxLayout;row->setSpacing(12);auto* heading=Ui::text(UiText::ChatFileAntivirusClamav(),"font-size:14px;font-weight:500");heading->setWordWrap(true);
        row->addWidget(heading,1);policy=new Ui::ToggleSwitch(UiText::ChatFileAntivirusClamav());row->addWidget(policy);layout->addLayout(row);
        for(auto** field:{&policyHelp,&version,&checked}){*field=Ui::text("","color:#aaa;font-size:12px");(*field)->setWordWrap(true);layout->addWidget(*field);}
        auto* actions=new QGridLayout;actions->setContentsMargins(0,0,0,0);actions->setHorizontalSpacing(12);actions->setColumnStretch(1,1);
        updateButton=new QPushButton(UiText::CheckForDatabaseUpdates());Ui::compactButton(updateButton);
        actions->addWidget(updateButton,0,0,Qt::AlignLeft|Qt::AlignVCenter);
        // Keep space for the wrapped progress message even before the first click.
        auto* reserve=Ui::text(progressText(),"color:#aaa;font-size:12px");reserve->setWordWrap(true);
        auto sizing=reserve->sizePolicy();sizing.setRetainSizeWhenHidden(true);reserve->setSizePolicy(sizing);
        actions->addWidget(reserve,0,1);reserve->hide();
        feedback=Ui::text("","color:#aaa;font-size:12px");feedback->setTextFormat(Qt::PlainText);feedback->setWordWrap(true);
        feedback->setObjectName("antivirusFeedback");actions->addWidget(feedback,0,1);layout->addLayout(actions);
        connect(policy,&QCheckBox::clicked,this,[this](bool enabled){mutate(false,enabled);});
        connect(updateButton,&QPushButton::clicked,this,[this]{mutate(true,false);});
        connect(&timer,&QTimer::timeout,this,[this]{load();});timer.start(10000);
        render();load();
    }
    void languageChanged(){message=UiText::knownMessage(message);render();}
private:
    static QString progressText(){return QString::fromUtf8(UiText::CheckingForDatabaseUpdatesNewDatabasesInstall());}
    static void display(QLabel* label,const QString& message){label->setText(message);label->setVisible(!message.isEmpty());}
    void render(){
        const bool loaded=!data.isEmpty();
        const auto updater=data["updater"].toObject();
        auto engine=data["version"].toString().section('/',0,0);engine.remove(QRegularExpression("^ClamAV\\s+",QRegularExpression::CaseInsensitiveOption));
        display(version,loaded?UiText::ClamavVersion()+(engine.isEmpty()?QString::fromUtf8(UiText::NoData()):engine):QString());
        const auto last=updater["lastCheck"].toInteger(updater["lastSuccess"].toInteger());
        display(checked,loaded?UiText::LastDatabaseCheck()+(last?QDateTime::fromSecsSinceEpoch(last).toLocalTime().toString("dd.MM.yyyy, HH:mm:ss"):QString::fromUtf8(UiText::NoData())):QString());
        const bool enabled=data["enabled"].toBool();
        const bool available=data["available"].toBool(true);
        policy->setEnabled(loaded && !busy && (enabled || available));policy->setChecked(enabled);
        const auto max=data["scanLimitBytes"].toInteger();display(policyHelp,policy->isChecked()&&max>0?QString(UiText::ClamavScansFilesUpToAndIncluding()).arg(max/1048576):QString());
        updateButton->setVisible(loaded);
        updateButton->setEnabled(!busy && data["updateConfigured"].toBool() && !updater["busy"].toBool());
        QStringList notices;
        if(loaded && !available)notices.append(data["configured"].toBool(true)?UiText::ScannerUnavailable():UiText::ScannerNotConfigured());
        if(requestingUpdate || updater["busy"].toBool())notices.append(progressText());
        if(!message.isEmpty())notices.append(message);
        const auto error=UiText::serverMessage(updater["error"].toString());
        if(!error.isEmpty() && !notices.contains(error))notices.append(error);
        display(feedback,notices.join('\n'));
    }
    void load(){
        if(loading||busy)return;
        loading=true;
        api.request("GET","/api/antivirus",{},[this,guard=QPointer<AntivirusSettings>(this),revision=changes](QJsonObject result,QString err){
            if(!guard)return;
            loading=false;if(revision!=changes)return;
            if(err.isEmpty())data=result;else message=err;render();
        });
    }
    void mutate(bool update,bool enabled){
        if(busy||data.isEmpty())return;
        ++changes;busy=true;requestingUpdate=update;message.clear();
        const bool previous=data["enabled"].toBool();if(!update)data["enabled"]=enabled;render();
        api.request(update?"POST":"PATCH",update?"/api/antivirus/update":"/api/antivirus",update?QJsonObject{}:QJsonObject{{"enabled",enabled}},
            [this,guard=QPointer<AntivirusSettings>(this),update,previous](QJsonObject result,QString err){
                if(!guard)return;
                busy=false;requestingUpdate=false;
                if(!err.isEmpty()){data["enabled"]=previous;message=err;}
                else if(update)data["updater"]=result;
                else{data["enabled"]=result["enabled"];message=UiText::ChangesSaved();}
                render();if(update && err.isEmpty())load();
            });
    }
    ServerApi& api;QJsonObject data;QTimer timer;QString message;bool loading=false,busy=false;unsigned changes=0;
    bool requestingUpdate=false;
    QLabel *version,*checked,*policyHelp,*feedback;
    QCheckBox* policy;QPushButton* updateButton;
};
