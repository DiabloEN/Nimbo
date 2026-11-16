#pragma once
#include "interface_text.h"
#include "api.h"
#include "ui.h"
#include <memory>

class WorkspaceIdentity final : public QWidget {
public:
    WorkspaceIdentity(ServerApi& api,const QJsonObject& workspace,std::function<QString()> choose,
                      std::function<void()> refresh):state(std::make_shared<State>()){
        state->api=&api;state->origin=api.server;state->refresh=std::move(refresh);
        for(const auto key:{"name","description","avatar"})state->confirmed[key]=workspace[key].toString();
        state->desired=state->confirmed;
        auto* form=new QHBoxLayout(this);form->setContentsMargins(0,0,0,0);form->setSpacing(12);
        auto* avatarBox=new QWidget;avatarBox->setFixedSize(60,60);form->addWidget(avatarBox);
        auto* avatar=new QPushButton(avatarBox);avatar->setFixedSize(60,60);avatar->setIconSize({60,60});
        avatar->setAccessibleName(UiText::UploadAvatar());avatar->setStyleSheet("border:0;padding:0;background:transparent");
        auto* erase=Ui::action("close",UiText::RemoveAvatar(),22);erase->setParent(avatarBox);erase->move(38,0);
        erase->setStyleSheet("background:#333;border:0;border-radius:10px;padding:0");
        auto field=[form](QString title,QString value,int maxLength){
            auto* e=new QLineEdit(value);e->setAccessibleName(title);e->setPlaceholderText(title);e->setMaxLength(maxLength);
            e->setMinimumWidth(40);e->setFixedHeight(42);
            e->setStyleSheet("QLineEdit{background:#202020;border:0;border-radius:10px;padding:0 14px;font-size:12px} QLineEdit:focus{background:#292d2c}");
            form->addWidget(e,1);return e;
        };
        auto* name=field(UiText::ServerName(),workspace["name"].toString(),60);
        auto* description=field(UiText::Description(),workspace["description"].toString(),200);
        photo=workspace["avatar"].toString();
        auto draw=[this,avatar,erase,name]{
            avatar->setIcon(QIcon(Ui::avatar(name->text(),Ui::imageData(photo),60,true)));
            erase->setVisible(!photo.isEmpty());
        };
        draw();
        connect(name,&QLineEdit::textChanged,avatar,draw);
        auto save=[this,name,description]{
            auto title=name->text().trimmed();
            if(title.isEmpty()){state->feedback(UiText::EnterAServerName());return;}
            state->desired={{"name",title},{"description",description->text()},{"avatar",photo}};
            flush(state);
        };
        connect(name,&QLineEdit::editingFinished,this,save);
        connect(description,&QLineEdit::editingFinished,this,save);
        connect(erase,&QPushButton::clicked,this,[this,draw,save]{photo.clear();draw();save();});
        connect(avatar,&QPushButton::clicked,this,[this,choose,draw,save]{auto value=choose();if(!value.isEmpty()){photo=value;draw();save();}});
    }
    void setFeedback(std::function<void(QString)> callback){state->feedback=std::move(callback);}
private:
    struct State{
        ServerApi* api;
        QUrl origin;
        QJsonObject desired,confirmed;
        std::function<void()> refresh;
        std::function<void(QString)> feedback=[](QString){};
        bool sending=false;
    };
    static void flush(const std::shared_ptr<State>& state){
        if(state->sending || state->confirmed==state->desired || state->api->server!=state->origin)return;
        state->sending=true;
        const auto sent=state->desired;
        state->api->request("PATCH","/api/workspace",sent,[state,sent](auto,QString issue){
            if(state->api->server!=state->origin)return;
            state->sending=false;
            if(!issue.isEmpty()){state->feedback(issue);return;}
            state->confirmed=sent;
            state->feedback({});state->refresh();flush(state);
        });
    }
    QString photo;
    std::shared_ptr<State> state;
};
