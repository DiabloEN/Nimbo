#pragma once
#include "interface_text.h"
#include <optional>
#include "api.h"
#include "ui.h"

class RoleEditor final : public QWidget {
public:
    RoleEditor(ServerApi& api,const QJsonObject& workspace,const QJsonObject& actor,
               std::function<void(const QJsonObject&)> updated)
        :api(api),roles(workspace["roles"].toArray()),members(workspace["members"].toArray()),
         actor(actor),updated(std::move(updated)),layout(new QVBoxLayout(this)) {
        layout->setContentsMargins(0,0,0,0);layout->setSpacing(6);
        setStyleSheet("QWidget#roleCard{background:#151515;border-radius:12px}"
                     "QPushButton{min-height:34px;background:#202020;border:0;border-radius:8px;padding:0 12px;font-size:11px}"
                     "QPushButton:hover,QPushButton:focus{background:#303030}"
                     "QCheckBox{background:transparent;font-size:12px}");
        render();
    }
private:
    const QList<QPair<QString,QString>> permissions={
        {"manage_server",UiText::ServerNameDescriptionAndAvatar()},{"manage_channels",UiText::CreateAndEditChannels()},
        {"manage_chat",UiText::DeleteMessagesAndTemporarilyRestrictPosting()},{"kick_members",UiText::DisableMediaAndDisconnectMembersFromVoice()},
        {"ban_members",UiText::BlockAccessToTheServer()},{"manage_roles",UiText::CreateAndAssignRoles()}};
    bool allowed(QString permission) const {
        return !actor["blocked"].toBool() && (actor["role"]=="owner" || actor["permissions"].toArray().contains(permission));
    }
    bool editable(const QJsonObject& role) const {
        if(actor["role"]=="owner")return true;
        if(!allowed("manage_roles") || role["id"]=="owner" ||
           actor["roleIds"].toArray().contains(role["id"]) || role["permissions"].toArray().contains("manage_roles"))return false;
        for(auto p:role["permissions"].toArray())if(!allowed(p.toString()))return false;
        return true;
    }
    QString describe(const QJsonObject& role) const {
        if(role["id"]=="owner")return UiText::TheOwnerHasAllPermissionsOnlyThe();
        QStringList names;
        for(auto p:role["permissions"].toArray())for(const auto& field:permissions)if(field.first==p.toString())names<<field.second;
        return names.isEmpty()?QString::fromUtf8(UiText::NoAdditionalPermissions()):names.join(" · ");
    }
    static QPushButton* action(QLayout* row,const QString& text,std::function<void()> callback){
        auto* button=new QPushButton(text);button->setCursor(Qt::PointingHandCursor);
        row->addWidget(button);connect(button,&QPushButton::clicked,button,std::move(callback));return button;
    }
    void cancel(){editing.reset();removal.clear();render();}
    void edit(QString id){editing=id;removal.clear();render();}
    void submit(QByteArray method,QString id,QJsonObject payload={}){
        if(!isEnabled())return;
        setEnabled(false);error->hide();
        const auto path=id.isEmpty()?QString("/api/roles"):"/api/roles/"+QString::fromLatin1(QUrl::toPercentEncoding(id));
        auto guard=QPointer<RoleEditor>(this);
        api.request(method,path,payload,[this,guard](auto,QString issue){
            if(!guard)return;
            if(!issue.isEmpty()){setEnabled(true);error->setText(issue);error->show();return;}
            api.request("GET","/api/workspace",{},[this,guard](auto data,QString issue){
                if(!guard)return;
                setEnabled(true);
                if(!issue.isEmpty()){error->setText(issue);error->show();return;}
                roles=data["roles"].toArray();members=data["members"].toArray();cancel();updated(data);
            });
        });
    }
    class Reveal final : public QGraphicsOpacityEffect {
    protected:
        void draw(QPainter* painter) override{
            painter->save();painter->translate(0,-4*(1-opacity()));
            QGraphicsOpacityEffect::draw(painter);painter->restore();
        }
    };
    void form(QVBoxLayout* card,const QJsonObject& role){
        const QString id=role["id"].toString();
        auto* top=new QHBoxLayout;top->setSpacing(10);card->addLayout(top);
        auto* name=new QLineEdit(role["name"].toString());
        name->setPlaceholderText(UiText::RoleName());name->setAccessibleName(UiText::RoleName());name->setMaxLength(40);
        name->setStyleSheet("QLineEdit{background:#202020;color:#ededed;border:0;border-radius:8px;padding:0 10px;font-size:12px;font-weight:500}QLineEdit:focus{background:#2c302f}");
        name->setFixedHeight(34);top->addWidget(name,1);
        const bool nameOnly=id=="owner" || id=="member";
        QList<QCheckBox*> checks;
        QHBoxLayout* actions=top;
        if(!nameOnly){
            auto* group=new QWidget;group->setObjectName("rolePermissions");group->setStyleSheet("QWidget#rolePermissions{background:transparent}");card->addWidget(group);
            auto* details=new QVBoxLayout(group);details->setContentsMargins(0,10,0,0);details->setSpacing(12);
            for(const auto& field:permissions){
                auto* check=new Ui::CheckBox(field.second,17,10,QColor("#bbb"));check->setChecked(role["permissions"].toArray().contains(field.first));
                check->setEnabled(allowed(field.first));checks.append(check);details->addWidget(check);
            }
            details->addSpacing(6);actions=new QHBoxLayout;details->addLayout(actions);
            if(Ui::animationsEnabled()){
                auto* reveal=new Reveal;reveal->setOpacity(0);group->setGraphicsEffect(reveal);
                auto* animation=new QPropertyAnimation(reveal,"opacity",reveal);
                QEasingCurve easeOut(QEasingCurve::BezierSpline);easeOut.addCubicBezierSegment({0,0},{.58,1},{1,1});
                animation->setEasingCurve(easeOut);animation->setDuration(140);
                animation->setStartValue(0.);animation->setEndValue(1.);
                connect(animation,&QPropertyAnimation::finished,group,[group]{group->setGraphicsEffect(nullptr);});
                animation->start();
            }
        }
        auto* save=action(actions,UiText::Save(),[this,name,checks,role,id,nameOnly]{
            if(name->text().trimmed().isEmpty()){error->setText(UiText::EnterARoleName());error->show();name->setFocus();return;}
            QJsonObject data{{"name",name->text().trimmed()}};
            if(id!="owner"){
                auto selected=role["permissions"].toArray();
                if(!nameOnly)for(int i=0;i<checks.size();++i){
                    const auto& key=permissions[i].first;
                    if(checks[i]->isChecked() && !selected.contains(key))selected.append(key);
                    if(!checks[i]->isChecked())for(int n=selected.size()-1;n>=0;--n)if(selected[n]==key)selected.removeAt(n);
                }
                data["permissions"]=selected;
            }
            submit(id.isEmpty()?"POST":"PATCH",id,data);
        });
        save->setStyleSheet("QPushButton{background:#e4e4e4;color:#111;font-weight:500}QPushButton:hover{background:white}");
        auto* dismiss=action(actions,UiText::Cancel(),[this]{cancel();});
        dismiss->setStyleSheet("QPushButton{background:transparent;color:#aaa} QPushButton:hover{background:#303030}");
        if(nameOnly)card->addWidget(detail(describe(role)));else actions->addStretch();
        connect(name,&QLineEdit::returnPressed,save,&QPushButton::click);
        auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),card->parentWidget());
        escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,[this]{cancel();});
        name->setFocus();name->selectAll();
    }
    static QLabel* detail(QString value){
        auto* label=Ui::text(value,"color:#999;font-size:12px");label->setWordWrap(true);return label;
    }
    void render(){
        Ui::clear(layout);
        auto* introduction=detail(UiText::MembersCanHaveSeveralRolesTheirPermissions());
        introduction->setMinimumHeight(21);layout->addWidget(introduction);
        layout->addSpacing(8);
        auto entries=roles;
        if(editing && editing->isEmpty())entries.append(QJsonObject{});
        for(const auto entry:entries){
            const auto role=entry.toObject();const QString id=role["id"].toString(),name=role["name"].toString();
            auto* panel=new QWidget;panel->setObjectName("roleCard");layout->addWidget(panel);
            auto* card=new QVBoxLayout(panel);card->setContentsMargins(15,12,15,12);card->setSpacing(8);
            if(editing && *editing==id){form(card,role);continue;}
            panel->setMinimumHeight(59);
            auto* row=new QHBoxLayout;row->setSpacing(12);card->addLayout(row);
            auto* summary=new QVBoxLayout;summary->setSpacing(5);row->addLayout(summary,1);
            summary->addWidget(Ui::text(name,"font-size:12px;font-weight:500"));auto* description=detail({});
            description->setStyleSheet("background:transparent;color:#737373;font-size:10px");
            description->setTextFormat(Qt::RichText);
            description->setText("<p style='margin:0;line-height:13px'>"+describe(role).toHtmlEscaped()+"</p>");
            summary->addWidget(description);
            if(!editable(role))continue;
            action(row,UiText::Edit(),[this,id]{edit(id);})->setAccessibleName(UiText::EditRole()+name);
            if(id=="owner" || id=="member" || id=="admin")continue;
            action(row,UiText::Delete3(),[this,id]{editing.reset();removal=id;render();})->setAccessibleName(UiText::DeleteRole()+name);
            if(removal==id){
                int count=0;for(auto member:members)if(member.toObject()["roleIds"].toArray().contains(id))++count;
                card->addWidget(Ui::text(UiText::DeleteRole2()+name+"»?","font-weight:500"));
                card->addWidget(detail(QString(UiText::ThisRoleWillBeRemovedFromAll()).arg(count)));
                auto* actions=new QHBoxLayout;card->addLayout(actions);
                action(actions,UiText::DeleteRole3(),[this,id]{submit("DELETE",id);});
                action(actions,UiText::Cancel(),[this]{cancel();});actions->addStretch();
            }
        }
        error=detail({});layout->addWidget(error);error->hide();
        if(allowed("manage_roles") && !editing){
            layout->addSpacing(12);auto* row=new QHBoxLayout;layout->addLayout(row);
            action(row,UiText::CreateRole(),[this]{edit({});});row->addStretch();
        }
    }
    ServerApi& api;
    QJsonArray roles,members;
    QJsonObject actor;
    std::function<void(const QJsonObject&)> updated;
    QVBoxLayout* layout;
    QLabel* error=nullptr;
    std::optional<QString> editing;
    QString removal;
};
