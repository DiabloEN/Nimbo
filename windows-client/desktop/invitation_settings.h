#pragma once
#include "interface_text.h"
#include "api.h"
#include "server_links.h"
#include "ui.h"

class InvitationText final : public QLineEdit {
public:
    using QLineEdit::QLineEdit;
protected:
    void focusInEvent(QFocusEvent* event) override {
        QLineEdit::focusInEvent(event);
        selectAll();
    }
};

class InvitationSettings final : public QWidget {
public:
    InvitationSettings(ServerApi& api,const QJsonArray& entries,std::function<void()> changed)
        :api(api),changed(std::move(changed)),entries(entries) {
        setObjectName("invitationSettings");
        setStyleSheet("QWidget#inviteCard,QWidget#inviteForm{background:#151515;border-radius:12px}"
                     "QPushButton{background:#252525;border:0;padding:9px 12px;border-radius:8px;font-size:11px}"
                     "QPushButton:hover,QPushButton:focus{background:#303030}"
                     "QPushButton[quiet=true]{background:transparent;color:#aaa}"
                     "QPushButton#inviteSubmit{background:#e4e4e4;color:#111}"
                     "QLineEdit#inviteLink{background:#202020;border:0;border-radius:8px;padding:6px 12px;font-size:11px}");
        auto* column=new QVBoxLayout(this);
        column->setContentsMargins(0,0,0,0);column->setSpacing(0);
        error=Ui::text("","color:#efaaaa;font-size:12px");error->setWordWrap(true);column->addWidget(error);error->hide();
        empty=Ui::text(UiText::NoActiveInvitations(),"color:#a0a0a0;font-size:12px");empty->setMinimumHeight(21);column->addWidget(empty);
        scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);column->addWidget(scroll);
        auto* list=new QWidget;cards=new QVBoxLayout(list);cards->setContentsMargins(0,0,0,0);cards->setSpacing(8);
        cards->setAlignment(Qt::AlignTop);scroll->setWidget(list);
        column->addSpacing(16);
        start=new QPushButton(UiText::CreateInvitation());column->addWidget(start,0,Qt::AlignLeft);
        form=new QWidget;form->setObjectName("inviteForm");form->setAttribute(Qt::WA_StyledBackground);
        auto* fields=new QVBoxLayout(form);fields->setContentsMargins(16,14,16,14);fields->setSpacing(8);
        options=new QBoxLayout(QBoxLayout::LeftToRight);options->setSpacing(12);fields->addLayout(options);
        auto* typeField=new QVBoxLayout;typeField->setSpacing(8);options->addLayout(typeField,1);
        typeField->addWidget(Ui::text(UiText::Invitation(),"font-size:11px"));
        duration=Ui::select(UiText::InvitationType());
        for(auto option:{std::pair{UiText::Personal(),-1},{UiText::Value1Hour(),3600},{UiText::Value1Day(),86400},{UiText::Value1Week(),604800},{UiText::Value30Days(),2592000},{UiText::NoExpiry(),0}})
            duration->addItem(QString::fromUtf8(option.first),option.second);
        typeField->addWidget(duration);
        auto* labelField=new QVBoxLayout;labelField->setSpacing(8);options->addLayout(labelField,1);
        const QString caption=UiText::LabelOptional();
        labelField->addWidget(Ui::text(caption,"font-size:11px"));
        label=new QLineEdit;label->setObjectName("inviteLabel");label->setAccessibleName(caption);label->setMaxLength(80);
        label->setPlaceholderText(UiText::ForExampleForTheTeam());label->setFixedHeight(42);
        labelField->addWidget(label);labelField->setAlignment(Qt::AlignTop);fields->addSpacing(6);
        auto* actions=new QHBoxLayout;actions->setSpacing(8);fields->addLayout(actions);
        auto* save=new QPushButton(UiText::CreateInvitation());save->setObjectName("inviteSubmit");actions->addWidget(save);
        auto* cancel=new QPushButton(UiText::Cancel());cancel->setProperty("quiet",true);actions->addWidget(cancel);actions->addStretch();
        column->addWidget(form);form->hide();
        connect(start,&QPushButton::clicked,this,[this]{label->clear();error->hide();start->clearFocus();edit(true);});
        connect(cancel,&QPushButton::clicked,this,[this,cancel]{cancel->clearFocus();edit(false);});
        connect(save,&QPushButton::clicked,this,[this]{
            const int seconds=duration->currentData().toInt();
            QJsonObject body{{"singleUse",seconds<0},{"seconds",qMax(0,seconds)}};
            const auto description=label->text().trimmed();
            if(!description.isEmpty())body["label"]=description;
            mutate({},body);
        });
        connect(label,&QLineEdit::returnPressed,save,&QPushButton::click);
        markerTimer.setSingleShot(true);
        connect(&markerTimer,&QTimer::timeout,this,[this]{createdId.clear();if(marker)marker->hide();});
        connect(&expiry,&QTimer::timeout,this,[this]{render();});expiry.start(1000);render();
    }
    void sync(const QJsonArray& next){entries=next;render();}
protected:
    void resizeEvent(QResizeEvent* event) override{
        QWidget::resizeEvent(event);
        options->setDirection(window()->width()<=500?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    }
private:
    void revealCreated(){
        if(!marker || createdId.isEmpty())return;
        auto* card=marker->parentWidget();
        scroll->ensureWidgetVisible(card,0,0);
        for(auto* parent=scroll->parentWidget();parent;parent=parent->parentWidget())
            if(auto* outer=qobject_cast<QScrollArea*>(parent)){outer->ensureWidgetVisible(card,0,0);break;}
    }
    void edit(bool creating){form->setVisible(creating);start->setVisible(!creating);}
    void mutate(const QString& id,const QJsonObject& body){
        if(busy)return;
        busy=true;error->hide();setEnabled(false);
        const auto path=id.isEmpty()?QString("/api/invitations"):"/api/invitations/"+QString::fromLatin1(QUrl::toPercentEncoding(id));
        api.request(id.isEmpty()?"POST":"DELETE",path,body,[this,guard=QPointer<InvitationSettings>(this),id](QJsonObject result,QString issue){
            if(!guard)return;
            busy=false;setEnabled(true);
            if(!issue.isEmpty()){error->setText(issue);error->show();return;}
            if(id.isEmpty()){
                createdId=result["id"].toString();label->clear();markerTimer.start(15000);
                for(int i=entries.size()-1;i>=0;--i)if(entries[i].toObject()["id"]==createdId)entries.removeAt(i);
                entries.append(result);
                initialized=false;
            }
            else for(int i=entries.size()-1;i>=0;--i)if(entries[i].toObject()["id"]==id)entries.removeAt(i);
            edit(false);render();
            if(id.isEmpty())QTimer::singleShot(0,this,&InvitationSettings::revealCreated);
            changed();
        });
    }
    void render(){
        const auto now=QDateTime::currentMSecsSinceEpoch();
        QList<QJsonObject> active;
        for(auto value:entries){
            const auto item=value.toObject();const auto end=item["expiresAt"].toInteger();
            if(!end || end>now)active.append(item);
        }
        std::reverse(active.begin(),active.end());
        std::stable_sort(active.begin(),active.end(),[](const auto& a,const auto& b){return a["createdAt"].toInteger()>b["createdAt"].toInteger();});
        if(initialized && active==displayed)return;
        initialized=true;displayed=active;Ui::clear(cards);marker=nullptr;
        empty->setVisible(active.isEmpty());scroll->setVisible(!active.isEmpty());
        for(const auto& item:active){
            const auto id=item["id"].toString(),code=item["code"].toString();
            const auto payload=ServerLinks::invitation(api.server,code);
            auto* card=new QWidget;card->setObjectName("inviteCard");card->setAttribute(Qt::WA_StyledBackground);
            auto* box=new QVBoxLayout(card);box->setContentsMargins(16,14,16,14);box->setSpacing(4);
            auto* meta=new QHBoxLayout;meta->setSpacing(12);box->addLayout(meta);
            const bool personal=item["singleUse"].toBool();
            meta->addWidget(Ui::text(personal?UiText::Personal():UiText::Reusable(),"font-size:12px;font-weight:500"));
            if(!createdId.isEmpty() && id==createdId){
                marker=Ui::text(UiText::JustCreated(),"font-size:11px;color:#a0a0a0");meta->addWidget(marker);
            }
            meta->addStretch();auto* revoke=new QPushButton(UiText::Revoke());revoke->setProperty("quiet",true);
            revoke->setAccessibleName(UiText::RevokeInvitation()+id);meta->addWidget(revoke);
            connect(revoke,&QPushButton::clicked,this,[this,id]{mutate(id,{});});
            const auto description=item["label"].toString();
            if(!description.isEmpty()){
                auto* caption=Ui::text(description,"font-size:13px");caption->setTextFormat(Qt::PlainText);
                caption->setWordWrap(true);box->addWidget(caption);
            }
            const auto date=[](qint64 ms){
                return UiText::locale().toString(QDateTime::fromMSecsSinceEpoch(ms),UiText::DMmmYyyyHhMm());
            };
            QStringList dates;
            if(auto created=item["createdAt"].toInteger())dates.append(UiText::Created()+date(created));
            const auto end=item["expiresAt"].toInteger();
            dates.append(end?UiText::Until()+date(end):(personal?UiText::UntilFirstSignIn():UiText::NoExpiry2()));
            auto* info=Ui::text(dates.join("    "),"font-size:11px;color:#a0a0a0");
            info->setWordWrap(true);box->addWidget(info);box->addSpacing(6);
            auto* text=new InvitationText;text->setObjectName("inviteLink");text->setReadOnly(true);text->setText(payload);text->setAccessibleName(UiText::InvitationLink());
            text->setFixedHeight(38);box->addWidget(text);box->addSpacing(6);
            auto* copy=new QPushButton(id==copied?UiText::Copied():UiText::CopyInvitation());box->addWidget(copy,0,Qt::AlignLeft);
            connect(copy,&QPushButton::clicked,this,[this,copy,id,payload]{
                QApplication::clipboard()->setText(payload);
                if(lastCopy && lastCopy!=copy)lastCopy->setText(UiText::CopyInvitation());
                copied=id;lastCopy=copy;copy->setText(UiText::Copied());
            });
            if(id==copied)lastCopy=copy;
            cards->addWidget(card);
            card->show();
        }
        cards->invalidate();
        cards->activate();
        const int listWidth=qMax(1,scroll->viewport()->width());
        const int listHeight=qMax(cards->sizeHint().height(),cards->heightForWidth(listWidth));
        scroll->setFixedHeight(qMin(400,listHeight));
    }
    ServerApi& api;
    std::function<void()> changed;
    QJsonArray entries;
    QList<QJsonObject> displayed;
    bool busy=false,initialized=false;
    QString copied;
    QPointer<QPushButton> lastCopy;
    QTimer expiry,markerTimer;
    QString createdId;
    QPointer<QLabel> marker;
    QBoxLayout* options;
    QLineEdit* label;
    QVBoxLayout* cards;
    QScrollArea* scroll;
    QLabel *empty,*error;
    QPushButton* start;
    QWidget* form;
    QComboBox* duration;
};
