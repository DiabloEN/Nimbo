#pragma once
#include "interface_text.h"
#include "ui.h"
#include "window.h"

class MemberSettings final : public QWidget {
public:
    explicit MemberSettings(DesktopWindow& window): owner(window) {
        setObjectName("memberSettings");
        setStyleSheet("QWidget#memberResult{background:#171717;border-radius:12px}"
                      "QPushButton{border:0;border-radius:8px;padding:8px 12px;background:#252525;font-size:11px}"
                      "QPushButton:hover,QPushButton:focus{background:#303030}"
                      "QPushButton:disabled{color:#777} QPushButton#confirmBlock{color:#efaaaa;background:#302124}"
                      "QFrame#rolePopup{background:#242424;border:1px solid #353535;border-radius:10px}");
        auto* layout=new QVBoxLayout(this);
        layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto* search=new QHBoxLayout;search->setSpacing(12);layout->addLayout(search);
        query=new QLineEdit;query->setAccessibleName(UiText::SearchMembers());query->setClearButtonEnabled(true);
        query->setPlaceholderText(UiText::FindAMemberByName());search->addWidget(query,1);
        count=Ui::text("","font-size:11px;color:#a0a0a0");search->addWidget(count);
        layout->addSpacing(16);
        list=new QScrollArea;list->setFrameShape(QFrame::NoFrame);list->setWidgetResizable(true);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);layout->addWidget(list);
        auto* results=new QWidget;rows=new QVBoxLayout(results);rows->setContentsMargins(0,0,0,0);
        rows->setSpacing(6);rows->setAlignment(Qt::AlignTop);list->setWidget(results);
        empty=Ui::text("","font-size:12px;color:#a0a0a0;padding:16px 0");layout->addWidget(empty);
        footer=new QWidget;auto* pages=new QHBoxLayout(footer);pages->setContentsMargins(0,16,0,0);
        pages->setSpacing(12);range=Ui::text("","font-size:11px;color:#a0a0a0");pages->addWidget(range,1);
        prev=new QPushButton(UiText::Back());next=new QPushButton(UiText::Next());pageNumber=Ui::text("","font-size:11px;color:#a0a0a0");
        pages->addWidget(prev);pages->addWidget(pageNumber);pages->addWidget(next);layout->addWidget(footer);
        error=Ui::text("","font-size:12px;color:#efaaaa");error->setWordWrap(true);layout->addWidget(error);error->hide();
        connect(query,&QLineEdit::textChanged,this,[this]{page=1;render(true);});
        connect(prev,&QPushButton::clicked,this,[this]{--page;render(true);});
        connect(next,&QPushButton::clicked,this,[this]{++page;render(true);});
        render(true);
    }
    void render(bool reset=false){
        const QString filter=query->text().trimmed();
        QList<QJsonObject> matches;
        for(const auto value:owner.workspace["members"].toArray()){
            const auto member=value.toObject();
            if(member["name"].toString().contains(filter,Qt::CaseInsensitive))matches.append(member);
        }
        const int total=matches.size(),pages=qMax(1,(total+19)/20);
        page=qBound(1,page,pages);
        count->setText((filter.isEmpty()?QString(UiText::Total()):QString(UiText::Found()))+QString::number(total));
        footer->setVisible(pages>1);prev->setEnabled(page>1);next->setEnabled(page<pages);
        range->setText(QString(UiText::Value12Of3()).arg((page-1)*20+1).arg(qMin(page*20,total)).arg(total));
        pageNumber->setText(QString("%1 / %2").arg(page).arg(pages));
        empty->setVisible(total==0);empty->setText(filter.isEmpty()?UiText::NoMembersOnThisServerYet():UiText::NoMatchesTryAnotherName());
        Ui::clear(rows);
        for(int i=(page-1)*20;i<qMin(page*20,total);++i)addRow(matches[i]);
        list->setVisible(total>0);fitList();
        if(reset)list->verticalScrollBar()->setValue(0);
    }
protected:
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);fitList();}
private:
    void fitList(){list->setFixedHeight(qMin(rows->sizeHint().height(),qMin(640,qRound(owner.height()*.65))));}
    void addRow(const QJsonObject& member){
        const auto id=member["id"].toString(),name=member["name"].toString();
        auto* row=new QWidget;row->setObjectName("memberResult");row->setAttribute(Qt::WA_StyledBackground);
        auto* box=new QVBoxLayout(row);box->setContentsMargins(12,12,12,12);box->setSpacing(8);
        auto* line=new QHBoxLayout;line->setSpacing(12);box->addLayout(line);
        auto* avatar=Ui::text("");avatar->setPixmap(Ui::avatar(name,owner.avatars.value(id),34));line->addWidget(avatar);
        auto* labels=new QVBoxLayout;labels->setSpacing(3);line->addLayout(labels,1);
        auto* caption=Ui::text(name+(id==owner.selfId?UiText::You():""),"font-size:12px;font-weight:500");caption->setWordWrap(true);labels->addWidget(caption);
        const bool blocked=member["blocked"].toBool();
        const auto until=member["blockedUntil"].toInteger();
        auto role=owner.roleName(member);
        if(blocked)role=until?UiText::BlockedUntil()+UiText::locale().toString(QDateTime::fromMSecsSinceEpoch(until),"d MMM, HH:mm"):UiText::AccessBlocked();
        labels->addWidget(Ui::text(role,"font-size:11px;color:#a0a0a0"));
        const bool editable=id!=owner.selfId && member["role"]!="owner" &&
            (owner.self["role"]=="owner" || blocked || !member["permissions"].toArray().contains("manage_roles"));
        if(editable && !blocked && owner.allowed("manage_roles")){
            auto* roles=new QPushButton(UiText::Roles());roles->setAccessibleName(UiText::Roles2()+name);line->addWidget(roles);
            connect(roles,&QPushButton::clicked,this,[this,member,roles]{showRoles(member,roles);});
        }
        if(editable && owner.allowed("ban_members")){
            auto* block=new QPushButton(blocked?UiText::Unblock():UiText::Block());line->addWidget(block);
            connect(block,&QPushButton::clicked,this,[this,id]{moderation=id;render();});
            if(moderation==id)confirmModeration(box,member);
        }
        rows->addWidget(row);
    }
    void confirmModeration(QVBoxLayout* box,const QJsonObject& member){
        const bool blocked=member["blocked"].toBool();
        const auto id=member["id"].toString();
        box->addWidget(Ui::text((blocked?QString(UiText::Unblock2()):QString(UiText::BlockAccess()))+member["name"].toString()+"?","font-size:13px;font-weight:500"));
        QComboBox* duration=nullptr;
        if(!blocked){
            box->addWidget(Ui::text(UiText::BlockDuration(),"font-size:12px"));
            duration=Ui::select(UiText::BlockDuration());
            for(auto choice:{std::pair{UiText::Forever(),0},{UiText::Value15Minutes(),900},{UiText::Value1Hour(),3600},{UiText::Value1Day(),86400},{UiText::Value1Week(),604800},{UiText::Value30Days(),2592000}})
                duration->addItem(QString::fromUtf8(choice.first),choice.second);
            box->addWidget(duration);
        }
        auto* buttons=new QHBoxLayout;box->addLayout(buttons);
        auto* confirm=new QPushButton(UiText::Confirm());confirm->setObjectName("confirmBlock");buttons->addWidget(confirm);
        auto* cancel=new QPushButton(UiText::Cancel());buttons->addWidget(cancel);buttons->addStretch();
        connect(cancel,&QPushButton::clicked,this,[this]{moderation.clear();render();});
        connect(confirm,&QPushButton::clicked,this,[this,id,blocked,duration]{
            if(busy)return;
            busy=true;setEnabled(false);error->hide();
            owner.api.request("POST",memberPath(id)+"/moderate",{{"action",blocked?"unblock":"block"},{"seconds",duration?duration->currentData().toInt():0}},
                [this,guard=QPointer<MemberSettings>(this)](QJsonObject,QString issue){
                    if(!guard)return;
                    busy=false;setEnabled(true);
                    if(!issue.isEmpty()){error->setText(issue);error->show();return;}
                    moderation.clear();render();owner.refresh();
                });
        });
    }
    static QString memberPath(const QString& id){return "/api/members/"+QString::fromLatin1(QUrl::toPercentEncoding(id));}
    void showRoles(const QJsonObject& member,QPushButton* trigger){
        auto* popup=new QFrame(this,Qt::Popup);popup->setAttribute(Qt::WA_DeleteOnClose);
        popup->setObjectName("rolePopup");popup->setAccessibleName(trigger->accessibleName());popup->setFixedWidth(240);
        auto* fields=new QVBoxLayout(popup);fields->setContentsMargins(12,12,12,12);fields->setSpacing(4);
        fields->addWidget(Ui::text(member["name"].toString(),"font-size:12px;font-weight:500"));
        auto selected=member["roleIds"].toArray();
        if(!member["roleIds"].isArray() && member["role"]=="admin")selected.append("admin");
        popup->setProperty("selected",selected);
        QCheckBox* focus=nullptr;
        for(const auto value:owner.workspace["roles"].toArray()){
            const auto role=value.toObject();const auto key=role["id"].toString();
            if(key=="owner"||key=="member")continue;
            auto* cb=new Ui::CheckBox(role["name"].toString(),16);fields->addWidget(cb);cb->setChecked(selected.contains(key));
            for(const auto p:role["permissions"].toArray())if(!owner.allowed(p.toString()))cb->setEnabled(false);
            if(!focus&&cb->isEnabled())focus=cb;
            connect(cb,&QCheckBox::clicked,popup,[this,popup,cb,key,id=member["id"].toString()](bool on){
                auto roles=popup->property("selected").toJsonArray();
                if(on)roles.append(key);else for(int i=roles.size()-1;i>=0;--i)if(roles[i]==key)roles.removeAt(i);
                for(auto* option:popup->findChildren<QCheckBox*>()){option->setProperty("permitted",option->isEnabled());option->setEnabled(false);}
                owner.api.request("PUT",memberPath(id)+"/roles",{{"roleIds",roles}},
                    [this,guard=QPointer<MemberSettings>(this),panel=QPointer<QFrame>(popup),cb,on,roles](QJsonObject,QString issue){
                        if(!guard)return;
                        if(panel){
                            if(issue.isEmpty())panel->setProperty("selected",roles);else cb->setChecked(!on);
                            for(auto* option:panel->findChildren<QCheckBox*>())option->setEnabled(option->property("permitted").toBool());
                        }
                        error->setText(issue);error->setVisible(!issue.isEmpty());
                        if(issue.isEmpty())owner.refresh();
                    });
            });
        }
        if(!focus && owner.workspace["roles"].toArray().size()<2)fields->addWidget(Ui::text(UiText::NoAdditionalRoles()));
        popup->adjustSize();
        const QRect area(owner.mapToGlobal(QPoint(0,0)),owner.size());
        const auto anchor=trigger->mapToGlobal(QPoint(trigger->width(),trigger->height()+6));
        popup->move(qBound(area.left()+8,anchor.x()-240,area.right()-247),
                    qMax(area.top()+8,qMin(anchor.y(),area.bottom()-popup->height()-7)));
        popup->show();if(focus)focus->setFocus();
    }
    DesktopWindow& owner;
    QLineEdit* query;
    QLabel *count,*empty,*range,*pageNumber,*error;
    QWidget* footer;
    QScrollArea* list;
    QVBoxLayout* rows;
    QPushButton *prev,*next;
    int page=1;
    QString moderation;
    bool busy=false;
};
