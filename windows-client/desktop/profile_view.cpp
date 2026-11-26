#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include "profile.h"
#include "server_links.h"
#include "avatar_editor.h"
#include "password_reveal.h"
#include <algorithm>

namespace {
using namespace Ui;
QWidget* panel(const QString& color, int radius) {
    auto* w = new QWidget;
    w->setAttribute(Qt::WA_StyledBackground);
    w->setObjectName("profileSurface");
    w->setStyleSheet(QString("QWidget#profileSurface{background:%1;border-radius:%2px}").arg(color).arg(radius));
    return w;
}
QVBoxLayout* stack(QWidget* w,int padding=0,int gap=0){
    auto* box=new QVBoxLayout(w);
    box->setContentsMargins(padding,padding,padding,padding);box->setSpacing(gap);return box;
}
QPushButton* control(QString caption,QString theme="soft",int height=38){
    auto* b=new QPushButton(caption);b->setProperty("theme",theme);
    b->setFixedHeight(height);b->setCursor(Qt::PointingHandCursor);return b;
}
QLabel* caption(const QString& s,int pixels=12,QString color="#aaa",bool bold=false){
    return text(s,QString("font-size:%1px;color:%2;font-weight:%3").arg(pixels).arg(color).arg(bold?500:400));
}
QLineEdit* field(QVBoxLayout* box,QString title,QString value={},bool secret=false){
    auto* label=caption(title);box->addWidget(label);box->addSpacing(5);
    auto* input=new QLineEdit(value);input->setFixedHeight(42);input->setAccessibleName(title);label->setBuddy(input);
    if(secret){
        input->setEchoMode(QLineEdit::Password);input->setMaxLength(256);
        new PasswordReveal(input);
    }
    box->addWidget(input);return input;
}
QLabel* paragraph(const QString& value,int px,QString color,int spacing){
    auto* p=caption("",px,color);p->setWordWrap(true);p->setTextFormat(Qt::RichText);
    p->setText(QString("<p style='margin:0;line-height:%1%'>%2</p>").arg(qRound(spacing*0.80)).arg(value.toHtmlEscaped()));return p;
}
QHBoxLayout* row(QVBoxLayout* parent,int spacing=10){
    auto* r=new QHBoxLayout;r->setSpacing(spacing);parent->addLayout(r);return r;
}
void pair(QVBoxLayout* box,QPushButton* a,QPushButton* b){auto* r=row(box,6);r->addWidget(a);r->addWidget(b);r->addStretch();}
}
using namespace Ui;

void DesktopWindow::openLocalHome(){
    if(!localHome){
        localHome=new QWidget;pages->addWidget(localHome);
        auto* host=new QHBoxLayout(localHome);host->setContentsMargins(0,0,0,0);host->addStretch();
        auto* center=new QWidget;center->setMaximumWidth(960);host->addWidget(center,1000);host->addStretch();
        auto* column=new QVBoxLayout(center);column->setContentsMargins(32,28,32,20);column->setSpacing(0);
        auto* bar=row(column);bar->addWidget(caption("nimbo.",36,"#ededed",true));bar->addStretch();
        column->addSpacing(36);auto* profileHeading=caption(UiText::Profile(),23,"#ededed",true);profileHeading->setObjectName("localProfileHeading");column->addWidget(profileHeading);column->addSpacing(56);
        localLayout=new QVBoxLayout;column->addLayout(localLayout,1);
        localStatus=caption("");column->addWidget(localStatus);localStatus->hide();
    }
    localHome->findChild<QLabel*>("localProfileHeading")->setText(UiText::Profile());
    pages->setCurrentWidget(localHome);Ui::clear(localLayout);localStatus->hide();
    draftProfileName=profile["name"].toString();draftProfileAvatar=profile["avatar"].toString();renderProfile();
    QTimer::singleShot(0,this,&DesktopWindow::warmSavedServers);
}
void DesktopWindow::renderProfile(){
    const bool local=localHome && pages->currentWidget()==localHome;
    title->setText(UiText::Profile());
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    (local?localLayout:detailLayout)->addWidget(scroll);
    auto* page=new QWidget;page->setObjectName("profileContents");scroll->setWidget(page);
    page->setStyleSheet(R"(
      QPushButton[flatIcon=true]{padding:0;border:0;background:transparent}
      QPushButton{background:#252525;border:1px solid transparent;border-radius:10px;color:#ddd;padding:0 13px;font-size:12px}
      QPushButton:hover{background:#303030} QPushButton:focus{border:1px solid #aaa} QLineEdit:focus{background:#292d2c;border:1px solid transparent}
      QPushButton[theme="primary"]{background:#e4e4e4;color:#111;font-weight:500}
      QPushButton[theme="primary"]:hover{background:white}
      QPushButton[theme="quiet"],QPushButton[theme="link"],QPushButton[theme="danger"]{background:transparent;padding:0;border:0}
      QPushButton[theme="danger"]{color:#ef9292} QPushButton[theme="link"]{text-align:left}
      QPushButton[theme="small"]{background:#303030;font-size:11px;border-radius:8px;padding:0 6px}
      QPushButton[theme="add"]{background:transparent;border:1px dashed #555;font-size:11px}
      QPushButton[theme="quiet"]{padding:0 10px;border-radius:8px}
      QPushButton[theme="add"]:hover,QPushButton[theme="quiet"]:hover{background:#222}
      QPushButton[theme="avatar"]{padding:0;border:0;background:transparent}
      QLineEdit{background:#202020;border:1px solid transparent;border-radius:10px;color:#eee;font-size:12px;padding:0 14px}
    )");
    auto* outer=new QVBoxLayout(page);if(local)outer->setContentsMargins(0,0,0,0);else outer->setContentsMargins(32,26,32,24);outer->setSpacing(0);
    auto* cols=row(outer,20);
    auto* serversWidget=new QWidget;serversWidget->setObjectName("profileServers");serversWidget->setFixedWidth(width()<=1150?170:190);
    cols->addWidget(serversWidget,0,Qt::AlignTop);auto* serversBox=stack(serversWidget);
    auto* details=new QWidget;details->setObjectName("profileDetails");details->setMaximumWidth(width()<=1150?QWIDGETSIZE_MAX:680);details->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    cols->addWidget(details,1,Qt::AlignTop);cols->setAlignment(Qt::AlignLeft);outer->addStretch();
    auto servers=profile["servers"].toArray();
    auto* serverHeading=row(serversBox);serverHeading->setContentsMargins(12,0,12,0);
    serverHeading->addWidget(caption(UiText::MyServers2()));serverHeading->addStretch();serverHeading->addWidget(caption(QString::number(servers.size()),12,"#777"));serversBox->addSpacing(16);
    QList<QJsonObject> sorted;for(auto v:servers)sorted.append(v.toObject());
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a["pinned"].toBool()>b["pinned"].toBool();});
    auto updateEntry=[this](QString url,QString key,QJsonValue v){
        auto entries=profile["servers"].toArray();for(int i=0;i<entries.size();++i){auto entry=entries[i].toObject();if(entry["url"]!=url)continue;entry[key]=v;if(key=="name")entry["customName"]=true;entries[i]=entry;}
        profile["servers"]=entries;saveLocal();renderView();
    };
    for(const auto& s:sorted){
        QString url=s["url"].toString();bool active=!local && DesktopProfile::origin(url)==api.server;
        QString name=active&&!s["customName"].toBool()?workspace["name"].toString():s["name"].toString();
        QString role=active?self["role"].toString(s["role"].toString()):s["role"].toString();
        auto* tile=panel(active?"#232323":"#131313",12);serversBox->addWidget(tile);auto* box=stack(tile,12);
        auto* top=row(box,4);
        auto* link=control({},"link",42);
        link->setObjectName("openSavedServer");link->setAccessibleName(UiText::OpenServer()+QUrl(url).authority());
        link->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        auto* header=stack(link);
        auto* titleRow=row(header,6);
        auto* serverName=caption(name,12,"#eee",true);
        serverName->setMinimumWidth(0);serverName->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(serverName);
        if(role=="owner"){
            auto* crown=new QLabel(link);crown->setFixedSize(14,14);
            crown->setAttribute(Qt::WA_TransparentForMouseEvents);
            crown->setAccessibleName(UiText::Owner());
            crown->setToolTip(active?UiText::YouOwnThisServer():UiText::OwnerAtLastSignIn());
            crown->setPixmap(tintedIcon("crown",QColor("#d7bc7d")).pixmap(QSize(14,14),devicePixelRatioF()));
            titleRow->addWidget(crown,0,Qt::AlignVCenter);
        }
        titleRow->addStretch();
        auto* addressLabel=caption(QUrl(url).authority(),10,"#888");
        addressLabel->setMinimumWidth(0);addressLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        header->addWidget(addressLabel);top->addWidget(link,1);
        auto* pin=action("pin",(s["pinned"].toBool()?UiText::Unpin():UiText::Pin())+name,20);
        top->addWidget(pin,0,Qt::AlignTop);
        connect(pin,&QPushButton::clicked,pin,[=]{updateEntry(url,"pinned",!s["pinned"].toBool());});
        connect(link,&QPushButton::clicked,link,[this,s,active]{if(active)openView("room");else switchServer(s);});
        box->addSpacing(12);
        auto* edit=control(UiText::Edit(),"small",30);auto* leave=control(UiText::Leave(),"small",30);auto* operations=row(box,6);operations->addWidget(edit);operations->addWidget(leave);
        auto* editor=panel("#1b1b1b",10);auto* form=stack(editor,10,4);auto* custom=field(form,UiText::NameInYourList(),name);custom->setMaxLength(60);
        field(form,UiText::Address(),url)->setReadOnly(true);auto* commit=control(UiText::Save(),"small",30);auto* cancel=control(UiText::Cancel(),"quiet",30);
        pair(form,commit,cancel);box->addWidget(editor);editor->hide();
        connect(edit,&QPushButton::clicked,editor,[=]{editor->show();custom->setFocus();});connect(cancel,&QPushButton::clicked,editor,&QWidget::hide);
        connect(commit,&QPushButton::clicked,editor,[=]{if(!custom->text().trimmed().isEmpty())updateEntry(url,"name",custom->text().trimmed());});
        auto* reason=panel("#242424",8);auto* removal=stack(reason,2,6);
        removal->addWidget(paragraph(role=="owner"?UiText::TransferOwnershipToAnotherMemberFirst():active?UiText::LeaveThisServerYourProfileAndAvatar():UiText::OpenThisServerToLeaveItAnd(),11,"#aaa",140));
        if(role!="owner"){
            auto* proceed=control(active?UiText::LeaveServer():UiText::OpenServer2(),"small",30);removal->addWidget(proceed);
            connect(proceed,&QPushButton::clicked,reason,[this,s,active,url]{if(!active){switchServer(s);return;}
                api.request("DELETE","/api/membership",{},[this,url](auto,QString error){if(!error.isEmpty()){note(error);return;}auto list=profile["servers"].toArray();for(int i=list.size()-1;i>=0;--i)if(list[i].toObject()["url"]==url)list.removeAt(i);profile["servers"]=list;saveLocal();showLogin();});});
        }
        auto* stay=control(UiText::Cancel(),"quiet",26);removal->addWidget(stay);box->addWidget(reason);reason->hide();connect(stay,&QPushButton::clicked,reason,&QWidget::hide);
        connect(leave,&QPushButton::clicked,reason,[=]{editor->hide();reason->show();});serversBox->addSpacing(12);
    }
    auto* add=control(UiText::AddServer(),"add",44);add->setObjectName("startAddServer");add->setIcon(icon("plus"));serversBox->addWidget(add);
    auto* newServer=panel("#1b1b1b",12);serversBox->addWidget(newServer);auto* nf=stack(newServer,12,6);
    nf->addWidget(caption(UiText::NewServer(),12,"#eee",true));auto* addressField=field(nf,UiText::AddressOrInvitationLink());addressField->setPlaceholderText("https://nimbo.example.com");
    auto* codeGroup=new QWidget;nf->addWidget(codeGroup);auto* code=field(stack(codeGroup,0,6),UiText::InvitationCode());
    addressField->setObjectName("addServerAddress");code->setObjectName("addServerCode");
    auto* enroll=control(UiText::Add(),"small",32);enroll->setObjectName("submitServerLink");
    auto destination=[addressField,servers]{
        const auto url=DesktopProfile::origin(addressField->text());
        if(!url.isEmpty())for(const auto value:servers){
            const auto entry=value.toObject();if(DesktopProfile::origin(entry["url"].toString())==url)return entry;
        }
        return QJsonObject{};
    };
    auto updateDestination=[=,this]{
        const auto entry=destination();const bool known=!entry.isEmpty();
        enroll->setText(known?UiText::OpenServer2():UiText::Add());
        codeGroup->setVisible(!known || (deniedProfile==localId && deniedOrigin==DesktopProfile::origin(entry["url"].toString())));
    };
    auto pasteLink=[addressField,code,updateDestination](const ServerLinks::Link& link){
        const QSignalBlocker blockAddress(addressField),blockCode(code);
        addressField->setText(link.origin.toString());code->setText(link.code);updateDestination();
    };
    const auto applyAddress=ServerLinks::bind(addressField,pasteLink,false);
    const auto applyCode=ServerLinks::bind(code,pasteLink);
    connect(addressField,&QLineEdit::textChanged,newServer,updateDestination);
    auto* cancel=control(UiText::Cancel(),"quiet",32);pair(nf,enroll,cancel);newServer->hide();
    connect(add,&QPushButton::clicked,newServer,[=]{newServer->show();add->hide();addressField->setFocus();});
    connect(cancel,&QPushButton::clicked,newServer,[=]{newServer->hide();code->clear();addressField->clear();add->show();});
    auto enrollmentKey=std::make_shared<QPair<QUrl,QString>>();
    connect(enroll,&QPushButton::clicked,newServer,[=,this]{
        applyAddress();applyCode();
        auto url=DesktopProfile::origin(addressField->text());
        const auto entry=destination();
        if(!entry.isEmpty()){
            if(deniedOrigin==url && deniedProfile==localId && !code->text().trimmed().isEmpty()){
                switchServer(entry,true,code->text().trimmed());return;
            }
            if(!local && api.server==url)openView("room");else switchServer(entry);
            return;
        }
        if(url.isEmpty()||code->text().trimmed().isEmpty()||code->text().size()>256){note(UiText::EnterTheAddressAndInvitationCode());return;}
        if(servers.size()>=64){note(UiText::YouCanSaveUpTo64Servers());return;}
        if(enrollmentKey->first!=url)*enrollmentKey={url,QString::fromLatin1(DesktopProfile::randomKey())};
        auto key=enrollmentKey->second;auto* probe=new ServerApi(newServer);probe->server=url;newServer->setEnabled(false);
        probe->request("POST","/api/enroll",{{"name",self.isEmpty()?profile["name"]:self["name"]},{"password",code->text().trimmed()},{"profileKey",key}},[=,this](auto reply,QString error){
            newServer->setEnabled(true);probe->deleteLater();if(!error.isEmpty()){note(error);return;}
            auto member=reply["member"].toObject();if(reply["profileKey"]!=key||reply["serverName"].toString().isEmpty()||member["name"].toString().isEmpty()){note(UiText::TheServerReturnedAnInvalidResponse());return;}
            auto saved=profile["servers"].toArray();saved.append(QJsonObject{{"name",reply["serverName"]},{"url",url.toString()},{"profileKey",key},{"role",member["role"]},{"displayName",member["name"]}});profile["servers"]=saved;saveLocal();serverSwitch->show();renderView();
        });
    });
    auto* main=stack(details);auto* identity=row(main,16);auto* avatarWrap=new QWidget;avatarWrap->setFixedSize(80,80);identity->addWidget(avatarWrap);
    auto* avatarButton=new AvatarUpload;avatarButton->setParent(avatarWrap);
    auto* erase=action("close",UiText::DeleteAvatar(),22);erase->setParent(avatarWrap);erase->move(58,0);erase->setStyleSheet("background:#333;border-radius:11px;padding:0");erase->setIconSize({14,14});
    auto* nameBox=new QVBoxLayout;nameBox->setSpacing(0);identity->addLayout(nameBox,1);
    auto* name=field(nameBox,local?UiText::ProfileName():UiText::NameOnThisServer(),draftProfileName);nameBox->addStretch();name->setMaxLength(40);name->setObjectName("serverDisplayName");
    auto draft=std::make_shared<QString>(draftProfileAvatar);
    auto refreshAvatar=[=,this]{draftProfileName=name->text();draftProfileAvatar=*draft;avatarButton->updateImage(name->text(),*draft);erase->setVisible(!draft->isEmpty());};refreshAvatar();connect(name,&QLineEdit::textChanged,avatarButton,refreshAvatar);
    connect(avatarButton,&QPushButton::clicked,avatarButton,[=,this]{auto value=selectAvatar(this);if(!value.isEmpty()){*draft=value;refreshAvatar();}});
    connect(erase,&QPushButton::clicked,erase,[=]{draft->clear();refreshAvatar();});
    auto* save=control(UiText::SaveProfile(),"primary",42);save->setFixedWidth(145);identity->addWidget(save);
    auto* feedback=caption("");feedback->setMinimumHeight(34);main->addWidget(feedback);feedback->hide();
    connect(save,&QPushButton::clicked,save,[=,this]{
        auto value=name->text().trimmed();if(value.isEmpty()){name->setFocus();return;}if(local){
            profile["name"]=value;profile["avatar"]=*draft;saveLocal();nickname->setText(value);feedback->setText(UiText::ProfileSaved());feedback->show();return;
        }
        save->setEnabled(false);
        auto avatarValue=*draft;auto guard=QPointer<QPushButton>(save);
        api.request("PATCH","/api/profile",{{"name",value},{"avatar",avatarValue}},[=,this](auto,QString error){
            if(guard){save->setEnabled(true);feedback->setText(error.isEmpty()?UiText::ProfileSaved():error);feedback->show();}
            if(!error.isEmpty())return;auto entries=profile["servers"].toArray();for(int i=0;i<entries.size();++i){auto e=entries[i].toObject();if(DesktopProfile::origin(e["url"].toString())==api.server){e["displayName"]=value;entries[i]=e;}}
            profile["servers"]=entries;profile["name"]=value;profile["avatar"]=avatarValue;avatars.remove(selfId);avatarVersions.remove(selfId);saveLocal();refresh();
        });
    });connect(name,&QLineEdit::returnPressed,save,&QPushButton::click);
    main->addSpacing(32);auto* library=panel("#1b1b1b",12);main->addWidget(library);auto* lib=stack(library,22);
    lib->addWidget(caption(UiText::Profiles(),16,"#eee",true));lib->addSpacing(14);
    lib->addWidget(paragraph(UiText::ImportANimboProfileOrDownloadYour(),12,"#aaa",160));lib->addSpacing(12);
    lib->addWidget(paragraph(UiText::YourProfileIsStoredLocallyYouCan(),11,"#929292",160));lib->addSpacing(20);
    auto* actions=row(lib);auto* download=control(UiText::DownloadProfile());auto* upload=control(UiText::ImportProfile());actions->addWidget(download);actions->addWidget(upload);actions->addStretch();
    auto* exportBox=panel("#1b1b1b",0);lib->addWidget(exportBox);auto* ef=stack(exportBox);ef->addSpacing(36);
    auto* fields=row(ef,12);auto* a=new QVBoxLayout;auto* b=new QVBoxLayout;fields->addLayout(a);fields->addLayout(b);
    auto* pass=field(a,UiText::FilePassword(),{},true);auto* repeat=field(b,UiText::RepeatPassword(),{},true);
    ef->addSpacing(30);ef->addWidget(caption(UiText::AtLeast6CharactersThePasswordStays()));ef->addSpacing(12);
    auto* submit=control(UiText::DownloadNimbo(),"primary");auto* close=control(UiText::Cancel(),"quiet");pair(ef,submit,close);exportBox->hide();
    connect(download,&QPushButton::clicked,exportBox,[=]{exportBox->show();});
    connect(close,&QPushButton::clicked,exportBox,[=]{pass->clear();repeat->clear();exportBox->hide();download->setFocus();});
    connect(submit,&QPushButton::clicked,exportBox,[=,this]{if(pass->text().size()<6){pass->setFocus();return;}if(pass->text()!=repeat->text()){note(UiText::PasswordsDoNotMatch());return;}if(exportProfile(pass->text()))close->click();});
    connect(repeat,&QLineEdit::returnPressed,submit,&QPushButton::click);
    auto* importBox=panel("#1b1b1b",0);lib->addWidget(importBox);auto* ib=stack(importBox);ib->addSpacing(16);
    auto* secret=field(ib,UiText::PasswordForTheImportedFile(),{},true);ib->addSpacing(12);auto* unlock=control(UiText::Upload(),"primary");auto* reject=control(UiText::Cancel(),"quiet");pair(ib,unlock,reject);importBox->hide();
    auto encrypted=std::make_shared<QByteArray>();
    auto dismiss=[=]{encrypted->fill(0);encrypted->clear();secret->clear();importBox->hide();};
    connect(reject,&QPushButton::clicked,importBox,dismiss);connect(download,&QPushButton::clicked,importBox,dismiss);
    connect(upload,&QPushButton::clicked,upload,[=,this]{
        QString path=QFileDialog::getOpenFileName(this,UiText::ImportProfile(),{},"Nimbo (*.nimbo *.json)");if(path.isEmpty())return;
        dismiss();exportBox->hide();pass->clear();repeat->clear();
        QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>300000){note(UiText::TheFileIsTooLargeOrUnavailable());return;}
        auto bytes=f.readAll();
        if(QJsonDocument::fromJson(bytes).object()["format"]=="nimbo-encrypted-profile"){
            try{DesktopProfile::inspectEncrypted(bytes);}catch(...){note(UiText::InvalidEncryptedFile());return;}
            *encrypted=bytes;importBox->show();return;
        }
        try{addToLibrary(DesktopProfile::open(bytes,{}));}catch(...){note(UiText::CouldNotOpenTheProfileCheckThe());}
    });
    connect(unlock,&QPushButton::clicked,unlock,[=,this]{
        try{auto incoming=DesktopProfile::open(*encrypted,secret->text());dismiss();addToLibrary(incoming);}
        catch(...){note(UiText::CouldNotOpenTheProfileCheckThe2());secret->setFocus();}
    });connect(secret,&QLineEdit::returnPressed,unlock,&QPushButton::click);
    lib->addSpacing(24);lib->addWidget(caption(UiText::OnThisDevice()+QString::number(localProfiles.size()),11,"#999"));lib->addSpacing(10);
    for(auto v:localProfiles){
        auto entry=v.toObject();auto p=entry["profile"].toObject();QString id=entry["id"].toString();bool current=id==localId;
        auto* local=panel("#151515",12);lib->addWidget(local);auto* list=stack(local,12);
        auto* lr=row(list);auto* face=text("");face->setPixmap(avatar(p["name"].toString(),imageData(p["avatar"].toString()),34));lr->addWidget(face);
        auto* info=new QVBoxLayout;info->setSpacing(3);info->addWidget(caption(p["name"].toString(),12,"#eee",true));info->addWidget(caption(current?UiText::CurrentProfile():UiText::Servers()+QString::number(p["servers"].toArray().size()),10,"#888"));lr->addLayout(info,1);
        if(!current){
            auto* activate=control(UiText::SignIn(),"small",32);lr->addWidget(activate);
            auto* prompt=panel("#151515",0);prompt->setObjectName("switchProfileForm");
            auto* form=stack(prompt,0,8);
            auto* invite=field(form,UiText::InvitationCodeForFirstSignIn());
            invite->setMaxLength(256);invite->setEchoMode(QLineEdit::Password);
            form->addWidget(paragraph(UiText::SwitchingEndsTheCurrentCallIfThis(),12,"#aaa",160));
            auto* enter=control(UiText::Switch(),"primary");auto* cancel=control(UiText::Cancel(),"quiet");pair(form,enter,cancel);
            list->addWidget(prompt);prompt->hide();
            auto attempt=[this,id,prompt](const QString& code){
                for(auto* other:pages->findChildren<QWidget*>("switchProfileForm"))other->hide();
                QPointer<QWidget> guard(prompt);
                chooseProfile(id,code,[guard]{if(guard)guard->show();});
            };
            connect(activate,&QPushButton::clicked,activate,[attempt,invite]{invite->clear();attempt({});});
            connect(enter,&QPushButton::clicked,enter,[attempt,invite]{
                if(invite->text().isEmpty()){invite->setFocus();return;}attempt(invite->text());
            });
            connect(invite,&QLineEdit::returnPressed,enter,&QPushButton::click);
            connect(cancel,&QPushButton::clicked,prompt,[this,invite,prompt]{invite->clear();prompt->hide();note({});});
        }
        auto* remove=action("close",UiText::RemoveProfile()+p["name"].toString(),28);lr->addWidget(remove);
        auto* confirm=panel("#151515",0);auto* cf=stack(confirm);cf->addWidget(paragraph(UiText::RemoveThisProfileFromThisDeviceDownloaded()+QString(current?UiText::YouWillBeSignedOutOfThis():""),12,"#aaa",150));
        auto* yes=control(UiText::RemoveProfile2(),"danger");auto* no=control(UiText::Cancel(),"quiet");pair(cf,yes,no);list->addWidget(confirm);confirm->hide();
        connect(remove,&QPushButton::clicked,confirm,&QWidget::show);connect(no,&QPushButton::clicked,confirm,&QWidget::hide);
        connect(yes,&QPushButton::clicked,yes,[this,id]{removeLocalProfile(id);});
    }
    main->addSpacing(12);auto* logoutButton=control(UiText::SignOutOfProfile(),"danger",34);logoutButton->setObjectName("logout");main->addWidget(logoutButton,0,Qt::AlignRight);connect(logoutButton,&QPushButton::clicked,this,[this,local]{
        if(!local){logout();return;}
        QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("resumeProfile",QString());showLogin();
    });
    fade(page);
}


