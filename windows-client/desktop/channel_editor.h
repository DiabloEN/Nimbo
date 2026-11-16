#pragma once
#include "interface_text.h"
#include "api.h"
#include "ui.h"
#include "channel_order.h"

class ChannelPopup final : public QFrame {
public:
    explicit ChannelPopup(QPushButton* trigger,QString label,Qt::Alignment edge=Qt::AlignLeft):QFrame(trigger->window()),trigger(trigger),edge(edge){
        hide();setAccessibleName(label);
        connect(trigger,&QObject::destroyed,this,&QObject::deleteLater);
    }
    void toggle(){
        if(isVisible()){hide();return;}
        adjustSize();
        auto p=trigger->mapTo(parentWidget(),QPoint(edge==Qt::AlignRight?trigger->width()-width():0,trigger->height()+(edge==Qt::AlignRight?6:8)));
        if(p.y()+height()>parentWidget()->height())p.setY(trigger->mapTo(parentWidget(),QPoint()).y()-height()-8);
        move(qBound(0,p.x(),qMax(0,parentWidget()->width()-width())),qMax(0,p.y()));
        show();raise();qApp->installEventFilter(this);
        for(auto* tile:findChildren<QPushButton*>())if(tile->isChecked()){tile->setFocus();break;}
    }
protected:
    void hideEvent(QHideEvent* e) override{qApp->removeEventFilter(this);QFrame::hideEvent(e);}
    bool eventFilter(QObject* object,QEvent* e) override{
        if(!trigger){hide();return false;}
        auto* target=qobject_cast<QWidget*>(object);
        if(!target || target->window()!=trigger->window())return false;
        if(e->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(e)->key()==Qt::Key_Escape){
            hide();trigger->setFocus();return true;
        }
        if(e->type()==QEvent::MouseButtonPress){
            auto point=static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
            if(!rect().contains(mapFromGlobal(point))&&!trigger->rect().contains(trigger->mapFromGlobal(point)))hide();
        }
        if((object==trigger&&e->type()==QEvent::Hide)||(object==parentWidget()&&e->type()==QEvent::Resize))hide();
        return false;
    }
private:
    QPointer<QPushButton> trigger;
    Qt::Alignment edge;
};

class ChannelEditor final : public QWidget {
public:
    ChannelEditor(ServerApi& api,QJsonObject workspace,std::function<void(const QJsonObject&)> updated):
        api(api),workspace(std::move(workspace)),updated(std::move(updated)),body(new QVBoxLayout(this)){
        body->setContentsMargins(12,32,12,12);body->setSpacing(5);
        setStyleSheet("QWidget#channelEntry{background:#171717;border-radius:12px}"
                      "QPushButton{background:transparent;border:0;border-radius:7px;padding:0 10px;font-size:12px;color:#a0a0a0}"
                      "QPushButton:hover,QPushButton:focus{background:#282828}"
                      "QLineEdit,QComboBox{background:#202020;padding:0 7px;border:0;font-size:12px}");
        render();
    }
private:
    enum class Mode {List,Rename,NewSection,Edit,NewChannel,Remove};
    void showForm(Mode next,QString id={}){
        mode=next;target=id;render();
    }
    void mutate(QByteArray method,QString path,QJsonObject data={}){
        if(!isEnabled())return;
        setEnabled(false);error->hide();auto guard=QPointer<ChannelEditor>(this);
        api.request(method,path,data,[this,guard](auto,QString issue){
            if(!guard)return;
            if(!issue.isEmpty()){render();failed(issue);return;}
            api.request("GET","/api/workspace",{},[this,guard](auto data,QString issue){
                if(!guard)return;
                if(!issue.isEmpty()){failed(issue);return;}
                setEnabled(true);workspace=data;showForm(Mode::List);updated(data);
            });
        });
    }
    void failed(QString issue){setEnabled(true);error->setText(issue);error->show();}
    static QPushButton* action(QLayout* row,QString caption,QString description,std::function<void()> callback,QString icon={}){
        auto* b=new QPushButton(caption);b->setCursor(Qt::PointingHandCursor);b->setFixedHeight(36);
        if(!description.isEmpty()){b->setAccessibleName(description);b->setToolTip(description);}
        if(!icon.isEmpty()){b->setIcon(Ui::icon(icon));b->setIconSize({16,16});}
        row->addWidget(b);connect(b,&QPushButton::clicked,b,std::move(callback));return b;
    }
    QLineEdit* input(QHBoxLayout* row,QString text,QString label){
        auto* edit=new QLineEdit(text);edit->setMaxLength(60);edit->setAccessibleName(label);
        edit->setPlaceholderText(label);edit->setMinimumWidth(40);edit->setFixedHeight(34);row->addWidget(edit,1);
        auto* esc=new QShortcut(QKeySequence(Qt::Key_Escape),edit);
        connect(esc,&QShortcut::activated,this,[this]{showForm(Mode::List);});
        edit->setFocus();edit->selectAll();return edit;
    }
    QHBoxLayout* orderRow(QVBoxLayout* rows,QString id,QString group,QString name,const QStringList& siblings,QStringList childNames={}){
        const bool section=group.isEmpty();
        auto reorder=[this,section](QString from,QString to){
            if(from==to)return;
            QJsonArray sections=workspace["sectionOrder"].toArray(),channels;
            for(auto ch:workspace["channels"].toArray())channels.append(ch.toObject()["id"]);
            auto& order=section?sections:channels;
            int a=-1,b=-1;
            for(int i=0;i<order.size();++i){if(order[i]==from)a=i;if(order[i]==to)b=i;}
            if(a<0||b<0)return;
            order.removeAt(a);order.insert(b,from);
            mutate("PATCH","/api/channel-order",{{"sections",sections},{"channels",channels}});
        };
        auto* wrapper=new QWidget;wrapper->setObjectName("orderWrapper");wrapper->setStyleSheet("#orderWrapper{background:transparent}");rows->addWidget(wrapper);
        auto* host=new QWidget;
        if(!section)host->setObjectName("channelEntry");
        auto* inset=new QHBoxLayout(wrapper);inset->setContentsMargins(section?0:22,0,0,0);inset->addWidget(host);
        auto* row=new QHBoxLayout(host);row->setContentsMargins(section?0:8,section?0:8,section?0:8,section?0:8);row->setSpacing(section?8:10);
        auto* block=section?rows->parentWidget():wrapper;
        auto* peers=section?body:rows;
        block->setProperty("orderId",id);
        auto* handle=new OrderHandle(block,peers,name,childNames,[reorder,id,siblings](int delta){
            int i=siblings.indexOf(id)+delta;if(i>=0&&i<siblings.size())reorder(id,siblings[i]);
        });
        handle->setAccessibleName((section?UiText::MoveSection():UiText::MoveChannel())+name);
        handle->setEnabled(mode==Mode::List);row->addWidget(handle);return row;
    }
    void sectionForm(QHBoxLayout* row,const QJsonObject& section){
        bool creating=section.isEmpty();
        auto* name=input(row,section["name"].toString(),creating?UiText::NewSectionName():UiText::SectionName());
        QComboBox* kind=nullptr;
        if(creating){
            kind=new QComboBox;kind->setAccessibleName(UiText::SectionType());kind->addItem(UiText::Voice(),"voice");kind->addItem(UiText::Text(),"text");row->addWidget(kind);
        }
        auto save=[this,section,creating,name,kind]{
            if(name->text().trimmed().isEmpty()){name->setFocus();return;}
            QJsonObject data{{"name",name->text().trimmed()}};
            if(creating)data["kind"]=kind->currentData().toString();
            mutate(creating?"POST":"PATCH",creating?"/api/sections":"/api/sections/"+section["id"].toString(),data);
        };
        action(row,creating?UiText::Create():"",creating?UiText::Create():UiText::SaveSectionName(),save,creating?"":"check");
        connect(name,&QLineEdit::returnPressed,name,save);
        action(row,"",creating?UiText::CancelCreatingSection():UiText::CancelRename(),[this]{showForm(Mode::List);},"close");
    }
    void postingField(QHBoxLayout* row,const std::shared_ptr<QJsonObject>& policy){
        auto* trigger=action(row,"",UiText::WhoCanPost(),[]{},"chevronDown");
        trigger->setLayoutDirection(Qt::RightToLeft);trigger->setIconSize({12,12});
        trigger->setStyleSheet("QPushButton{background:#222;border-radius:10px;padding:0 8px;font-size:11px;color:#ededed} QPushButton:hover{background:#303030}");
        auto* popup=new ChannelPopup(trigger,UiText::WhoCanPost(),Qt::AlignRight);
        auto* shadow=new QGraphicsDropShadowEffect(popup);shadow->setBlurRadius(32);shadow->setOffset(0,12);shadow->setColor(QColor(0,0,0,136));popup->setGraphicsEffect(shadow);
        popup->setFixedWidth(240);popup->setStyleSheet("QFrame{background:#272727;border:0;border-radius:12px}");
        auto* layout=new QVBoxLayout(popup);layout->setContentsMargins(12,12,12,12);layout->setSpacing(0);
        layout->addWidget(Ui::text(UiText::WhoCanPost(),"font-size:11px;color:#999;padding:4px"));
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setFrameShape(QFrame::NoFrame);scroll->setStyleSheet("QScrollArea,QScrollArea QWidget{background:transparent}");layout->addWidget(scroll);
        auto* options=new QWidget;scroll->setWidget(options);
        auto* choices=new QVBoxLayout(options);choices->setContentsMargins(4,0,4,0);choices->setSpacing(0);
        auto add=[&](QString name,QString roleId){
            auto* box=new Ui::CheckBox(name,14);box->setProperty("roleId",roleId);
            box->setFixedHeight(34);choices->addWidget(box);return box;
        };
        add(UiText::AllMembers(),{});
        for(const auto entry:workspace["roles"].toArray()){
            const auto role=entry.toObject();const QString id=role["id"].toString();
            if(id!="owner")add(role["name"].toString(),id);
        }
        scroll->setFixedHeight(qMin(235,34*choices->count()));
        layout->addWidget(Ui::text(UiText::TheOwnerCanAlwaysPost(),"font-size:11px;color:#999;padding:4px"));
        const auto boxes=options->findChildren<QCheckBox*>();
        auto refresh=[trigger,boxes,policy]{
            const bool all=policy->value("mode")=="everyone";
            const auto ids=policy->value("roleIds").toArray();
            trigger->setText(all?UiText::EveryoneCanPost():ids.isEmpty()?UiText::OwnerOnly():QString(UiText::RolesThatCanPost1()).arg(ids.size()));
            for(auto* box:boxes){
                const auto id=box->property("roleId").toString();
                QSignalBlocker silent(box);box->setChecked(id.isEmpty()?all:!all&&ids.contains(id));
            }
        };
        for(auto* box:boxes)connect(box,&QCheckBox::toggled,popup,[box,policy,refresh](bool on){
            const auto id=box->property("roleId").toString();
            QJsonArray ids;
            if(!id.isEmpty()){
                for(const auto value:policy->value("roleIds").toArray())
                    if(value!=id)ids.append(value);
                if(on)ids.append(id);
            }
            *policy={{"mode",id.isEmpty()&&on?"everyone":"roles"},{"roleIds",ids}};
            refresh();
        });
        refresh();connect(trigger,&QPushButton::clicked,popup,&ChannelPopup::toggle);
    }
    void channelForm(QHBoxLayout* row,QString group,QString type,const QJsonObject& ch){
        const bool creating=ch.isEmpty();const QString id=ch["id"].toString();
        auto* picker=action(row,"",UiText::ChooseChannelIcon(),[]{},ch["icon"].toString(type=="text"?"hash":"sound"));
        picker->setFixedSize(creating?36:18,36);picker->setIconSize({18,18});
        picker->setStyleSheet("QPushButton{padding:0}");
        picker->setProperty("iconId",ch["icon"].toString(type=="text"?"hash":"sound"));
        const QList<QPair<QString,QString>> icons={{"hash",UiText::Text2()},{"sound",UiText::Voice2()},{"headphones",UiText::Headphones()},{"mic",UiText::Microphone()},
            {"chat",UiText::Chat()},{"music",UiText::Music()},{"game",UiText::Games()},{"coffee",UiText::Coffee()},{"home",UiText::Home()},{"users",UiText::Group()},
            {"star",UiText::Star()},{"heart",UiText::Heart()},{"book",UiText::Book()},{"code",UiText::Code()},{"cinema",UiText::Cinema()},{"radio",UiText::Broadcast()}};
        auto* gridHost=new ChannelPopup(picker,UiText::ChannelIcons());gridHost->setFixedWidth(224);
        gridHost->setStyleSheet("QWidget{background:#2b2b2b;border-radius:14px} QPushButton{background:transparent;border:0;border-radius:9px;padding:0} QPushButton:checked{background:#4a4a4a} QPushButton:hover{background:#444}");
        auto* grid=new QGridLayout(gridHost);grid->setContentsMargins(10,10,10,10);grid->setSpacing(5);
        for(int i=0;i<icons.size();++i){
            const auto icon=icons[i];
            auto* tile=Ui::action(icon.first,icon.second,42);tile->setMaximumWidth(QWIDGETSIZE_MAX);tile->setMinimumWidth(0);tile->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);tile->setIconSize({22,22});tile->setCheckable(true);
            tile->setAutoExclusive(true);tile->setChecked(picker->property("iconId")==icon.first);
            grid->addWidget(tile,i/4,i%4);
            connect(tile,&QPushButton::clicked,picker,[picker,icon,gridHost]{picker->setIcon(Ui::icon(icon.first));picker->setProperty("iconId",icon.first);gridHost->hide();picker->setFocus();});
        }
        connect(picker,&QPushButton::clicked,gridHost,&ChannelPopup::toggle);
        auto* name=input(row,ch["name"].toString(),creating?UiText::NewChannelName():UiText::ChannelName()+ch["name"].toString());
        QComboBox* dest=nullptr;
        if(!creating){
            dest=Ui::select(UiText::ChannelSection(),6);dest->setFixedSize(150,32);
            dest->setStyleSheet(dest->styleSheet()+"QComboBox{padding:0 5px 0 12px} QComboBox::drop-down{width:32px}");
            for(auto s:workspace["sections"].toArray()){auto v=s.toObject();if(v["kind"]==type)dest->addItem(v["name"].toString(),v["id"].toString());}
            dest->setCurrentIndex(dest->findData(group));row->addWidget(dest);
        }
        auto policy=std::make_shared<QJsonObject>(ch["posting"].toObject());
        if(type=="text"){
            if(policy->isEmpty())*policy={{"mode","everyone"},{"roleIds",QJsonArray{}}};
            postingField(row,policy);
        }
        auto save=[this,ch,id,creating,type,group,dest,name,picker,policy]{
            if(name->text().trimmed().isEmpty()){failed(UiText::EnterAChannelName());name->setFocus();return;}
            QJsonObject data{{"name",name->text()},{"kind",type},{"section",dest?dest->currentData().toString():group},
                {"description",ch["description"].toString()},{"icon",picker->property("iconId").toString()}};
            if(type=="text")data["posting"]=*policy;
            mutate(creating?"POST":"PATCH",creating?"/api/channels":"/api/channels/"+id,data);
        };
        action(row,creating?UiText::Create():"✓",creating?UiText::Create():UiText::SaveChannel(),save);
        connect(name,&QLineEdit::returnPressed,name,save);
        action(row,"",UiText::CancelChannelChanges(),[this]{showForm(Mode::List);},"close");
    }
    void render(){
        Ui::clear(body);
        error=Ui::text("","color:#efaaaa;font-size:12px");body->addWidget(error);error->hide();
        QStringList order;
        for(auto v:workspace["sectionOrder"].toArray())order<<v.toString();
        for(const auto& group:order){
            auto section=Ui::byId(workspace["sections"].toArray(),group);
            const auto title=section["name"].toString(),type=section["kind"].toString();
            QStringList ids;
            for(auto v:workspace["channels"].toArray()){
                auto ch=v.toObject();
                if(workspace["channelSections"].toObject()[ch["id"].toString()].toString(ch["section"].toString(ch["kind"].toString()))==group)ids<<ch["id"].toString();
            }
            auto* block=new QWidget;block->setStyleSheet("QWidget#orderSection{background:transparent}");block->setObjectName("orderSection");
            auto* rows=new QVBoxLayout(block);rows->setContentsMargins(0,0,0,42);rows->setSpacing(5);body->addWidget(block);
            QStringList childNames;for(const auto& channel:ids)childNames<<Ui::byId(workspace["channels"].toArray(),channel)["name"].toString();
            auto* header=orderRow(rows,group,{},title,order,childNames);
            if(mode==Mode::Rename&&target==group)sectionForm(header,section);
            else {auto* titleButton=action(header,title,UiText::EditSection()+title,[this,group]{showForm(Mode::Rename,group);});titleButton->setStyleSheet("color:#ededed;font-size:14px;font-weight:500");}
            header->addStretch();header->addWidget(Ui::text(type=="voice"?UiText::Voice():UiText::Text(),"font-size:10px;color:#999"));
            if(ids.isEmpty()&&order.size()>1)action(header,"",UiText::DeleteEmptySection()+title,[this,group]{mutate("DELETE","/api/sections/"+group);},"close");
            action(header,UiText::Add(),UiText::AddChannel()+title,[this,group]{showForm(Mode::NewChannel,group);},"plus");
            for(const auto& id:ids){
                auto ch=Ui::byId(workspace["channels"].toArray(),id);const QString name=ch["name"].toString();
                auto* row=orderRow(rows,id,group,name,ids);
                if(mode==Mode::Edit&&target==id){channelForm(row,group,type,ch);continue;}
                auto* mark=Ui::text("");mark->setPixmap(Ui::icon(ch["icon"].toString(type=="voice"?"sound":"hash")).pixmap(18,18));row->addWidget(mark);
                auto* info=new QVBoxLayout;row->addLayout(info,1);info->addWidget(Ui::text(name,"font-size:14px;font-weight:500"));
                if(ch["posting"].toObject()["mode"]=="roles")info->addWidget(Ui::text(UiText::PostByRole(),"font-size:10px;color:#999"));
                action(row,UiText::Edit(),UiText::Edit2()+name,[this,id]{showForm(Mode::Edit,id);});
                action(row,"",UiText::Delete()+name,[this,id]{showForm(Mode::Remove,id);},"close");
            }
            if(mode==Mode::NewChannel&&target==group){auto* row=new QHBoxLayout;rows->addLayout(row);channelForm(row,group,type,{});}
        }
        auto* row=new QHBoxLayout;body->addLayout(row);row->addStretch();
        if(mode==Mode::NewSection)sectionForm(row,{});
        else action(row,UiText::AddSection(),"",[this]{showForm(Mode::NewSection);},"plus");
        if(mode==Mode::Remove){
            auto name=Ui::byId(workspace["channels"].toArray(),target)["name"].toString();
            body->addWidget(Ui::text(UiText::Delete2()+name+"»?","font-weight:500"));
            auto* help=Ui::text(UiText::TheChannelAndItsHistoryWillBe());help->setWordWrap(true);body->addWidget(help);
            auto* actions=new QHBoxLayout;body->addLayout(actions);
            action(actions,UiText::DeleteChannel(),"",[this]{mutate("DELETE","/api/channels/"+target);});
            action(actions,UiText::Cancel(),"",[this]{showForm(Mode::List);});actions->addStretch();
        }
    }
    ServerApi& api;
    QJsonObject workspace;
    std::function<void(const QJsonObject&)> updated;
    QVBoxLayout* body;
    QLabel* error=nullptr;
    Mode mode=Mode::List;
    QString target;
};
