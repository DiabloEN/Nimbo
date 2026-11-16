#pragma once
#include "interface_text.h"
#include "api.h"
#include "ui.h"
#include <cmath>

class StorageSettings final : public QWidget {
public:
    explicit StorageSettings(ServerApi& api, QWidget* parent=nullptr):QWidget(parent),api(api) {
        setObjectName("storageSettings");
        auto* body=new QVBoxLayout(this);
        body->setContentsMargins(0,0,0,0);
        body->setSpacing(10);
        body->addWidget(Ui::text(UiText::ServerStorage(),"font-size:14px;font-weight:500"));
        body->addSpacing(10);

        meter=new QWidget;
        meter->setObjectName("storageMeter");
        meter->setStyleSheet("QWidget#storageMeter{background:#171717;border-radius:14px}");
        auto* stats=new QVBoxLayout(meter);
        stats->setContentsMargins(20,20,20,20);
        stats->setSpacing(0);
        auto* totals=new QHBoxLayout;
        totals->setSpacing(8);
        used=Ui::text("","font-weight:500;font-size:20px");
        quota=help({});
        totals->addWidget(used,0,Qt::AlignBaseline);
        totals->addWidget(quota,0,Qt::AlignBaseline);
        totals->addStretch();
        stats->addLayout(totals);
        bar=new QProgressBar;
        bar->setAccessibleName(UiText::SpaceUsed());
        bar->setRange(0,10000);
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        bar->setStyleSheet("QProgressBar{background:#333;border:0;border-radius:4px} QProgressBar::chunk{background:#66aaf0;border-radius:4px}");
        stats->addSpacing(16);
        stats->addWidget(bar);
        stats->addSpacing(10);
        count=help({});
        stats->addWidget(count);
        breakdown=new QWidget;
        breakdown->setStyleSheet("background:transparent");
        auto* categories=new QVBoxLayout(breakdown);
        categories->setContentsMargins(0,18,0,0);
        categories->setSpacing(0);
        const QList<QPair<QString,QString>> fields={{"messages",UiText::Messages()},{"photos",UiText::Photos()},{"video",UiText::Video()},{"audio",UiText::Audio()},{"files",UiText::Files()},{"other",UiText::ProfilesAndSettings()}};
        for(const auto& field:fields){
            auto* row=new QHBoxLayout;
            row->setContentsMargins(0,7,0,7);
            row->addWidget(Ui::text(field.second,"color:#aaa;font-size:13px"));
            row->addStretch();
            auto* value=Ui::text("","font-size:13px");
            values.insert(field.first,value);
            row->addWidget(value);
            categories->addLayout(row);
            auto* rule=new QFrame;rule->setFixedHeight(1);rule->setStyleSheet("background:#202020");
            categories->addWidget(rule);
        }
        stats->addWidget(breakdown);
        stats->addSpacing(14);
        disk=help({});stats->addWidget(disk);
        body->addWidget(meter);meter->hide();body->addSpacing(10);
        cards=new QBoxLayout(QBoxLayout::LeftToRight);cards->setSpacing(10);body->addLayout(cards);
        auto card=[this](const QString& title,const QString& unit,QLineEdit*& field,QLabel*& hint){
            auto* box=new QWidget;box->setObjectName("storageLimitCard");box->setStyleSheet("#storageLimitCard{background:#171717;border-radius:14px}");
            auto* layout=new QVBoxLayout(box);layout->setContentsMargins(14,14,14,14);layout->setSpacing(8);layout->setAlignment(Qt::AlignTop);
            auto* label=Ui::text(title,"font-size:12px");label->setWordWrap(true);layout->addWidget(label);
            auto* row=new QHBoxLayout;row->setSpacing(6);field=new QLineEdit;field->setAccessibleName(title+", "+unit);field->setEnabled(false);
            field->setFixedSize(68,38);field->setStyleSheet("QLineEdit{padding:0 7px;background:#202020;border:1px solid transparent;border-radius:10px;font-size:12px} QLineEdit:focus{border-color:#ddd}");row->addWidget(field);row->addWidget(help(unit));row->addStretch();
            layout->addLayout(row);hint=help({});layout->addWidget(hint);cards->addWidget(box,1);return box;
        };
        card(UiText::StorageLimit(),"GB",limit,maximum);
        fileCard=card(UiText::MaximumChatUploadSize(),"MB",fileLimit,fileMaximum);fileCard->hide();
        submit=new QPushButton(UiText::Update());submit->setAccessibleName(UiText::UpdateLimits());Ui::compactButton(submit);submit->setEnabled(false);
        body->addWidget(submit,0,Qt::AlignLeft);
        legacyNote=help(UiText::OnceTheLimitsAreUpdatedAttachmentsWill());legacyNote->hide();body->addWidget(legacyNote);
        body->addWidget(help(UiText::WhenStorageReaches90OfTheLimit()));
        feedback=help({});feedback->hide();body->addWidget(feedback);
        connect(submit,&QPushButton::clicked,this,[this]{save();});
        for(auto* field:{limit,fileLimit}){
            connect(field,&QLineEdit::returnPressed,submit,&QPushButton::click);
            connect(field,&QLineEdit::textEdited,this,[this]{status({});controls();});
        }
        connect(&poll,&QTimer::timeout,this,[this]{load();});
        poll.start(15000);
        load();
    }
    void languageChanged(){
        if(initialized && !limit->isModified()){
            auto locale=UiText::locale();
            locale.setNumberOptions(QLocale::OmitGroupSeparator);
            limit->setText(locale.toString(savedDisplay,'g',15));
        }
        if(!lastStats.isEmpty())render(lastStats);
        status(UiText::knownMessage(feedback->text()));
    }
protected:
    void resizeEvent(QResizeEvent* e) override{QWidget::resizeEvent(e);cards->setDirection(width()<=380?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);}
private:
    qint64 bytes()const{
        bool ok;const double n=QString(limit->text()).replace(',','.').toDouble(&ok);
        if(!ok||!std::isfinite(n)||n<8./1024||(maxGib>0 && n>maxGib)||n>=double(std::numeric_limits<qint64>::max())/gib)return 0;
        return n==savedDisplay?savedBytes:qRound64(n*gib);
    }
    qint64 cap()const{return qMin<qint64>(bytes(),1920LL*1048576)*4/(5*1048576);}
    void controls(){
        const auto n=cap();QLocale ru=UiText::locale();
        const auto value=n>=1024 && n%512==0?ru.toString(n/1024.,'g',4)+UiText::Gb():ru.toString(n)+UiText::Mb();
        fileMaximum->setText(UiText::UpTo()+value+UiText::Within80OfTheLimit());fileLimit->setAccessibleDescription(fileMaximum->text());
        submit->setEnabled(initialized && !saving && (legacy || bytes()!=savedBytes || (modern && fileLimit->text().toLongLong()!=savedFile/1048576)));
    }
    static QLabel* help(const QString& content){
        auto* label=Ui::text(content,"color:#999;font-size:12px");
        label->setWordWrap(true);
        return label;
    }
    static QString size(double bytes){
        const QStringList units={UiText::B(),UiText::Kb(),UiText::Mb2(),UiText::Gb2()};int i=0;
        while(bytes>=1024 && i<units.size()-1){bytes/=1024;++i;}
        return UiText::locale().toString(std::round(bytes*10)/10,'f',std::fmod(std::round(bytes*10),10)==0?0:1)+" "+units[i];
    }
    void status(const QString& message){
        feedback->setText(message);feedback->setVisible(!message.isEmpty());
    }
    void render(const QJsonObject& data){
        lastStats=data;
        const double usedBytes=data["usedBytes"].toDouble();
        const double maxBytes=data["policy"].toObject()["limitBytes"].toDouble();
        used->setText(size(usedBytes));quota->setText(UiText::Of()+size(maxBytes));
        bar->setValue(maxBytes>0?qRound(10000*qBound(0.,usedBytes/maxBytes,1.)):0);
        count->setText(UiText::Messages2()+QString::number(data["messages"].toInteger()));
        auto parts=data["breakdown"].toObject();
        breakdown->setVisible(data["breakdown"].isObject());
        for(auto it=values.begin();it!=values.end();++it)it.value()->setText(size(parts[it.key()].toDouble()));
        auto drive=data["disk"].toObject();
        disk->setText(drive.isEmpty()?UiText::DiskUsageUnavailable():QString(UiText::FreeSpaceOnTheStorageVolume1()).arg(size(drive["freeBytes"].toDouble()),size(drive["totalBytes"].toDouble())));
        maxGib=std::floor(drive["totalBytes"].toDouble()/gib*1000)/1000;
        maximum->setVisible(maxGib>0);
        maximum->setText(UiText::Maximum()+UiText::locale().toString(maxGib,'g',12)+UiText::Gb3());limit->setAccessibleDescription(maximum->text());
        savedBytes=data["policy"].toObject()["limitBytes"].toInteger();
        modern=data["policy"].toObject().contains("maxFileBytes");savedFile=data["policy"].toObject()["maxFileBytes"].toInteger();
        legacy=data["legacyDisabledFiles"].toBool();legacyNote->setVisible(legacy);fileCard->setVisible(modern);
        savedDisplay=maxGib>0?qMin(maxBytes/gib,maxGib):maxBytes/gib;
        if(!initialized){fileLimit->setText(QString::number(savedFile/1048576));
            double value=maxBytes/gib;
            QLocale locale=UiText::locale();locale.setNumberOptions(QLocale::OmitGroupSeparator);
            limit->setText(locale.toString(maxGib>0?qMin(value,maxGib):value,'g',15));
            initialized=true;
        }
        meter->show();limit->setEnabled(true);fileLimit->setEnabled(modern);controls();
    }
    void load(){
        if(saving || loading)return;
        loading=true;auto generation=revision;
        api.request("GET","/api/storage",{},[this,guard=QPointer<QWidget>(this),generation](auto data,QString error){
            if(!guard)return;
            loading=false;
            if(generation!=revision)return;
            if(error.isEmpty())render(data);else status(error);
        });
    }
    void save(){
        if(!initialized||saving)return;
        const auto quotaBytes=bytes();const auto file=fileLimit->text().toLongLong();
        if(!quotaBytes){status(UiText::CheckTheStorageLimit());limit->setFocus();return;}
        if(modern && (file<1 || file>cap())){status(UiText::CheckTheMaximumFileSize());fileLimit->setFocus();return;}
        QJsonObject patch{{"limitBytes",quotaBytes}};if(modern)patch["maxFileBytes"]=file*1048576;
        saving=true;++revision;status({});
        // Disabling a focused widget makes Qt scroll to the next focusable section.
        if(submit->hasFocus())submit->clearFocus();
        if(limit->hasFocus())limit->clearFocus();
        fileLimit->setEnabled(false);limit->setEnabled(false);submit->setEnabled(false);
        api.request("PATCH","/api/storage",patch,[this,guard=QPointer<QWidget>(this)](auto data,QString error){
            if(!guard)return;
            saving=false;limit->setEnabled(true);fileLimit->setEnabled(modern);
            if(error.isEmpty()){initialized=false;render(data);}else controls();
            status(error.isEmpty()?UiText::LimitsUpdated():error);
        });
    }
    static constexpr double gib=1073741824.;
    ServerApi& api;
    QJsonObject lastStats;
    QTimer poll;
    bool initialized=false,loading=false,saving=false;
    int revision=0;
    double maxGib=0;
    QWidget *meter,*breakdown;
    QLabel *used,*quota,*count,*disk,*maximum,*feedback;
    QProgressBar* bar;
    QLineEdit *limit,*fileLimit;
    QWidget* fileCard;QBoxLayout* cards;QLabel *fileMaximum,*legacyNote;bool modern=false,legacy=false;double savedDisplay=0;qint64 savedBytes=0,savedFile=0;
    QPushButton* submit;
    QHash<QString,QLabel*> values;
};
