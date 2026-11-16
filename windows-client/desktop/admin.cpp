#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include "role_editor.h"
#include "member_settings.h"
#include "channel_editor.h"

namespace {
QString idOf(QJsonObject o){return o.value("id").toString();}
QString pathId(QString id){return QString::fromLatin1(QUrl::toPercentEncoding(id));}
}
void DesktopWindow::renderAdmin(QVBoxLayout* body,const QString& section){
    if(section=="channels"){
        body->addWidget(new ChannelEditor(api,workspace,[this](const QJsonObject& data){renderWorkspace(data);}));
    }else if(section=="roles"){
        body->addWidget(new RoleEditor(api,workspace,self,[this](const QJsonObject& data){renderWorkspace(data);}));
    }else if(section=="members"){
        body->addWidget(new MemberSettings(*this));
    }else if(section=="files"){
        auto* panel=new QWidget;panel->setObjectName("fileSettings");body->addWidget(panel);
        panel->setStyleSheet("QWidget#fileSettings{background:#171717;border-radius:14px} QCheckBox{background:transparent;font-size:12px} QComboBox{background:#202020;border:0;border-radius:10px;padding:0 14px;font-size:12px}");
        panel->hide();auto* form=new QVBoxLayout(panel);form->setContentsMargins(18,18,18,18);form->setSpacing(14);
        form->addWidget(Ui::text(UiText::FilesInTextChannels(),"font-size:13px;font-weight:500"));
        auto* channels=Ui::select(UiText::TextChannel());
        for(auto room:rooms){auto ch=room.toObject();if(ch["kind"]=="text")channels->addItem(ch["name"].toString(),idOf(ch));}
        if(!channels->count()){panel->hide();delete channels;return;}
        form->addWidget(channels);
        auto* enabled=new Ui::CheckBox(UiText::AllowFilesUpTo20Mb(),13);form->addWidget(enabled);
        auto* scan=Ui::text("","font-size:12px;color:#aaa");scan->setWordWrap(true);scan->setTextFormat(Qt::RichText);
        scan->setTextInteractionFlags(Qt::TextBrowserInteraction);form->addWidget(scan);
        connect(scan,&QLabel::linkActivated,scan,[this]{openView("general");});
        auto* hint=Ui::text("","font-size:12px;color:#aaa");hint->setWordWrap(true);form->addWidget(hint);
        auto* submit=new QPushButton(UiText::Save());form->addWidget(submit,0,Qt::AlignLeft);
        auto* error=Ui::text("","font-size:12px;color:#efa5a5");error->setWordWrap(true);form->addWidget(error);error->hide();
        auto busy=[=](bool loading){channels->setEnabled(!loading);enabled->setEnabled(!loading);submit->setEnabled(!loading);};
        auto load=[=,this]{
            busy(true);error->hide();
            auto guard=QPointer<QWidget>(panel);
            api.request("GET","/api/channels/"+pathId(channels->currentData().toString())+"/files",{},[=](QJsonObject data,QString issue){
                if(!guard)return;
                if(data.contains("maxBatchBytes")){panel->hide();return;}
                panel->show();busy(false);
                if(!issue.isEmpty()){submit->setEnabled(false);enabled->setEnabled(false);error->setText(issue);error->show();return;}
                auto policy=data["policy"].toObject();bool scanning=policy["scan"].toBool();submit->setProperty("scan",scanning);enabled->setChecked(policy["enabled"].toBool());
                scan->setText(QString(UiText::Antivirus1AStyleColorAaaHref()).arg(scanning?UiText::ScanningEnabled():UiText::ScanningDisabled()));
                hint->setText(!scanning?UiText::FilesWillBeAvailableWithoutAntivirusScanning():data["scannerConfigured"].toBool()?UiText::FilesAppearOnlyAfterTheAntivirusResponds():UiText::ClamavIsNotConfiguredOnThisHost());
            });
        };
        connect(channels,&QComboBox::currentIndexChanged,channels,load);load();
        connect(submit,&QPushButton::clicked,submit,[=,this]{
            auto guard=QPointer<QWidget>(panel);
            if(submit->hasFocus())submit->clearFocus();
            busy(true);error->hide();submit->setText(UiText::Saving());
            api.request("PATCH","/api/channels/"+pathId(channels->currentData().toString())+"/files",
                        {{"enabled",enabled->isChecked()},{"scan",submit->property("scan").toBool()}},[=](auto,QString issue){
                if(!guard)return;busy(false);submit->setText(UiText::Save());error->setText(issue);error->setVisible(!issue.isEmpty());
            });
        });
    }
}
