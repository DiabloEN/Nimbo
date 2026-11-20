#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include "message_text.h"
#include "message_row.h"
#include "message_input.h"
#include "storage_settings.h"
#include "desktop_updates.h"
#include "link_card.h"
#include "server_updates_panel.h"
#include "antivirus_settings.h"
#include "effect_card.h"
#include "workspace_identity.h"
#include "invitation_settings.h"
#include "wrap_layout.h"
#include "avatar_editor.h"
#include "audio_check.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QCameraDevice>
#include <QImageReader>

using namespace Ui;
namespace {
class ElidedQuote final : public QLabel {
public:
    explicit ElidedQuote(QString value):value(std::move(value)){
        this->value.replace(QRegularExpression("\\s+")," ");
        setTextFormat(Qt::PlainText);setStyleSheet("background:transparent;color:#aaa;font-size:11px");
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);setFixedHeight(13);setAccessibleName(this->value);
    }
protected:
    void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);setText(fontMetrics().elidedText(value,Qt::ElideRight,e->size().width()));
    }
private:
    QString value;
};
QString enc(QString s){return QString::fromLatin1(QUrl::toPercentEncoding(s));}
QPushButton* linkButton(QVBoxLayout* body,QString name,std::function<void()> fn){
    auto* b=new QPushButton(name);b->setCursor(Qt::PointingHandCursor);body->addWidget(b);
    QObject::connect(b,&QPushButton::clicked,b,std::move(fn));return b;
}
}
QString DesktopWindow::selectAvatar(QWidget* parent,bool square){
    auto path=QFileDialog::getOpenFileName(parent,UiText::ChooseAvatar(),{},UiText::ImagesJpgJpegPngWebp());
    if(path.isEmpty())return {};if(QFileInfo(path).size()>8*1024*1024){QMessageBox::information(parent,UiText::Avatar(),UiText::ChooseAPngJpegOrWebpImage());return {};}QImageReader reader(path);reader.setAutoTransform(true);auto size=reader.size();
    if(!size.isValid()||qint64(size.width())*size.height()>80000000)return {};
    reader.setScaledSize(size.scaled(768,768,Qt::KeepAspectRatio));auto image=reader.read();if(image.isNull())return {};
    QDialog dialog(parent);dialog.setWindowTitle(UiText::CropAvatar());QVBoxLayout layout(&dialog);layout.setContentsMargins(24,24,24,24);layout.setSpacing(12);
    layout.addWidget(text(UiText::CropAvatar(),"font-size:20px;font-weight:500"));layout.addWidget(text(UiText::MoveTheImageAndAdjustTheZoom(),"font-size:12px;color:#aaa"));
    AvatarCrop crop(image,square);layout.addWidget(&crop,0,Qt::AlignHCenter);QLabel amount(UiText::Zoom100());layout.addWidget(&amount);
    QSlider zoom(Qt::Horizontal);zoom.setRange(100,400);zoom.setAccessibleName(UiText::Zoom());layout.addWidget(&zoom);
    connect(&zoom,&QSlider::valueChanged,&crop,[&](int v){crop.setZoom(v);amount.setText(QString(UiText::Zoom1()).arg(v));});
    QDialogButtonBox actions;actions.addButton(UiText::Apply(),QDialogButtonBox::AcceptRole);actions.addButton(UiText::Cancel(),QDialogButtonBox::RejectRole);layout.addWidget(&actions);
    connect(&actions,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&actions,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return {};image=crop.result();
    QByteArray bytes;QBuffer b(&bytes);b.open(QIODevice::WriteOnly);if(!image.save(&b,"JPEG",85))return {};
    return "data:image/jpeg;base64,"+QString::fromLatin1(bytes.toBase64());
}

void DesktopWindow::updateSettingsTitle(QString sectionName){
    QStringList labels={UiText::Settings(),"·",sectionName};
    if(view=="roles")labels<<"·"<<QString::number(workspace["members"].toArray().size());
    title->setAccessibleName(labels.join(' '));
    QString markup="<table cellspacing='0' cellpadding='0'><tr>";
    for(int i=0;i<labels.size();++i){
        if(i)markup+="<td width='12'></td>";
        const bool quiet=i==1 || i>=3;
        markup+="<td style='color:"+QString(quiet?"#737373":"#ededed")+"'>"+labels[i].toHtmlEscaped()+"</td>";
    }
    title->setTextFormat(Qt::RichText);title->setText(markup+"</tr></table>");
}
void DesktopWindow::openView(const QString& next){
    if(previewingOverlay && next!="overlay"){previewingOverlay=false;syncGameOverlay();}
    if(next=="profile"&&view!=next){draftProfileName=self.value("name").toString(nickname->text());draftProfileAvatar=profile.value("avatar").toString();}
    view=next;bool room=view=="room";content->setCurrentWidget(room?roomPage:detailPage);
    backButton->setVisible(!room);
    if(room || view=="profile"){title->setTextFormat(Qt::PlainText);title->setAccessibleName({});}
    title->setStyleSheet(room?"background:transparent;font-size:19px;font-weight:500":"background:transparent;font-size:22px;font-weight:500");
    titleIcon->setVisible(room);roomCount->setVisible(room&&kind=="voice");notifyButton->setVisible(room&&kind=="text");
    if(room){clear(detailLayout);if(workspaceLanguageDirty && !workspace.isEmpty()){workspaceLanguageDirty=false;renderWorkspace(workspace,WorkspaceRefresh::All);}else selectChannel();}else{refreshStage();updateMentionIndicators();renderView();}layoutForWidth();refreshDock();
}

void DesktopWindow::addEffectCards(QHBoxLayout* row,QWidget* section){
    QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
    // Loading a QPixmap by filename also keeps the full image in Qt's global pixmap cache.
    QImageReader reader(QCoreApplication::applicationDirPath()+"/assets/room-preview.png");
    const auto side=qMin(reader.size().height(),qCeil(220*section->devicePixelRatioF()));
    if(side>0)reader.setScaledSize({side*2,side});
    const auto photo=QPixmap::fromImage(reader.read());
    auto* choices=new QButtonGroup(section);
    for(bool blurred:{false,true}){
        auto* choice=new EffectCard(photo,blurred,prefs.value("blurRadius",12).toInt());
        choices->addButton(choice);row->addWidget(choice,1);
        choice->setChecked(prefs.value("blur",false).toBool()==blurred);
        connect(choice,&QPushButton::clicked,choice,[this,blurred]{
            QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
            updateCameraEffect(blurred,prefs.value("blurRadius",12).toInt());
        });
        if(choice->slider)connect(choice->slider,&QSlider::valueChanged,choice,[this](int value){updateCameraEffect(true,value);});
    }
}

void DesktopWindow::showCameraEffects(){
    auto* panel=new QFrame(this,Qt::Popup);panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setObjectName("quickEffectsPanel");panel->setAccessibleName(UiText::CameraEffects());panel->setFixedWidth(qMin(380,width()-40));
    panel->setStyleSheet("#quickEffectsPanel{background:#202020;border-radius:16px}");
    auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(16,16,16,16);layout->setSpacing(12);
    auto* head=new QHBoxLayout;head->addWidget(text(UiText::CameraEffects(),"font-size:14px;font-weight:500"));head->addStretch();
    auto* close=action("close",UiText::CloseEffects());head->addWidget(close);connect(close,&QPushButton::clicked,panel,&QWidget::close);
    layout->addLayout(head);auto* row=new QHBoxLayout;row->setSpacing(12);layout->addLayout(row);addEffectCards(row,panel);
    const int preferredHeight=layout->totalHeightForWidth(panel->width());
    panel->setFixedHeight(preferredHeight>0?preferredHeight:layout->sizeHint().height());panel->adjustSize();QPainterPath outline;outline.addRoundedRect(panel->rect(),16,16);
    panel->setMask(QRegion(outline.toFillPolygon().toPolygon()));
    const bool inHeader=dock->parentWidget()==header;
    const int dx=inHeader || width()<=1050?quickEffects->width()-panel->width():0;
    auto target=quickEffects->mapToGlobal(QPoint(dx,inHeader?quickEffects->height()+12:-panel->height()-12));
    target.setX(qBound(mapToGlobal(QPoint(8,0)).x(),target.x(),mapToGlobal(QPoint(width()-panel->width()-8,0)).x()));
    panel->move(target);panel->show();close->setFocus();
}

void DesktopWindow::renderView(){
    if(localHome && pages->currentWidget()==localHome){openLocalHome();return;}
    int oldScroll=0;
    if(detailLayout->count()){
        auto* previous=detailLayout->itemAt(0)->widget()->findChild<QScrollArea*>("settingsScroll");
        if(previous)oldScroll=previous->verticalScrollBar()->value();
    }
    clear(detailLayout);
    if(view=="profile"){renderProfile();return;}
    auto* host=new QWidget;detailLayout->addWidget(host);
    auto* frame=new QHBoxLayout(host);frame->setSpacing(40);
    auto* nav=new QWidget;nav->setObjectName("settingsNavPanel");frame->addWidget(nav,0,Qt::AlignTop);
    auto* scroll=new QScrollArea;scroll->setObjectName("settingsScroll");scroll->setWidgetResizable(true);frame->addWidget(scroll,1);
    auto* page=new QWidget;scroll->setWidget(page);page->setObjectName("settingsPage");auto* columns=new QVBoxLayout(page);
    auto* tabs=new WrapLayout(nav);tabs->setSpacing(7);
    struct Tab{QString id,label,icon;};
    for(const auto& tab:QList<Tab>{{"general",UiText::General(),"settings"},{"audio",UiText::Sound(),"headphones"},{"video",UiText::Video(),"camera"},{"overlay",UiText::GameOverlay(),"screen"},{"channels",UiText::Channels(),"hash"},{"roles",UiText::MembersAndRoles(),"users"},{"keys",UiText::KeyboardShortcuts(),"keyboard"}}){
        if(tab.id=="channels" && !allowed("manage_server") && !allowed("manage_channels"))continue;
        auto* b=new QPushButton(icon(tab.icon),tab.label);b->setFixedHeight(45);b->setIconSize({19,19});
        b->setStyleSheet(QString("QPushButton{text-align:left;padding:0 13px;border:0;border-radius:10px;background:%1;color:%2;font-size:12px} QPushButton:hover{background:#202020} QPushButton:focus{border:1px solid #ddd}").arg(tab.id==view?"#242424":"transparent",tab.id==view?"#eee":"#a0a0a0"));
        tabs->addWidget(b);connect(b,&QPushButton::clicked,b,[this,tab]{openView(tab.id);});if(tab.id==view)updateSettingsTitle(tab.label);
    }
    auto* section=new QWidget;section->setObjectName("settingsSection");section->setMaximumWidth(680);section->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    columns->addWidget(section);columns->addStretch();auto* body=new QVBoxLayout(section);body->setContentsMargins(0,0,0,0);body->setSpacing(10);
    auto button=[body](QString s,std::function<void()> fn){return linkButton(body,s,std::move(fn));};
    auto heading=[body](QString s){body->addWidget(text(s,"font-weight:500;font-size:17px"));};
    {
        if(view=="general"){
            body->addWidget(text(UiText::Language(),"font-size:12px"));auto* language=Ui::select(UiText::Language());language->setFixedHeight(38);language->setObjectName("profileLanguage");
            language->addItem(UiText::SystemLanguage(),"system");
            language->addItem(UiText::Russian(),"ru");
            language->addItem(UiText::English(),"en");
            language->setCurrentIndex(qMax(0,language->findData(profile["language"].toString())));
            body->addWidget(language);
            if(self["role"]=="owner"){body->addSpacing(22);body->addWidget(new StorageSettings(api));body->addSpacing(14);body->addWidget(new AntivirusSettings(api));}
            body->addSpacing(14);auto* panel=new ServerUpdatesPanel(api,*updates,self["role"]=="owner",scroll->viewport(),serverOffer,
                [this](const QJsonObject& data){recordServerOffer(data);return serverOffer;},
                [this](const QString& desktop,const QString& server){markOffersRead(desktop,server);});
            body->addWidget(panel);
            // Bind only authored settings copy, before any asynchronous server values arrive.
            const auto labels=authoredChrome(host);
            connect(language,&QComboBox::currentIndexChanged,language,[this,language,panel,section,labels]{
                profile["language"]=language->currentData().toString();
                saveLocal();
                applyProfileLanguage();
                translateChrome(labels);
                language->setItemText(0,UiText::SystemLanguage());
                updateSettingsTitle(UiText::General());
                if(auto* storage=section->findChild<QWidget*>("storageSettings"))
                    static_cast<StorageSettings*>(storage)->languageChanged();
                if(auto* antivirus=section->findChild<QWidget*>("antivirusSettings"))
                    static_cast<AntivirusSettings*>(antivirus)->languageChanged();
                panel->languageChanged();
                refreshDock();
                layoutForWidth();
            });
        }
        if(view=="audio"||view=="video"){
            auto device=[this,body](QString label,const auto& list,QByteArray* target,QString key){body->addWidget(text(label,"font-size:12px;color:#b9b9b9"));auto* c=Ui::select(label);c->setFixedHeight(38);c->setObjectName("device-"+key);c->setEnabled(!joining);
                QString defaultName=key=="camera"?UiText::DefaultCamera():UiText::SystemDefault();
                if(key!="camera"){
                    auto device=key=="input"?QMediaDevices::defaultAudioInput():QMediaDevices::defaultAudioOutput();
                    if(!device.description().isEmpty())defaultName+=" ("+device.description()+")";
                }
                c->addItem(defaultName,QByteArray());for(const auto& d:list)c->addItem(d.description(),d.id());c->setCurrentIndex(qMax(0,c->findData(*target)));body->addWidget(c);
                connect(c,&QComboBox::currentIndexChanged,c,[this,c,target,key,label]{
                    if(inCall && key!="camera"){
                        c->setEnabled(false);media->selectAudioDevice(key=="input",c->currentData().toByteArray());
                        return;
                    }
                    *target=c->currentData().toByteArray();QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue(key,*target);});};
            if(view=="audio"){device(UiText::Microphone(),QMediaDevices::audioInputs(),&inputDevice,"input");body->addSpacing(8);device(UiText::HeadphonesSpeakers(),QMediaDevices::audioOutputs(),&outputDevice,"output");
                auto* refresh=new QPushButton(UiText::RefreshDeviceList());Ui::compactButton(refresh);
                body->addWidget(refresh,0,Qt::AlignLeft);
                auto reload=[this,section,refresh]{
                    refresh->setEnabled(false);refresh->setText(UiText::CheckingDevices());
                    QTimer::singleShot(0,section,[this,section,refresh]{
                        for(bool input:{true,false}){
                            const auto key=input?QStringLiteral("input"):QStringLiteral("output");
                            auto* combo=section->findChild<QComboBox*>("device-"+key);if(!combo)continue;
                            auto& saved=input?inputDevice:outputDevice;
                            const auto list=input?QMediaDevices::audioInputs():QMediaDevices::audioOutputs();
                            const auto def=input?QMediaDevices::defaultAudioInput():QMediaDevices::defaultAudioOutput();
                            {QSignalBlocker block(combo);combo->clear();combo->addItem(def.isNull()?UiText::SystemDefault():UiText::SystemDefault2()+def.description()+")",QByteArray());
                             for(const auto& d:list)combo->addItem(d.description(),d.id());
                             combo->setCurrentIndex(qMax(0,combo->findData(saved)));}
                            if(!saved.isEmpty() && combo->findData(saved)<0){
                                // The default output is temporary until the chosen device returns.
                                if(!input)continue;
                                saved.clear();QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).remove(key);
                                if(inCall && media)media->selectAudioDevice(input,{});
                                note(UiText::TheSelectedDeviceWasDisconnectedUsingThe());
                            }
                        }
                        if(auto* w=section->findChild<QWidget*>("audioCheck"))static_cast<AudioCheck*>(w)->devices(inputDevice,outputDevice);
                        refresh->setText(UiText::RefreshDeviceList());refresh->setEnabled(true);
                    });
                };
                auto* devices=new QMediaDevices(section);
                connect(refresh,&QPushButton::clicked,section,reload);
                connect(devices,&QMediaDevices::audioInputsChanged,section,reload);
                connect(devices,&QMediaDevices::audioOutputsChanged,section,reload);
            }
            else device(UiText::Camera(),QMediaDevices::videoInputs(),&cameraDevice,"camera");
            QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
            if(view=="video"){
                body->addSpacing(21);heading(UiText::Effects());body->addSpacing(3);auto* row=new QHBoxLayout;row->setSpacing(12);body->addLayout(row);
                addEffectCards(row,section);
                row->addStretch();
            }else{
                body->addSpacing(12);body->addWidget(text(UiText::MicrophoneProcessing(),"font-weight:500;font-size:15px"));body->addSpacing(6);body->addWidget(text(UiText::NoiseSuppression(),"font-size:12px;color:#b9b9b9"));
                auto* engines=Ui::select(UiText::NoiseSuppression());engines->setFixedHeight(38);
                engines->addItem("GTCRN","gtcrn");engines->addItem("RNNoise","rnnoise");engines->addItem("WebRTC","browser");engines->addItem(UiText::Off(),"off");
                engines->setCurrentIndex(qMax(0,engines->findData(prefs.value("noise",true).toBool()?prefs.value("engine","gtcrn"):QVariant("off"))));
                body->addWidget(engines);body->addSpacing(6);
                auto* check=new AudioCheck(inputDevice,outputDevice,inCall,[engines]{return engines->currentData().toString();});check->setObjectName("audioCheck");body->addWidget(check);
                for(auto* c:section->findChildren<QComboBox*>())connect(c,&QComboBox::currentIndexChanged,check,[this,check]{check->devices(inputDevice,outputDevice);});
                connect(engines,&QComboBox::currentIndexChanged,engines,[this,engines]{QSettings p(dataPath+"/desktop.ini",QSettings::IniFormat);QString engine=engines->currentData().toString();p.setValue("noise",engine!="off");if(engine!="off")p.setValue("engine",engine);if(media)media->setProcessing(engine!="off",p.value("echo",true).toBool(),p.value("agc",false).toBool(),engine);});
            }
        }else if(view=="keys"){renderKeys(body);}
        else if(view=="overlay"){renderOverlaySettings(body);}
        else if(view=="channels"){
            if(allowed("manage_server")){
                auto* identity=new WorkspaceIdentity(api,workspace,[this]{return selectAvatar(this,true);},[this]{refresh();});
                identity->setFeedback([this](QString issue){note(issue);});
                body->addWidget(identity);body->addSpacing(18);
            }
            if(!allowed("manage_server")&&!allowed("manage_channels"))body->addWidget(text(UiText::ChannelsAreManagedByTheOwnerAnd()));
            if(allowed("manage_channels"))renderAdmin(body,"channels");
            if(self["role"]=="owner"){body->addSpacing(14);renderAdmin(body,"files");}
        }else if(view=="roles"){
            const auto blockTitle=[body](const QString& caption){
                body->addWidget(text(caption,"font-size:14px;font-weight:500"));body->addSpacing(6);
            };
            blockTitle(UiText::ManageRoles());renderAdmin(body,"roles");
            if(allowed("manage_server")){
                body->addSpacing(26);blockTitle(UiText::Invitations());
                body->addWidget(new InvitationSettings(api,workspace["invitations"].toArray(),[this]{refresh();}));
            }
            body->addSpacing(26);
            blockTitle(UiText::ManageMembers());renderAdmin(body,"members");
        }
    }
    body->addStretch();layoutForWidth();fade(page);
    if(oldScroll>0)QTimer::singleShot(0,scroll,[scroll,oldScroll]{scroll->verticalScrollBar()->setValue(oldScroll);});
}

void DesktopWindow::memberMenu(QString id,QPoint pos){
    if(id==selfId || id.isEmpty())return;
    auto m=byId(workspace.value("members").toArray(),id);if(m.isEmpty())return;
    class MenuPanel final : public QFrame {
    public:
        explicit MenuPanel(QWidget* parent):QFrame(parent,Qt::Popup){}
        void paintEvent(QPaintEvent*) override{
            QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);painter.setBrush(QColor("#252525"));painter.drawRoundedRect(rect(),14,14);
        }
    };
    auto* panel=new MenuPanel(this);panel->setObjectName("participantMenu");panel->setAttribute(Qt::WA_TranslucentBackground);
    panel->setAttribute(Qt::WA_DeleteOnClose);panel->setFixedWidth(252);panel->setFocusPolicy(Qt::StrongFocus);
    panel->setAccessibleName(UiText::Member()+m["name"].toString());
    panel->setStyleSheet("QPushButton{background:transparent;border:0;border-radius:8px;min-height:36px;padding:0 8px;text-align:left;font-size:12px}"
                         "QPushButton:hover,QPushButton:focus{background:#353535}"
                         "QSlider{background:transparent;border:0;padding:0} QSlider::groove:horizontal{height:4px;background:#444;border-radius:2px}"
                         "QSlider::sub-page:horizontal{background:#66aaf0;border-radius:2px}"
                         "QSlider::handle:horizontal{margin:-6px 0;width:12px;background:#66aaf0;border-radius:8px;border:2px solid #252525}");
    auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(12,12,12,12);layout->setSpacing(0);
    layout->addWidget(text(m["name"].toString(),"font-size:12px;font-weight:500;padding:4px 8px 12px"));
    const bool editable=m["role"]!="owner" && (self["role"]=="owner" || m["blocked"].toBool() || !m["permissions"].toArray().contains("manage_roles"));
    bool voice=false;
    for(auto room:workspace["presence"].toObject())
        for(auto p:room.toObject()["people"].toArray())if(p.toObject()["identity"].toString().section('.',0,0)==id)voice=true;
    if(inCall && callOrigin==api.server.toString())
        for(auto p:liveRoster)if(p.toObject()["identity"].toString().section('.',0,0)==id)voice=true;
    if(voice && (!inCall || callOrigin==api.server.toString())){
        const QString key="volume/"+api.server.toString()+"/"+id;
        auto* label=text("","font-size:11px;padding:8px");layout->addWidget(label);
        auto* slider=new QSlider(Qt::Horizontal);slider->setFixedHeight(24);slider->setCursor(Qt::PointingHandCursor);slider->setAccessibleName(UiText::VoiceVolume());slider->setRange(0,40);slider->setPageStep(2);
        slider->setValue(qRound(QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).value(key,100).toInt()/5.));
        const auto changeLabel=[label](int step){label->setText(QString(UiText::VoiceVolume1()).arg(step*5));};
        changeLabel(slider->value());auto* range=new QHBoxLayout;range->setContentsMargins(8,0,8,8);range->addWidget(slider);layout->addLayout(range);
        connect(slider,&QSlider::valueChanged,panel,[this,id,key,changeLabel](int value){
            changeLabel(value);if(media && inCall && callOrigin==api.server.toString())media->setVolume(id,value*5);
            QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue(key,value*5);
        });
    }
    auto* error=text("","color:#efaaaa;font-size:12px");error->setWordWrap(true);error->hide();
    auto menuButton=[](QString caption,QString glyph,bool danger=false){
        auto* button=new QPushButton;
        button->setAccessibleName(caption);button->setCursor(Qt::PointingHandCursor);
        auto* row=new QHBoxLayout(button);row->setContentsMargins(8,0,8,0);row->setSpacing(10);
        auto color=QColor(danger?"#e3a5a5":"#eeeeee");
        auto* icon=new QLabel;icon->setPixmap(tintedIcon(glyph,color).pixmap(16,16));row->addWidget(icon);
        auto* label=new QLabel(caption);label->setStyleSheet("background:transparent;font-size:12px;color:"+color.name());
        row->addWidget(label,1);
        for(auto* child:{label,icon})child->setAttribute(Qt::WA_TransparentForMouseEvents);
        return button;
    };
    auto action=[this,panel,error,id,menuButton](QLayout* dest,QString caption,QString op,int seconds=0){
        static const QHash<QString,QString> icons={{"mute_mic","mic-off"},{"mute_camera","camera-off"},{"mute_screen","screen"},{"kick","exit"}};
        auto* button=seconds?new QPushButton(caption):menuButton(caption,icons.value(op,"chat"),op=="kick");dest->addWidget(button);
        connect(button,&QPushButton::clicked,panel,[this,panel,error,id,op,seconds]{
            for(auto* b:panel->findChildren<QPushButton*>())b->setEnabled(false);
            api.request("POST","/api/members/"+enc(id)+"/moderate",{{"action",op},{"seconds",seconds}},
                [this,ptr=QPointer<QFrame>(panel),error](QJsonObject,QString issue){
                    if(issue.isEmpty()){if(ptr)ptr->close();refresh();return;}
                    if(!ptr)return;
                    error->setText(issue);error->show();
                    for(auto* b:ptr->findChildren<QPushButton*>())b->setEnabled(true);
                    ptr->adjustSize();
                });
        });
    };
    const bool chat=editable&&allowed("manage_chat");
    if(chat){
        if(m["chatMutedUntil"].toInteger()>QDateTime::currentMSecsSinceEpoch())action(layout,UiText::AllowPosting(),"unmute");
        else{
            auto* select=menuButton(UiText::RestrictPosting(),"chat");select->setCheckable(true);layout->addWidget(select);
            auto* durations=new QWidget;auto* options=new QGridLayout(durations);options->setDefaultPositioning(2,Qt::Horizontal);
            options->setContentsMargins(8,5,8,5);options->setSpacing(5);
            action(options,UiText::Value15Minutes(),"mute",900);action(options,UiText::Value1Hour(),"mute",3600);
            action(options,UiText::Value24Hours(),"mute",86400);action(options,UiText::Value1Week(),"mute",604800);
            layout->addWidget(durations);durations->hide();
            connect(select,&QPushButton::toggled,panel,[panel,durations](bool on){durations->setVisible(on);panel->adjustSize();});
        }
    }
    if(voice&&editable&&allowed("kick_members")){
        action(layout,UiText::MuteMicrophone(),"mute_mic");
        action(layout,UiText::TurnOffCamera(),"mute_camera");
        action(layout,UiText::StopScreenSharing2(),"mute_screen");
        action(layout,UiText::DisconnectFromVoice(),"kick");
    }
    if(!voice&&!chat)layout->addWidget(text(UiText::NoActionsAvailable(),"font-size:12px;color:#a0a0a0;padding:8px"));
    layout->addWidget(error);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),panel);connect(escape,&QShortcut::activated,panel,&QWidget::close);
    panel->adjustSize();
    QRect bounds(mapToGlobal(QPoint()),size());
    panel->move(qBound(bounds.left()+8,pos.x(),bounds.right()-panel->width()-7),
                qMax(bounds.top()+8,qMin(pos.y(),bounds.bottom()-panel->height()-7)));
    panel->show();panel->setFocus(Qt::PopupFocusReason);
}

void DesktopWindow::updateMessageRows(){
    QPointer<QPlainTextEdit> oldEditor=qobject_cast<QPlainTextEdit*>(QApplication::focusWidget());
    const bool editing=oldEditor && oldEditor->objectName()=="editMessageText" && messages->isAncestorOf(oldEditor);
    const int cursor=editing?oldEditor->textCursor().position():0;
    const int anchor=editing?oldEditor->textCursor().anchor():0;
    auto* sb=messages->verticalScrollBar();
    const int old=sb->value();
    const bool bottom=messages->count()==0 || old==sb->maximum();
    auto* firstVisible=messages->itemAt(1,0);
    const QString firstId=firstVisible?firstVisible->data(Qt::UserRole).toJsonObject()["id"].toString():QString();
    const int firstY=firstVisible?messages->visualItemRect(firstVisible).top():0;
    QSet<QString> wanted;
    for(const auto entry:lastMessages)wanted.insert(entry.toObject()["id"].toString());
    for(int i=messages->count()-1;i>=0;--i){
        if(!wanted.contains(messages->item(i)->data(Qt::UserRole).toJsonObject()["id"].toString()))
            delete messages->takeItem(i);
    }
    QHash<QString,LinkCard*> previews;
    int index=0;
    QHash<QString,QPixmap> messageAvatars;
    const bool restricted=self["chatMutedUntil"].toDouble()>QDateTime::currentMSecsSinceEpoch();
    for(auto v:lastMessages){
        auto m=v.toObject();auto id=m.value("id").toString(),name=m.value("name").toString(),author=m.value("authorId").toString();
        auto* item=messages->item(index);
        if(!item || item->data(Qt::UserRole).toJsonObject()["id"]!=id){
            item=new QListWidgetItem;
            messages->insertItem(index,item);
        }
        ++index;
        auto reply=m["replyTo"].toString().isEmpty()?QJsonObject{}:Ui::byId(lastMessages,m["replyTo"].toString());
        if(reply.isEmpty())reply=m["reply"].toObject();
        const QJsonObject context{{"language",profile["language"]},{"reply",reply},{"blocked",self["blocked"]},{"roles",self["roleIds"]},{"role",self["role"]},
            {"self",selfId},{"write",compose->isEnabled()},{"restricted",restricted},{"manage",allowed("manage_chat")}};
        const bool editingRow=!id.isEmpty() && editOriginal["id"]==id;
        const int bottomPadding=index==lastMessages.size()?24:14;
        int selection=-1,length=0;bool restoreFocus=false;
        if(auto* row=messages->itemWidget(item)){
            if(item->data(Qt::UserRole).toJsonObject()==m && item->data(Qt::UserRole+1).toJsonObject()==context
               && !editingRow && !row->findChild<QWidget*>("editMessageText")){
                row->layout()->setContentsMargins(6,10,6,bottomPadding);
                auto* pic=row->findChild<QLabel*>("messageAvatar");
                if(pic && pic->property("sourceKey").toLongLong()!=avatars.value(author).cacheKey()){
                    pic->setPixmap(avatar(name,avatars.value(author),34));
                    pic->setProperty("sourceKey",avatars.value(author).cacheKey());
                }
                continue;
            }
            if(auto* label=row->findChild<QLabel*>("messageBody");label && label->hasSelectedText() && item->data(Qt::UserRole).toJsonObject()["text"]==m["text"]){
                selection=label->selectionStart();length=label->selectedText().size();restoreFocus=label->hasFocus();
            }
            if(auto* widget=row->findChild<QWidget*>("linkPreview")){
                auto* card=static_cast<LinkCard*>(widget);
                previews.insert(card->property("messageLink").toString(),card);
                card->detach(messages);
            }
            for(const auto& type:{QStringLiteral("audioAttachment"),QStringLiteral("videoAttachment")})
                for(auto* card:row->findChildren<QWidget*>(type)){card->hide();card->setParent(messages);}
        }
        item->setText(m.value("text").toString());item->setForeground(Qt::transparent);item->setData(Qt::UserRole,m);
        item->setData(Qt::UserRole+1,context);
        auto* row=new MessageRow;row->setAccessibleName(UiText::MessageFrom()+name);auto* h=new QHBoxLayout(row);
        h->setContentsMargins(6,10,6,bottomPadding);h->setSpacing(12);
        const auto avatarKey=author+QChar::Null+name;
        auto picture=messageAvatars.constFind(avatarKey);
        if(picture==messageAvatars.cend())picture=messageAvatars.insert(avatarKey,avatar(name,avatars.value(author),34));
        auto* pic=text("");pic->setObjectName("messageAvatar");pic->setProperty("sourceKey",avatars.value(author).cacheKey());pic->setPixmap(picture.value());h->addWidget(pic,0,Qt::AlignTop);
        auto* b=new QVBoxLayout;b->setSpacing(4);b->setAlignment(Qt::AlignTop);h->addLayout(b,1);auto* meta=new QHBoxLayout;meta->setContentsMargins(0,0,0,0);meta->addStrut(28);meta->setSpacing(12);meta->addWidget(text(name,"font-size:13px;font-weight:500"));meta->addWidget(text(UiText::locale().toString(QDateTime::fromMSecsSinceEpoch(m.value("createdAt").toInteger()),"d MMM, HH:mm")+(m.contains("editedAt")?UiText::Edited():""),"font-size:10px;color:#737373"));meta->addStretch();b->addLayout(meta);
        if(!m["deleted"].toBool() && !m["replyTo"].toString().isEmpty()){
            auto* quote=new QWidget;
            quote->setAttribute(Qt::WA_StyledBackground);
            quote->setObjectName("quotedMessage");
            quote->setStyleSheet("QWidget#quotedMessage{background:transparent;border-left:2px solid #657b74}");
            auto* lines=new QVBoxLayout(quote);
            lines->setContentsMargins(12,5,10,5);lines->setSpacing(0);
            const QString snippet=reply["deleted"].toBool()?QString::fromUtf8(UiText::MessageDeleted()):reply["text"].toString();
            lines->addWidget(text(reply["name"].toString(),"font-size:11px;font-weight:500;color:#aaa"));
            auto* preview=new ElidedQuote(snippet);
            lines->addWidget(preview);
            b->addWidget(quote);
        }
        const bool deleted=m.value("deleted").toBool();
        const QString body=deleted?QString::fromUtf8(UiText::MessageDeleted()):m["text"].toString();
        if(!deleted && editOriginal["id"]==id)renderMessageEdit(b);
        else if(!body.isEmpty()){
            auto* message=text(body,"color:#c8c8c8;font-size:13px");
            message->setObjectName("messageBody");message->setAccessibleName(body);
            message->setContentsMargins(0,0,10,0);message->setWordWrap(true);
            message->setTextInteractionFlags(Qt::TextBrowserInteraction);
            message->setTextFormat(Qt::RichText);
            message->setText(MessageText::body(body,deleted?QJsonArray():m["mentions"].toArray(),self,m["format"].toString()));
            if(selection>=0)message->setSelection(selection,length);
            connect(message,&QLabel::linkActivated,message,[](const QString& href){
                const QUrl url(href);
                if((url.scheme()=="http"||url.scheme()=="https") && !url.host().isEmpty() && url.userInfo().isEmpty())
                    QDesktopServices::openUrl(url);
            });
            b->addWidget(message);
            const auto link=LinkCard::firstLink(body);
            if(!deleted && !link.first.isEmpty()){
                const QString key=id+QChar::Null+link.first;
                auto* card=previews.take(key);
                if(!card)card=new LinkCard(api,selected,id,link,[this]{relayoutChat();});
                card->setProperty("messageLink",key);
                b->addSpacing(7);card->attach(b);
            }
        }
        auto files=m.value("attachments").toArray();if(files.isEmpty()&&m.value("attachment").isObject())files.append(m.value("attachment"));
        if(deleted)files={};
        QJsonArray visuals,others;
        for(auto file:files){
            const QString ext=QFileInfo(file.toObject()["name"].toString()).suffix().toLower();
            if(QStringList{"jpg","jpeg","png","gif","webp","mp4","webm","ogv"}.contains(ext))visuals.append(file);
            else others.append(file);
        }
        if(!visuals.isEmpty()){
            auto* gallery=new QWidget;auto* wrap=new WrapLayout(gallery);wrap->setSpacing(8);
            for(auto file:visuals){
                auto* cell=new QWidget;cell->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
                auto* inner=new QVBoxLayout(cell);inner->setContentsMargins(0,0,0,0);
                attachmentActions(inner,file.toObject(),visuals);wrap->addWidget(cell);
            }
            b->addSpacing(4);b->addWidget(gallery);
        }
        for(auto file:others){b->addSpacing(4);attachmentActions(b,file.toObject());}
        if(!files.isEmpty())b->addSpacing(8);
        if(!deleted){
            const auto reacts=m["reactions"].toObject();
            if(!reacts.isEmpty()){
                auto* chips=new QHBoxLayout;chips->setSpacing(6);
                for(auto it=reacts.begin();it!=reacts.end();++it){
                    const auto ids=it.value().toArray();
                    if(ids.isEmpty())continue;
                    auto* chip=new QPushButton(it.key()+"  "+QString::number(ids.size()));
                    const bool mine=ids.contains(selfId);
                    chip->setAccessibleName(it.key()+": "+QString::number(ids.size())+". "+(mine?UiText::Remove2():UiText::Add())+UiText::Reaction());
                    QStringList people;
                    for(auto user:ids)people<<byId(workspace["members"].toArray(),user.toString())["name"].toString(UiText::Member2());
                    chip->setToolTip(people.join("\n"));chip->setEnabled(!restricted);
                    chip->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:0;border-radius:10px;padding:5px 9px;font-size:12px} QPushButton:hover{background:#343434;color:white}")
                        .arg(mine?"#344746":"#242424",mine?"#d1f5ed":"#aaa"));
                    chips->addWidget(chip);
                    connect(chip,&QPushButton::clicked,this,[this,id,emoji=it.key()]{toggleReaction(id,emoji);});
                }
                chips->addStretch();b->addLayout(chips);
            }
            const bool writable=compose->isEnabled(), own=author==selfId, removable=own||allowed("manage_chat");
            row->createActions=[this,row,restricted,writable,own,removable,id,m,name]{
                if(!restricted){
                    auto* react=row->actionButton("reaction-solid",UiText::AddReaction(),UiText::AddReaction());
                    connect(react,&QPushButton::clicked,react,[this,id,react]{
                        showEmojiPicker(react,[this,id,gen=channelGeneration](QString emoji){if(gen==channelGeneration)toggleReaction(id,emoji);});
                    });
                    if(writable){
                        auto* reply=row->actionButton("reply-solid",UiText::ReplyToMessage(),UiText::Reply2());
                        connect(reply,&QPushButton::clicked,reply,[this,m]{showReply(m);compose->setFocus();});
                        if(own){
                            auto* edit=row->actionButton("pencil",UiText::EditMessage(),UiText::Edit3());
                            connect(edit,&QPushButton::clicked,edit,[this,m]{beginMessageEdit(m);});
                        }
                    }
                }
                if(removable){
                    auto* remove=row->actionButton("trash-solid",UiText::DeleteMessageFrom()+name,UiText::Delete3());
                    connect(remove,&QPushButton::clicked,remove,[this,id,remove]{
                        if(deleteBusy)return;
                        deleteTarget=id;confirmDelete->setEnabled(true);cancelDelete->setEnabled(true);
                        deleteBanner->adjustSize();
                        QPoint at=remove->mapTo(roomPage,QPoint(remove->width()-deleteBanner->width(),remove->height()+3));
                        at.setX(qBound(0,at.x(),qMax(0,roomPage->width()-deleteBanner->width())));
                        if(at.y()+deleteBanner->height()>roomPage->height())at.setY(at.y()-remove->height()-deleteBanner->height()-6);
                        deleteBanner->move(at);deleteBanner->show();deleteBanner->raise();confirmDelete->setFocus();
                    });
                }
            };
        }
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row,&QWidget::customContextMenuRequested,row,[this,row,author](QPoint p){memberMenu(author,row->mapToGlobal(p));});
        messages->setItemWidget(item,row);
        if(restoreFocus)if(auto* bodyLabel=row->findChild<QLabel*>("messageBody"))bodyLabel->setFocus();
        if(editing && editOriginal["id"]==id){
            if(auto* editor=row->findChild<QPlainTextEdit*>("editMessageText")){
                auto caret=editor->textCursor();caret.setPosition(qMin(anchor,int(editDraft.size())));
                caret.setPosition(qMin(cursor,int(editDraft.size())),QTextCursor::KeepAnchor);
                editor->setTextCursor(caret);editor->setFocus();
            }
        }
    }
    while(messages->count()>index)delete messages->takeItem(messages->count()-1);
    appendPendingRows();
    qDeleteAll(previews);
    for(const auto& type:{QStringLiteral("audioAttachment"),QStringLiteral("videoAttachment")})
        for(auto* unused:messages->findChildren<QWidget*>(type,Qt::FindDirectChildrenOnly))delete unused;
    if(!replyId.isEmpty()){
        const auto target=byId(lastMessages,replyId);
        if(target.isEmpty()){replyId.clear();replyBanner->hide();}
        else showReply(target);
    }
    sizeMessageRows();
    messages->doItemsLayout();
    if(bottom){messages->scrollToBottom();return;}
    for(int i=0;!firstId.isEmpty()&&i<messages->count();++i){
        auto* item=messages->item(i);
        if(item->data(Qt::UserRole).toJsonObject()["id"].toString()==firstId){
            sb->setValue(sb->value()+messages->visualItemRect(item).top()-firstY);return;
        }
    }
    sb->setValue(old);
}
