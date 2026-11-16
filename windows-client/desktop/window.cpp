#include "interface_text.h"
#include "window.h"
#include "desktop_updates.h"
#include "message_input.h"
#include "password_reveal.h"
#include "profile.h"
#include "share_picker.h"
#include "ui.h"
#include <QtWidgets>
#include <QAudioDevice>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QSoundEffect>
#include <QWindowCapture>
#include <QThread>
#include <memory>
#include <windows.h>
#include <dwmapi.h>

namespace {
void releaseUnusedHeapPages() {
    HEAP_OPTIMIZE_RESOURCES_INFORMATION options{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};
    HeapSetInformation(GetProcessHeap(),HeapOptimizeResources,&options,sizeof(options));
}
QPushButton* button(QString text, QString icon = {}) {
    auto* b = new QPushButton(text);
    if (!icon.isEmpty()) b->setIcon(QIcon(QCoreApplication::applicationDirPath()+"/assets/"+icon+".png"));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QLabel* label(QString text, QString style = {}) {
    auto* l = new QLabel(text); l->setTextFormat(Qt::PlainText);
    if (!style.isEmpty()) l->setStyleSheet(style);
    l->setWordWrap(true); return l;
}
QString encoded(QString value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
QByteArray readSmall(QString path,int limit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size()>limit) throw std::runtime_error("read failed");
    return file.read(limit+1);
}
bool writeFile(QString path,QByteArray data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data)==data.size() && file.commit();
}
}
DesktopWindow::DesktopWindow(QString directory,bool generated,QJsonObject restartView,bool legacyInstalledUpdate) : generatedMedia(generated),dataPath(std::move(directory)) {
    UiText::setLanguage({});
    configureMediaSync();
    updates=new DesktopUpdates(this,dataPath,[this]{return inCall||joining;});
    updates->restartView=[this]{return captureUpdateView();};
    api.activeProfileKey=[this]{
        for(auto value:profile["servers"].toArray()){
            const auto entry=value.toObject();
            if(DesktopProfile::origin(entry["url"].toString())==api.server)return entry["profileKey"].toString();
        }
        return QString();
    };
    const QDir assets(QCoreApplication::applicationDirPath()+"/assets");
    static const bool fontsLoaded=[&assets] {
        for(const auto weight:{400,500,700})QFontDatabase::addApplicationFont(assets.filePath(QString("Roboto-%1.ttf").arg(weight)));
        return true;
    }();
    Q_UNUSED(fontsLoaded);
    QApplication::setFont(QFont("Roboto",10));
    setWindowTitle("Nimbo"); resize(1150,780); setMinimumSize(730,600);
    setWindowIcon(QIcon(QCoreApplication::applicationDirPath()+"/assets/nimbo.ico"));
    setStyleSheet(QStringLiteral(
        "QWidget{background:#080808;color:#ededed;font-size:14px} QLabel{background:transparent}"
        "QPushButton,QLineEdit,QComboBox{background:#202020;border:1px solid #333;border-radius:10px;padding:9px 12px}"
        "QPushButton:hover{background:#303030} QPushButton:disabled{color:#777} QLineEdit:focus{border-color:#aaa}"
        "QPushButton#primary{background:#ededed;color:#151515;font-weight:500} QPushButton#primary:disabled{color:#777}"
        "QListWidget{background:#101010;border:0;padding:5px} QListWidget::item{padding:8px;border-radius:8px}"
        "QListWidget::item:selected{background:#303030;color:white} QWidget#sidebar{background:#101010}"
        "QMenu{background:#202020} QMenu::item:selected{background:#444}"));
    pages = new QStackedWidget; setCentralWidget(pages);
    QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);inputDevice=prefs.value("input").toByteArray();outputDevice=prefs.value("output").toByteArray();cameraDevice=prefs.value("camera").toByteArray();buildLogin(); buildWorkspace(); chromeTexts=authoredChrome(pages); loadLocal(); showLogin();if(!profile["name"].toString().isEmpty())openLocalHome();
    qApp->installEventFilter(this);
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget* old,QWidget* current){
        if((old&&old->window()==this)||(current&&current->window()==this))bindMute();
    });
    connect(pages,&QStackedWidget::currentChanged,this,[this]{
        if(localLayout && pages->currentWidget()!=localHome)Ui::clear(localLayout);
    });
    idleMediaTimer.setSingleShot(true);
    // Let the queued disconnect notifications finish before destroying the session.
    idleMediaTimer.setInterval(0);
    connect(&idleMediaTimer,&QTimer::timeout,this,[this]{
        if(!inCall && !joining && !leavingVoice && pendingVoiceRoom.isEmpty()){
            media.reset();
            releaseUnusedHeapPages();
        }
    });
    connect(&timer,&QTimer::timeout,this,&DesktopWindow::refresh);
    mentionClock.start();mentionTimer.setInterval(175);
    connect(&mentionTimer,&QTimer::timeout,this,&DesktopWindow::acknowledgeVisibleMentions);
    if(legacyInstalledUpdate)
        restartView={{"profile",localId},{"server",prefs.value("lastServer").toString()},{"view","general"},{"bottom",true}};
    restoreUpdateServer(restartView);
}

QJsonObject DesktopWindow::captureUpdateView() const {
    if(pages->currentIndex()!=1 || api.token.isEmpty() || localId.isEmpty())return {};
    QJsonObject route{{"profile",localId},{"server",api.server.toString()},{"channel",selected},{"view",view}};
    if(detailLayout->count()){
        auto* current=detailLayout->itemAt(0)->widget();
        if(auto* scroll=current->findChild<QScrollArea*>("settingsScroll")){
            auto* bar=scroll->verticalScrollBar();
            route["scroll"]=bar->value();
            route["bottom"]=bar->maximum()>0 && bar->value()==bar->maximum();
        }
    }
    return route;
}

void DesktopWindow::restoreUpdateServer(const QJsonObject& route){
    if(localId.isEmpty() || localId!=route["profile"].toString())return;
    for(const auto value:profile["servers"].toArray()){
        const auto entry=value.toObject();
        const auto origin=DesktopProfile::origin(entry["url"].toString());
        if(origin.isEmpty() || origin.toString()!=route["server"].toString())continue;
        pendingUpdateView=route;
        switchServer(entry);
        return;
    }
}
// Register only static interface labels, before any profile or server data is loaded.
QList<DesktopWindow::ChromeText> DesktopWindow::authoredChrome(QWidget* root) {
    QList<ChromeText> texts;
    for (auto* widget : root->findChildren<QWidget*>()) {
        for (const auto* property : {"text", "toolTip", "placeholderText", "accessibleName"}) {
            if (qstrcmp(property, "text") == 0 && !qobject_cast<QLabel*>(widget) && !qobject_cast<QAbstractButton*>(widget)) continue;
            const auto raw = widget->property(property).toString();
            if (raw.isEmpty()) continue;
            const bool paragraph = raw.startsWith("<p style=") && raw.endsWith("</p>");
            const QString text = paragraph ? QTextDocumentFragment::fromHtml(raw).toPlainText() : raw;
            const auto id = UiText::identify(text);
            if (id != UiText::Id::Count) texts.append({widget, property, id, paragraph});
        }
    }
    return texts;
}

void DesktopWindow::translateChrome(const QList<ChromeText>& texts) {
    for (const auto& item : texts) {
        if (!item.widget) continue;
        QString value = QString::fromUtf8(UiText::value(item.id));
        if (item.paragraph) {
            const auto html = item.widget->property(item.property).toString();
            value = html.left(html.indexOf('>') + 1) + value.toHtmlEscaped() + "</p>";
        }
        item.widget->setProperty(item.property, value);
    }
}

void DesktopWindow::applyProfileLanguage() {
    if (!UiText::setLanguage(profile["language"].toString())) return;
    translateChrome(chromeTexts);
    sendQuality->setItemText(0,UiText::Auto());
    refreshUpdateBadge();
    translateSidebar();
    workspaceLanguageDirty=true;
    stageSignature.clear();
}

DesktopWindow::~DesktopWindow() { qApp->removeEventFilter(this);disconnect(qApp,nullptr,this,nullptr);removeKeys(); }
void DesktopWindow::showEvent(QShowEvent* event){
    QMainWindow::showEvent(event);
    styleCaption();
}
void DesktopWindow::styleCaption(){
    const auto handle=reinterpret_cast<HWND>(internalWinId());
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if(!handle || !SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0))return;
    const bool system=contrast.dwFlags&HCF_HIGHCONTRASTON;
    const COLORREF caption=system?DWMWA_COLOR_DEFAULT:RGB(16,16,16);
    const COLORREF textColor=system?DWMWA_COLOR_DEFAULT:RGB(237,237,237);
    const COLORREF border=system?DWMWA_COLOR_DEFAULT:RGB(36,36,36);
    DwmSetWindowAttribute(handle,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(handle,DWMWA_TEXT_COLOR,&textColor,sizeof(textColor));
    DwmSetWindowAttribute(handle,DWMWA_BORDER_COLOR,&border,sizeof(border));
}
void DesktopWindow::closeEvent(QCloseEvent* event) {
    if(forceClose || (!joining && !inCall && !pendingVoiceDepartures && !leavingVoice)){
        QMainWindow::closeEvent(event);
        return;
    }
    event->ignore();
    if(closingWindow)return;
    closingWindow=true;
    setEnabled(false);
    timer.stop();
    QTimer::singleShot(2500,this,[this]{forceClose=true;close();});
    leaveCall();
    finishVoiceClose();
}
void DesktopWindow::finishVoiceClose(){
    if(closingWindow && !pendingVoiceDepartures && !joining && !inCall && !leavingVoice)
        QTimer::singleShot(0,this,[this]{close();});
}
void DesktopWindow::note(QString text) {
    auto* output=pages->currentWidget()==localHome ? localStatus : pages->currentIndex()==0 ? loginStatus : status;
    output->setText(text);
    output->setVisible(!text.isEmpty());
    if(output==status){
        noticeTimer.stop();
        if(!text.isEmpty()){
            positionNotice();status->raise();Ui::fade(status,180);noticeTimer.start(10000);
        }
    }
}
void DesktopWindow::syncUpdateContext(){
    const QString origin=self["role"]=="owner"?api.server.toString():QString();
    if(updateProfile!=localId || updateServer!=origin){
        updateProfile=localId;
        updateServer=origin;
        serverOffer.clear();
        serverOfferChecked.invalidate();
        QSettings seen(dataPath+"/desktop.ini",QSettings::IniFormat);
        seen.beginGroup("readUpdates/"+localId);
        seenDesktopOffer=seen.value("desktop").toString();
        seenServerOffer=seen.value("server-"+QString::fromLatin1(QUrl::toPercentEncoding(origin))).toString();
        refreshUpdateBadge();
    }
    if(origin.isEmpty() || (serverOfferChecked.isValid() && serverOfferChecked.elapsed()<300000))return;
    serverOfferChecked.start();
    if(view=="general" && isVisible() && findChild<QWidget*>("serverUpdates"))return;
    api.request("GET","/api/server-update",{},[this,origin,id=localId](QJsonObject data,QString error){
        if(error.isEmpty() && origin==updateServer && id==localId && view!="general")recordServerOffer(data);
    });
}

void DesktopWindow::recordServerOffer(const QJsonObject& state){
    if(updateServer.isEmpty() || state.isEmpty())return;
    const auto phase=state["phase"].toString();
    const auto tag=state["available"].toString();
    const auto installed=state["installed"].toString();
    if(!tag.isEmpty() && tag!=installed && (phase=="available" || state["canInstall"].toBool()))serverOffer=tag;
    else if(phase=="current" || phase=="unmanaged" || phase=="empty" || (!serverOffer.isEmpty() && serverOffer==installed))serverOffer.clear();
    refreshUpdateBadge();
}

void DesktopWindow::refreshUpdateBadge(){
    if(!settingsUpdateDot)return;
    const QString desktop=updates->offeredVersion();
    const bool unread=(!desktop.isEmpty() && seenDesktopOffer!=desktop) || (!serverOffer.isEmpty() && seenServerOffer!=serverOffer);
    settingsUpdateDot->setVisible(unread);
    settingsButton->setAccessibleDescription(unread?UiText::AnUpdateIsAvailable():"");
}

void DesktopWindow::markOffersRead(const QString& desktopTag,const QString& serverTag){
    if(localId.isEmpty() || (desktopTag.isEmpty() && serverTag.isEmpty()))return;
    QSettings seen(dataPath+"/desktop.ini",QSettings::IniFormat);
    seen.beginGroup("readUpdates/"+localId);
    if(!desktopTag.isEmpty() && desktopTag==updates->offeredVersion() && desktopTag!=seenDesktopOffer){
        seenDesktopOffer=desktopTag;
        seen.setValue("desktop",desktopTag);
    }
    if(!serverTag.isEmpty() && serverTag==serverOffer && serverTag!=seenServerOffer){
        seenServerOffer=serverTag;
        seen.setValue("server-"+QString::fromLatin1(QUrl::toPercentEncoding(updateServer)),serverTag);
    }
    refreshUpdateBadge();
}

void DesktopWindow::positionNotice(){
    const int maxWidth=qMax(1,qMin(360,content->width()-16));
    const int w=qBound(1,status->fontMetrics().horizontalAdvance(status->text())+24,maxWidth);
    status->resize(w,qMax(40,status->heightForWidth(w)));
    const bool footer=dock->isVisible() && dock->parentWidget()==roomPage;
    const int y=footer?dock->mapTo(content,QPoint()).y()-status->height()-6:0;
    status->move(qMax(0,content->width()-w),qMax(0,y));
}
void DesktopWindow::buildLogin() {
    auto* page = new QWidget; auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24,90,24,90);outer->setSpacing(0);outer->setSizeConstraint(QLayout::SetMinimumSize);
    auto* brand=label("nimbo.","font-weight:700;font-size:36px");brand->setTextFormat(Qt::RichText);brand->setText("nimbo<span style='color:#888'>.</span>");brand->setParent(page);brand->move(40,36);auto brandFont=brand->font();brandFont.setLetterSpacing(QFont::AbsoluteSpacing,-2);brand->setFont(brandFont);brand->adjustSize();
    outer->addStretch();
    auto* form = new QWidget; form->setFixedWidth(400);
    auto* f = new QVBoxLayout(form); f->setContentsMargins(0,0,0,0); f->setSpacing(0);form->setStyleSheet("QLineEdit{background:#202020;color:#b9b9b9;placeholder-text-color:#727272;border:1px solid transparent;padding:0 14px;border-radius:10px;font-size:12px} QLineEdit:focus{background:#292d2c} QPushButton#importProfile{background:#e6e6e6;border:0;color:#111;font-size:13px} QLabel{font-size:12px}");
    auto* symbol = new QLabel; symbol->setPixmap(QIcon(QCoreApplication::applicationDirPath()+"/assets/sound.png").pixmap(26,26));
    auto formLabel=[f](QString value,QString style,int height){
        auto* l=label(value,style);l->setFixedHeight(height);l->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);f->addWidget(l);return l;
    };
    symbol->setFixedSize(48,48);symbol->setAlignment(Qt::AlignCenter);symbol->setStyleSheet("background:#202020;border-radius:15px");f->addWidget(symbol);
    f->addSpacing(20);auto* heading=formLabel(UiText::SignInToNimbo(),"font-size:30px;font-weight:500",37);
    auto headingFont=heading->font();headingFont.setLetterSpacing(QFont::AbsoluteSpacing,-.8);heading->setFont(headingFont);
    f->addSpacing(13);auto* intro=formLabel(UiText::SignInWithYourNimboFileTo(),"color:#a0a0a0;font-size:13px",47);
    intro->setTextFormat(Qt::RichText);intro->setText("<p style='margin:0;line-height:180%'>"+intro->text().toHtmlEscaped()+"</p>");
    importButton=button(UiText::SignInWithANimboFile());importButton->setFixedHeight(41);importButton->setObjectName("importProfile");f->addWidget(importButton);
    loginImportBox=new QWidget;
    auto* importLayout=new QVBoxLayout(loginImportBox);
    importLayout->setContentsMargins(0,20,0,0);
    auto* card=new QFrame;card->setObjectName("loginProtectedProfile");
    card->setStyleSheet("QFrame#loginProtectedProfile{background:#1b1b1b;border-radius:13px} QPushButton{border:0}");
    auto* inputs=new QVBoxLayout(card);inputs->setContentsMargins(22,22,22,22);inputs->setSpacing(0);
    inputs->addWidget(label(UiText::ProtectedProfile(),"font-size:14px;font-weight:500"));
    auto* passwordLabel=label(UiText::FilePassword());
    inputs->addSpacing(20);inputs->addWidget(passwordLabel);inputs->addSpacing(9);
    filePassword=new QLineEdit;filePassword->setEchoMode(QLineEdit::Password);filePassword->setMaxLength(256);
    filePassword->setFixedHeight(42);passwordLabel->setBuddy(filePassword);filePassword->setAccessibleName(UiText::FilePassword());new PasswordReveal(filePassword);
    inputs->addWidget(filePassword);
    inputs->addSpacing(20);
    auto* buttons=new QHBoxLayout;buttons->setSpacing(0);inputs->addLayout(buttons);
    unlockFile=button(UiText::OpenProfile());unlockFile->setFixedHeight(38);buttons->addWidget(unlockFile);
    auto* cancel=button(UiText::Cancel());cancel->setStyleSheet("background:transparent;color:#aaa;border:0;padding:0 10px;font-size:12px");
    buttons->addWidget(cancel);buttons->addStretch();
    importLayout->addWidget(card);f->addWidget(loginImportBox);loginImportBox->hide();
    connect(filePassword,&QLineEdit::textChanged,unlockFile,[this](const QString& value){unlockFile->setEnabled(!value.isEmpty());});
    connect(cancel,&QPushButton::clicked,this,&DesktopWindow::resetLoginImport);
    connect(unlockFile,&QPushButton::clicked,this,&DesktopWindow::unlockLoginProfile);
    connect(filePassword,&QLineEdit::returnPressed,unlockFile,&QPushButton::click);
    loginStatus=label({},"color:#999;font-size:11px;padding-top:16px");loginStatus->setMinimumHeight(35);f->addWidget(loginStatus);
    f->addSpacing(22);auto* divider=formLabel(UiText::OrSignInWithAnInvitationCode(),"color:#777;font-size:11px",18);divider->setAlignment(Qt::AlignCenter);f->addSpacing(20);
    auto* group=new QWidget;auto* savedLayout=new QVBoxLayout(group);
    savedLayout->setContentsMargins(0,0,0,20);savedLayout->setSpacing(8);
    loginProfilesLabel=label(UiText::SavedProfile(),"font-size:12px;color:#b9b9b9");loginProfilesLabel->setFixedHeight(17);
    loginProfiles=Ui::select(UiText::SavedProfile());
    savedLayout->addWidget(loginProfilesLabel);savedLayout->addWidget(loginProfiles);f->addWidget(group);
    address=new QLineEdit(QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).value("lastServer","https://nimbo-voice.com:7443").toString(),page);
    address->setObjectName("serverAddress");address->hide();
    // The browser obtains its origin from the address bar. Keep a native equivalent in the logo menu.
    brand->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(brand,&QWidget::customContextMenuRequested,brand,[this,brand](QPoint pos){
        QMenu menu;auto* change=menu.addAction(UiText::ServerAddress());
        if(menu.exec(brand->mapToGlobal(pos))!=change)return;
        bool ok;auto value=QInputDialog::getText(this,UiText::ServerAddress(),UiText::ServerAddress(),QLineEdit::Normal,address->text(),&ok);
        if(!ok)return;
        auto origin=DesktopProfile::origin(value);
        if(origin.isEmpty()){note(UiText::EnterTheServerAddress());return;}address->setText(origin.toString());
    });
    nickname=new QLineEdit;nickname->setObjectName("nickname");nickname->setMaxLength(40);nickname->setPlaceholderText(UiText::WhatShouldWeCallYou());
    password=new QLineEdit;password->setObjectName("accessCode");password->setEchoMode(QLineEdit::Password);password->setMaxLength(256);password->setPlaceholderText(UiText::CodeFromTheOrganizer());
    for(auto* field:{nickname,password})field->setFixedHeight(42);
    new PasswordReveal(password);
    formLabel(UiText::YourName(),"font-size:12px;color:#b9b9b9",17);f->addSpacing(8);f->addWidget(nickname);f->addSpacing(18);
    formLabel(UiText::AccessCode(),"font-size:12px;color:#b9b9b9",17);f->addSpacing(8);f->addWidget(password);f->addSpacing(30);
    loginButton=button(UiText::SignInToNimbo());loginButton->setLayoutDirection(Qt::RightToLeft);
    auto arrow=Ui::icon("arrow").pixmap(18,18);{QPainter painter(&arrow);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(arrow.rect(),QColor("#111"));}
    loginButton->setIcon(QIcon(arrow));loginButton->setIconSize({18,18});loginButton->setFixedHeight(48);loginButton->setObjectName("primary");
    loginButton->setStyleSheet("QPushButton#primary{background:#e4e4e4;border:0;border-radius:12px;color:#111;font-size:14px;font-weight:500} QPushButton#primary:hover{background:white}");
    f->addWidget(loginButton);f->addSpacing(14);
    auto* hint=formLabel(UiText::YourMicrophoneTurnsOnWhenYouJoin(),"color:#a0a0a0;font-size:11px",37);
    hint->setTextFormat(Qt::RichText);hint->setText("<p style='margin:0;line-height:170%'>"+hint->text().toHtmlEscaped()+"</p>");f->addSpacing(14);
    outer->addWidget(form,0,Qt::AlignHCenter);outer->addStretch();
    auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollBar:vertical{background:#101010;width:6px} QScrollBar::handle:vertical{background:#333;min-height:20px} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}");
    scroll->setWidget(page);pages->addWidget(scroll);
    connect(loginButton,&QPushButton::clicked,this,[this]{ signIn(false); });
    connect(password,&QLineEdit::returnPressed,loginButton,&QPushButton::click);
    connect(importButton,&QPushButton::clicked,this,&DesktopWindow::importProfile);
    connect(loginProfiles,&QComboBox::activated,this,[this](int index){
        auto entry=Ui::byId(localProfiles,loginProfiles->itemData(index).toString());
        if(entry.isEmpty()||!storeLibrary(localProfiles,entry["id"].toString()))return;
        profile=entry["profile"].toObject();nickname->setText(profile["name"].toString());applyProfileLanguage();restoreCameraEffect();
    });
}
void DesktopWindow::showLogin() {
    if(attachmentViewer){pages->removeWidget(attachmentViewer);attachmentViewer->deleteLater();attachmentViewer=nullptr;}
    workspaceReceived.invalidate();
    if(switchNotice)finishServerSwitch();
    Ui::clear(detailLayout);
    clearPreparedServers();resetLoginImport();timer.stop(); removeKeys(); api.reset(); media.reset(); setCall(false);
    rooms = {}; selected.clear(); kind.clear(); drafts.clear();draftFormats.clear(); sendSignature.clear();
    workspaceLatency=0;workspaceFailed=false;workspace={};updateNetworkIndicator();self={};selfId.clear();syncUpdateContext();thumbnails.clear();imageFiles.clear();imageRequests.clear();fileDrafts.clear();mentionDrafts.clear();avatars.clear();avatarVersions.clear();stageSignature.clear();lastMessages={};view="room";content->setCurrentWidget(roomPage);
    clearMessageActions();outbox.clear();outboxOrder.clear();++channelGeneration; refreshing = false; loadingMessages = false; sending = false;
    channels->clear(); members->clear(); clearChatHistory(); compose->clear(); password->clear();
    sendButton->setEnabled(true);
    pages->setCurrentIndex(0); loginStatus->clear();loginStatus->hide(); loginProfiles->clear();for(auto v:localProfiles){auto e=v.toObject();loginProfiles->addItem(e["profile"].toObject()["name"].toString(),e["id"].toString());}
    loginProfiles->setCurrentIndex(qMax(0,loginProfiles->findData(localId)));loginProfiles->parentWidget()->setVisible(!localProfiles.isEmpty());
}
void DesktopWindow::loadLocal() {
    try {
        const auto library=dataPath+"/profiles.dpapi";
        if(QFile::exists(library)){
            auto saved=QJsonDocument::fromJson(DesktopProfile::unprotect(readSmall(library,5000000))).object();
            if(!saved["entries"].isArray())throw std::runtime_error("invalid library");
            localId=saved["active"].toString();
            for(auto v:saved["entries"].toArray()){
                auto entry=v.toObject();
                auto clean=DesktopProfile::validate(QJsonDocument(entry["profile"].toObject()).toJson(QJsonDocument::Compact));
                entry["profile"]=clean;localProfiles.append(entry);if(entry["id"]==localId)profile=clean;
            }
        }else if(QFile::exists(dataPath+"/profile.dpapi")){
            profile=DesktopProfile::validate(DesktopProfile::unprotect(readSmall(dataPath+"/profile.dpapi",310000)));
            saveLocal();
        }
        nickname->setText(profile["name"].toString());applyProfileLanguage();restoreCameraEffect();
    }catch(...){QMessageBox::warning(this,"Nimbo",UiText::CouldNotOpenTheSavedProfileRestore());}
}
bool DesktopWindow::storeLibrary(QJsonArray entries,QString active){
    try{
        QJsonObject state{{"entries",entries},{"active",active}};
        if(!QDir().mkpath(dataPath)||!writeFile(dataPath+"/profiles.dpapi",DesktopProfile::protect(QJsonDocument(state).toJson(QJsonDocument::Compact))))throw std::runtime_error("save");
        localProfiles=entries;localId=active;
        return true;
    }catch(...){note(UiText::CouldNotSaveTheProfileLibrary());return false;}
}
void DesktopWindow::saveLocal(){
    auto entries=localProfiles;
    QString id=localId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):localId;
    QJsonObject entry{{"id",id},{"profile",profile}};
    int i=0;while(i<entries.size()&&entries[i].toObject()["id"]!=id)++i;
    if(i<entries.size())entries[i]=entry;else if(i<16)entries.append(entry);else{note(UiText::YouCanStoreUpTo16Profiles());return;}
    if(storeLibrary(entries,id)){
        try{if(!writeFile(dataPath+"/profile.dpapi",DesktopProfile::protect(QJsonDocument(profile).toJson(QJsonDocument::Compact))))throw std::runtime_error("save");}
        catch(...){note(UiText::CouldNotUpdateTheBackupProfileFile());}
    }
}
void DesktopWindow::removeLocalProfile(const QString& id){
    bool current=(id==localId);auto entries=localProfiles;
    for(int i=0;i<entries.size();++i)if(entries[i].toObject()["id"]==id){entries.removeAt(i);break;}
    if(!storeLibrary(entries,current?QString():localId))return;
    if(current){profile={};applyProfileLanguage();QFile::remove(dataPath+"/profile.dpapi");showLogin();}else renderView();
}
void DesktopWindow::resetLoginImport(){
    ++importGeneration;pendingLoginFile.clear();filePassword->clear();
    loginImportBox->hide();loginImportBox->setEnabled(true);
    importButton->setEnabled(true);unlockFile->setEnabled(false);unlockFile->setText(UiText::OpenProfile());
}
void DesktopWindow::loadLoginFile(const QByteArray& bytes){
    resetLoginImport();note({});
    try{
        if(QJsonDocument::fromJson(bytes).object()["format"]=="nimbo-encrypted-profile"){
            DesktopProfile::inspectEncrypted(bytes);pendingLoginFile=bytes;loginImportBox->show();return;
        }
        acceptLoginProfile(DesktopProfile::validate(bytes));
    }catch(...){note(UiText::CouldNotReadTheProfileFile());}
}
void DesktopWindow::importProfile(){
    const QString path=QFileDialog::getOpenFileName(this,UiText::SignInWithANimboFile(),{},"Nimbo (*.nimbo *.json)");
    if(path.isEmpty())return;
    try{loadLoginFile(readSmall(path,300000));}
    catch(...){resetLoginImport();note(UiText::CouldNotReadTheProfileFile());}
}
void DesktopWindow::unlockLoginProfile(){
    if(!unlockFile->isEnabled() || pendingLoginFile.isEmpty() || !loginImportBox->isEnabled())return;
    const auto generation=importGeneration;
    loginImportBox->setEnabled(false);importButton->setEnabled(false);unlockFile->setText(UiText::Decrypting());note({});
    auto result=std::make_shared<QJsonObject>();
    auto* worker=QThread::create([result,bytes=pendingLoginFile,pass=filePassword->text()]{
        try{*result=DesktopProfile::open(bytes,pass);}catch(const std::exception&){}
    });
    connect(worker,&QThread::finished,this,[this,generation,result]{
        if(generation!=importGeneration)return;
        if(result->isEmpty()){
            loginImportBox->setEnabled(true);importButton->setEnabled(true);unlockFile->setText(UiText::OpenProfile());
            note(UiText::WrongPasswordOrDamagedFile());return;
        }
        resetLoginImport();acceptLoginProfile(*result);
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    worker->start();
}
void DesktopWindow::acceptLoginProfile(const QJsonObject& incoming){
    bool merged=false;
    const auto id=persistImport(incoming,true,merged);
    if(id.isEmpty())return;
    profile=Ui::byId(localProfiles,id)["profile"].toObject();
    nickname->setText(profile["name"].toString());applyProfileLanguage();restoreCameraEffect();saveLocal();
    const auto origin=DesktopProfile::origin(address->text());
    for(auto item:profile["servers"].toArray()){
        if(DesktopProfile::origin(item.toObject()["url"].toString())==origin){
            nickname->setText(item.toObject()["displayName"].toString(profile["name"].toString()));signIn(true,true);return;
        }
    }
    openLocalHome();note(UiText::ThisIsALocalProfileYouNeed());
}
void DesktopWindow::restoreCameraEffect(){
    auto fx=profile.value("cameraEffects").toObject();
    QSettings p(dataPath+"/desktop.ini",QSettings::IniFormat);p.setValue("blur",fx["enabled"].toBool());p.setValue("blurRadius",qBound(1,fx["strength"].toInt(12),60));
}
void DesktopWindow::updateCameraEffect(bool on,int strength){
    QSettings p(dataPath+"/desktop.ini",QSettings::IniFormat);p.setValue("blur",on);p.setValue("blurRadius",strength);
    profile["cameraEffects"]=QJsonObject{{"enabled",on},{"strength",strength}};saveLocal();if(media)media->setBlur(on,strength);
}
void DesktopWindow::addToLibrary(QJsonObject next){
    bool merged=false;
    const QString id=persistImport(next,false,merged);
    if(id.isEmpty())return;
    if(id==localId){profile=Ui::byId(localProfiles,id)["profile"].toObject();applyProfileLanguage();saveLocal();}
    renderView();note(merged?UiText::ProfileUpdatedNewServersWereAddedAnd():UiText::ProfileImportedYouCanSwitchToIt());
}
QString DesktopWindow::persistImport(QJsonObject next,bool activate,bool& merged){
    if(next.isEmpty())return {};
    auto entries=localProfiles;
    for(int i=0;i<entries.size();++i){
        auto entry=entries[i].toObject(),saved=entry["profile"].toObject();auto servers=saved["servers"].toArray();
        bool matches=saved==next;
        for(auto incoming:next["servers"].toArray())for(auto old:servers)
            if(old.toObject()["url"]==incoming.toObject()["url"]&&old.toObject()["profileKey"]==incoming.toObject()["profileKey"])matches=true;
        if(!matches)continue;
        for(auto incoming:next["servers"].toArray()){
            bool found=false;for(auto old:servers)if(old.toObject()["url"]==incoming.toObject()["url"])found=true;
            if(!found)servers.append(incoming);
        }
        if(servers.size()>64){note(UiText::YouCanSaveUpTo64Servers());return {};}
        const auto language=next["language"].toString();
        if(language=="ru" || language=="en" || language=="system")saved["language"]=language;
        saved["servers"]=servers;entry["profile"]=saved;entries[i]=entry;merged=true;
        const QString id=entry["id"].toString();
        return storeLibrary(entries,activate?id:localId)?id:QString();
    }
    if(entries.size()>=16){note(UiText::YouCanStoreUpTo16Profiles());return {};}
    const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    entries.append(QJsonObject{{"id",id},{"profile",next}});
    return storeLibrary(entries,activate?id:localId)?id:QString();
}
void DesktopWindow::chooseProfile(const QString& id,const QString& code,std::function<void()> rejected){
    auto entries=localProfiles;
    int index=0;while(index<entries.size() && entries[index].toObject()["id"]!=id)++index;
    if(index==entries.size() || id==localId || !pages->isEnabled())return;
    auto entry=entries[index].toObject();
    auto next=entry["profile"].toObject();
    auto servers=next["servers"].toArray();
    const QUrl origin=api.token.isEmpty()?DesktopProfile::origin(address->text()):api.server;
    if(origin.isEmpty()){note(UiText::EnterTheServerAddress());rejected();return;}
    int serverIndex=0;
    while(serverIndex<servers.size() && DesktopProfile::origin(servers[serverIndex].toObject()["url"].toString())!=origin)++serverIndex;
    if(serverIndex==servers.size() && servers.size()>=64){note(UiText::YouCanSaveUpTo64Servers());rejected();return;}
    auto target=serverIndex<servers.size()?servers[serverIndex].toObject():QJsonObject{};
    const QString key=target["profileKey"].toString();
    if(key.isEmpty() && code.isEmpty()){
        note(UiText::AnInvitationCodeIsRequiredForThe());rejected();return;
    }
    QJsonObject body;
    if(!key.isEmpty())body["profileKey"]=key;
    if(!code.isEmpty()){body["password"]=code;body["name"]=target["displayName"].toString(next["name"].toString());}
    auto* probe=new ServerApi(this);probe->server=origin;
    pages->setEnabled(false);note({});
    probe->request("POST",code.isEmpty()?"/api/profile-session":"/api/auth",body,
        [=,this](QJsonObject session,QString error)mutable{
        probe->deleteLater();pages->setEnabled(true);
        if(!error.isEmpty()||session["session"].toString().isEmpty()){
            note(error.isEmpty()?UiText::CouldNotSwitchProfiles():error);rejected();return;
        }
        const auto member=session["member"].toObject();
        target["url"]=origin.toString();target["profileKey"]=session["profileKey"].toString(key);
        if(target["profileKey"].toString().isEmpty()){note(UiText::TheServerDidNotReturnAProfile());rejected();return;}
        target["displayName"]=member["name"].toString(next["name"].toString());target["role"]=member["role"];
        if(!target.contains("name"))target["name"]=workspace["name"].toString(origin.host());
        if(serverIndex==servers.size())servers.append(target);else servers[serverIndex]=target;
        next["servers"]=servers;entry["profile"]=next;entries[index]=entry;
        if(!storeLibrary(entries,id)){rejected();return;}
        showLogin();profile=next;nickname->setText(target["displayName"].toString());applyProfileLanguage();restoreCameraEffect();
        api.server=origin;api.token=session["session"].toString();saveLocal();
        pages->setCurrentIndex(1);bindMute();openView("profile");refresh();timer.start(3000);
    });
}
bool DesktopWindow::exportProfile(const QString& pass) {
    auto path=QFileDialog::getSaveFileName(this,UiText::DownloadProfile(),"profile.nimbo","Nimbo (*.nimbo)"); if(path.isEmpty())return false;
    try { if(!writeFile(path,DesktopProfile::seal(profile,pass))) throw std::runtime_error("save"); note(UiText::ProtectedNimboFileDownloaded());return true; }
    catch(...){note(UiText::CouldNotSaveTheProfileCheckThe());return false;}
}
void DesktopWindow::signIn(bool fromProfile,bool imported) {
    if(!loginButton->isEnabled())return;
    auto url=DesktopProfile::origin(address->text());
    auto name=nickname->text().trimmed();
    if(url.isEmpty()||name.isEmpty()||(!fromProfile&&password->text().isEmpty())){note(UiText::EnterTheServerAddressYourNameAnd());return;}
    QJsonArray servers=profile.value("servers").toArray(); QString key;
    for(auto item:servers) if(DesktopProfile::origin(item.toObject()["url"].toString())==url)key=item.toObject()["profileKey"].toString();
    if(fromProfile&&key.isEmpty()){note(UiText::ThisProfileHasNoKeyForThe());return;}
    if(key.isEmpty())key=QString::fromLatin1(DesktopProfile::randomKey());
    QJsonObject body{{"profileKey",key}};
    if(!fromProfile){body["name"]=name;body["password"]=password->text();}
    resetLoginImport();api.reset();imageRequests.clear(); api.server=url;
    pages->currentWidget()->setEnabled(false); note(UiText::Connecting());
    api.request("POST",fromProfile?"/api/profile-session":"/api/auth",body,[this,url,name,servers,key,imported](QJsonObject data,QString error) mutable {
        pages->currentWidget()->setEnabled(true);
        if(!error.isEmpty()||data["session"].toString().isEmpty()){if(imported)openLocalHome();note(error.isEmpty()?UiText::NoSignInTokenReceived():error);return;}
        password->clear(); api.token=data["session"].toString();QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("lastServer",url.toString());
        auto member=data["member"].toObject(); auto display=member["name"].toString(name);
        if(profile.value("format")!="nimbo-profile")profile={{"format","nimbo-profile"},{"version",1},{"name",display},{"avatar",""}};
        const auto credential=data["profileKey"].toString(key);
        int index=0; while(index<servers.size()&&DesktopProfile::origin(servers[index].toObject()["url"].toString())!=url)++index;
        auto entry=index<servers.size()?servers[index].toObject():QJsonObject{};
        entry["url"]=url.toString();entry["displayName"]=display;entry["profileKey"]=credential;entry["role"]=member["role"];
        if(!entry.contains("name"))entry["name"]=url.host();
        if(index<servers.size())servers[index]=entry;else servers.append(entry);
        profile["servers"]=servers; pages->setCurrentIndex(1); note({}); saveLocal();
        bindMute();openView(imported?"profile":"room");setMemberPanel(QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).value("membersExpanded",false).toBool(),false);
        serverTitle->setText(url.host()); refresh(); timer.start(3000);
    });
}
void DesktopWindow::refresh() {
    if (refreshing || api.token.isEmpty()) return;
    refreshing=true;
    QElapsedTimer elapsed;elapsed.start();
    api.request("GET","/api/workspace",{},[this,elapsed](QJsonObject data,QString error){
        refreshing=false;workspaceLatency=elapsed.elapsed();workspaceFailed=!error.isEmpty();
        if(workspaceFailed){updateNetworkIndicator();note(error);return;}
        workspaceReceived.start();renderWorkspace(data);
        loadMessages();
    });
}
void DesktopWindow::clearChatHistory(){
    const bool large=messages->count()>=24 || !thumbnails.isEmpty()
        || messages->findChild<QWidget*>("videoAttachment") || messages->findChild<QWidget*>("audioAttachment");
    messages->clear();
    thumbnails.clear();imageFiles.clear();
    if(large){
        // Qt deletes item widgets on the next event loop turn. Return free heap
        // pages only after their backing images and layouts have been destroyed.
        QTimer::singleShot(250,this,releaseUnusedHeapPages);
    }
}
void DesktopWindow::selectChannel() {
    auto* item=channels->currentItem();
    const auto room=item?item->data(Qt::UserRole).toJsonObject():QJsonObject{};
    auto id=room.value("id").toString();
    if(id.isEmpty() && !rooms.isEmpty()){
        QSignalBlocker block(channels);
        for(int i=0;i<channels->count();++i){
            if(channels->item(i)->data(Qt::UserRole).toJsonObject().value("id").toString()==selected && !selected.isEmpty()){
                channels->setCurrentRow(i);break;
            }
        }
        return;
    }
    if (id!=selected) {
        if(!selected.isEmpty()) drafts[selected]=compose->draftText();
        selected=id; ++channelGeneration; loadingMessages=false; loadingOlder=false; moreHistory=false;
        compose->setDraft(drafts.value(id),draftFormats.value(id)); lastMessages={}; clearChatHistory(); video->clear();
        clearMessageActions();stageSignature.clear();
    }
    kind=room.value("kind").toString();if(view=="room")title->setText(room.value("name").toString());
    QString glyph=room.value("icon").toString();if(glyph.isEmpty())glyph=kind=="text"?"hash":"sound";
    titleIcon->setPixmap(Ui::icon(glyph).pixmap(21,21));
    const bool chat=kind=="text";messages->setVisible(chat);composer->setVisible(chat);stage->setVisible(kind=="voice");
    notifyButton->setVisible(chat&&view=="room");
    QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
    const bool enabled=prefs.value("notify/"+api.server.host()+"/"+selfId+"/"+selected,false).toBool();
    notifyButton->setIcon(Ui::icon(enabled?"sound":"sound-off"));notifyButton->setToolTip(enabled?UiText::MuteChannelNotifications():UiText::EnableChannelNotifications());
    roomCount->setVisible(!chat&&view=="room");olderButton->setVisible(chat&&moreHistory);
    auto policy=room.value("posting").toObject();
    bool write=policy.value("mode")!="roles"||self.value("role")=="owner";
    for(auto role:policy.value("roleIds").toArray())if(self.value("roleIds").toArray().contains(role)||self.value("role")==role)write=true;
    write=write&&self.value("chatMutedUntil").toDouble()<=QDateTime::currentMSecsSinceEpoch();
    compose->setEnabled(write);attachButton->setEnabled(write);emojiButton->setEnabled(write);
    updateComposer();updateMentionIndicators();refreshStage();refreshDock();loadMessages();
}
void DesktopWindow::loadMessages() {
    if(kind!="text"||selected.isEmpty()||loadingMessages||loadingOlder||api.token.isEmpty())return;
    loadingMessages=true;auto gen=channelGeneration;
    const QString base="/api/messages?paged=1&channel="+encoded(selected);
    auto page=std::make_shared<std::function<void(QString,bool)>>();
    auto buffer=std::make_shared<QJsonArray>();auto seen=std::make_shared<QSet<QString>>();
    auto more=std::make_shared<bool>(false);
    std::weak_ptr<std::function<void(QString,bool)>> weak=page;
    *page=[this,base,gen,rev=messageRevision,weak,buffer,seen,more](QString cursor,bool continuation){
        auto next=weak.lock();if(!next)return;
        auto path=base+(cursor.isEmpty()?QString():"&from="+encoded(cursor))+(continuation?"&continuation=1":"");
        api.request("GET",path,{},[this,gen,rev,next,buffer,seen,more,continuation](QJsonObject data,QString error){
            if(gen!=channelGeneration)return;
            if(rev!=messageRevision){loadingMessages=false;loadMessages();return;}
            if(!error.isEmpty()){loadingMessages=false;note(error);return;}
            if(!continuation)*more=data["hasMore"].toBool();
            for(auto v:data["messages"].toArray())buffer->append(v);
            auto after=data["nextFrom"].toString();
            if(!after.isEmpty()&&!seen->contains(after)){seen->insert(after);(*next)(after,true);return;}
            loadingMessages=false;moreHistory=*more;olderButton->setVisible(moreHistory&&kind=="text");
            if(*buffer!=lastMessages){lastMessages=*buffer;updateMessageRows();}
        });
    };
    (*page)(lastMessages.isEmpty()?QString():lastMessages.first().toObject()["id"].toString(),false);
}
void DesktopWindow::loadOlder(){
    if(!moreHistory||loadingMessages||loadingOlder||lastMessages.isEmpty())return;
    loadingOlder=true;auto gen=channelGeneration;auto anchor=lastMessages.first().toObject()["id"].toString();
    api.request("GET","/api/messages?paged=1&channel="+encoded(selected)+"&before="+encoded(anchor),{},[this,gen](QJsonObject data,QString error){
        if(gen!=channelGeneration)return;loadingOlder=false;if(!error.isEmpty()){note(error);return;}
        auto older=data["messages"].toArray();int count=older.size();for(auto v:lastMessages)older.append(v);lastMessages=older;
        moreHistory=data["hasMore"].toBool();olderButton->setVisible(moreHistory);updateMessageRows();
        if(count<messages->count())messages->scrollToItem(messages->item(count),QAbstractItemView::PositionAtTop);
    });
}
void DesktopWindow::sendMessage() {
    QString raw=compose->draftText(),text=raw.trimmed(),room=selected;auto files=fileDrafts.value(room);
    if(kind!="text"||!compose->isEnabled()||(text.isEmpty()&&files.isEmpty())||sending)return;
    QJsonArray tags;int trim=raw.indexOf(text);
    for(auto v:mentionDrafts.value(room)){
        auto m=v.toObject();int a=m["start"].toInt(),b=m["end"].toInt();
        if(a>=trim&&raw.mid(a,b-a)=="@"+m["label"].toString()){m["start"]=a-trim;m["end"]=b-trim;tags.append(m);}
    }
    static const QRegularExpression everyone(QStringLiteral(R"((?:^|\s)(@everyone)(?=$|[\s.,!?;:]))"));
    auto matches=everyone.globalMatch(text);
    while(matches.hasNext()){
        const auto hit=matches.next();const int a=hit.capturedStart(1),b=hit.capturedEnd(1);
        auto overlap=std::any_of(tags.cbegin(),tags.cend(),[&](auto v){auto t=v.toObject();return t["start"].toInt()<b&&t["end"].toInt()>a;});
        if(!overlap){
            int pos=0;while(pos<tags.size()&&tags[pos].toObject()["start"].toInt()<a)++pos;
            tags.insert(pos,QJsonObject{{"id","everyone"},{"kind","everyone"},{"label","everyone"},{"start",a},{"end",b}});
        }
    }
    QJsonObject fields{{"text",text},{"replyTo",replyId},{"mentions",tags}};
    const QString format=compose->formatMask().mid(trim,text.size());
    if(!format.isEmpty())fields["format"]=format;
    QString sig=room+QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact))+files.join(QChar(0));
    if(sig!=sendSignature){sendSignature=sig;sendKey=QString::fromLatin1(DesktopProfile::randomKey());}
    fields["clientId"]=sendKey;
    if(!files.isEmpty()){
        outbox.insert(sendKey,PendingUpload{room,files,fields});
        outboxOrder.append(sendKey);fileDrafts.remove(room);compose->clear();mentionDrafts.remove(room);
        replyId.clear();replyBanner->hide();sendSignature.clear();
        uploadPending(sendKey);return;
    }
    sending=true;updateComposer();
    auto done=[this,room,text,format,reply=replyId,files](QJsonObject accepted,QString error){
        sending=false;if(!error.isEmpty()){note(error);updateComposer();return;}
        sendSignature.clear();note({});
        if(room==selected&&compose->draftText().trimmed()==text && compose->formatMask().mid(compose->draftText().indexOf(text),text.size())==format){compose->clear();drafts.remove(room);draftFormats.remove(room);mentionDrafts.remove(room);}
        else if(room!=selected&&drafts.value(room).trimmed()==text && draftFormats.value(room).mid(drafts.value(room).indexOf(text),text.size())==format){drafts.remove(room);draftFormats.remove(room);mentionDrafts.remove(room);}
        if(fileDrafts.value(room)==files)fileDrafts.remove(room);
        if(room==selected&&replyId==reply){replyId.clear();replyBanner->hide();}
        ++messageRevision;
        if(room==selected && !accepted["id"].toString().isEmpty()){
            if(Ui::byId(lastMessages,accepted["id"].toString()).isEmpty())lastMessages.append(accepted);
            updateMessageRows();QTimer::singleShot(0,messages,&QListWidget::scrollToBottom);
        }
        updateComposer();loadMessages();
    };
    fields["channel"]=room;api.request("POST","/api/messages",fields,done);
}
void DesktopWindow::messageMenu(QPoint position) {
    auto* item=messages->itemAt(position);if(!item)return;
    const auto message=item->data(Qt::UserRole).toJsonObject();
    memberMenu(message["authorId"].toString(),messages->viewport()->mapToGlobal(position));
}
void DesktopWindow::retireVoiceIdentity(){
    const auto entry=qMakePair(callOrigin,callIdentity);
    if(callIdentity.isEmpty() || endedVoiceSessions.contains(entry))return;
    endedVoiceSessions.append(entry);
    if(endedVoiceSessions.size()>64)endedVoiceSessions.removeFirst();
    if(callStateApi.server==api.server && selfId==callIdentity.section('.',0,0) && !api.token.isEmpty())callStateApi.token=api.token;
    ++pendingVoiceDepartures;
    callStateApi.request("POST","/api/voice-state",{{"identity",callIdentity},{"room",activeVoice},{"state","left"}},[this](QJsonObject,QString){
        --pendingVoiceDepartures;
        finishVoiceClose();
    });
}
void DesktopWindow::leaveCall(){
    pendingVoiceRoom.clear();pendingVoiceOrigin.clear();
    if((!inCall && !joining) || leavingVoice)return;
    note({});
    if(joining && callIdentity.isEmpty()){
        // An unused /api/join token has no presence in LiveKit. A new room can request its own immediately.
        setCall(false);
        return;
    }
    leavingVoice=true;
    retireVoiceIdentity();
    media->stop();
    if(callOrigin==api.server.toString())renderWorkspace(workspace,WorkspaceRefresh::Channels);
    refreshDock();
}
void DesktopWindow::playCallCue(const QString& cue){
    if(!inCall || leavingVoice)return;
    playSound("voice-"+cue);
}
void DesktopWindow::playSound(const QString& file){
    auto*& sound=callSounds[file];
    QAudioDevice device=QMediaDevices::defaultAudioOutput();
    for(const auto& candidate:QMediaDevices::audioOutputs())if(candidate.id()==outputDevice){device=candidate;break;}
    if(!sound){
        sound=new QSoundEffect(device,this);
        if(file=="chat"){
            auto release=[this,file,effect=sound]{
                if(effect->isPlaying() || callSounds.value(file)!=effect)return;
                callSounds.remove(file);
                effect->deleteLater();
            };
            connect(sound,&QSoundEffect::playingChanged,sound,release,Qt::QueuedConnection);
            connect(sound,&QSoundEffect::statusChanged,sound,[effect=sound,release]{
                if(effect->status()==QSoundEffect::Error)release();
            },Qt::QueuedConnection);
        }
        sound->setSource(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()+"/assets/sounds/"+file+".wav"));
    }else if(sound->audioDevice()!=device)sound->setAudioDevice(device);
    sound->setMuted(qEnvironmentVariableIsSet("NIMBO_QA_SILENT_OUTPUT") ||
                    (generatedMedia && !qEnvironmentVariableIsSet("NIMBO_QA_PLAY_CUES")));
    sound->play();
}
void DesktopWindow::updateCallRoster(const QJsonArray& people){
    if(!inCall)return;
    if(rosterSeeded && liveRoster==people)return;
    QHash<QString,QJsonObject> before;for(auto old:liveRoster)before.insert(old.toObject()["identity"].toString(),old.toObject());
    bool onlySpeech=rosterSeeded && liveRoster.size()==people.size();
    QList<QJsonObject> speechChanges;
    for(auto value:people){
        auto peer=value.toObject();auto old=before.take(peer["identity"].toString());
        if(!rosterSeeded)continue;
        if(peer!=old){
            auto updated=old;
            updated["speaking"]=peer["speaking"];
            if(updated==peer)speechChanges.append(peer);
            else onlySpeech=false;
        }
        if(old.isEmpty()){playCallCue("join");continue;}
        for(const auto key:{"mic","camera","screen"}){
            const bool on=peer[key].toBool();
            if(old[key].toBool()!=on)playCallCue(QString(key)=="mic"?(on?"unmute":"mute"):(on?"video-on":"video-off"));
        }
    }
    if(rosterSeeded && !before.isEmpty())playCallCue("leave");
    rosterSeeded=true;
    if(liveRoster==people)return;
    liveRoster=people;
    if(onlySpeech){
        if(callOrigin==api.server.toString())for(const auto& person:speechChanges)updateSpeakingPerson(person);
        return;
    }
    if(callOrigin==api.server.toString())renderWorkspace(workspace,WorkspaceRefresh::Channels);else refreshStage();
}
void DesktopWindow::join() {
    if(closingWindow || kind!="voice")return;
    if(inCall || joining){
        if(!leavingVoice && activeVoice==selected && callOrigin==api.server.toString())return;
        if(!leavingVoice)leaveCall();
        if(inCall || joining){
            pendingVoiceRoom=selected;pendingVoiceOrigin=api.server.toString();
            refreshStage();
            return;
        }
    }
    idleMediaTimer.stop();
    joining=true;joinProgress.start();
    QTimer::singleShot(500,this,[this]{if(joining){stageSignature.clear();refreshStage();}});
    const auto room=selected;voiceRoomName=title->text();voiceServerName=serverTitle->text();
    activeVoice=room;callOrigin=api.server.toString();callStateApi.server=api.server;callStateApi.token=api.token;
    renderWorkspace(workspace,WorkspaceRefresh::Channels);refreshDock();
    note({});
    const auto generation=++voiceInviteGeneration;
    api.request("POST","/api/join",{{"room",room},{"name",self.value("name").toString()}},[this,generation](QJsonObject data,QString error){
        if(generation!=voiceInviteGeneration)return;
        if(!error.isEmpty()){setCall(false);note(error);return;}
        callIdentity=data["identity"].toString();
        if(leavingVoice){setCall(false);return;}
        if(!media){
            media=std::make_unique<MediaSession>();
            connect(media.get(),&VoiceEvents::connectedChanged,this,&DesktopWindow::setCall);
            connect(media.get(),&VoiceEvents::failed,this,[this](QString message){
                if(leavingVoice)return; // The queued stop completes the handover.
                setCall(false);note(message);
            });
            connect(media.get(),&VoiceEvents::audioWarning,this,&DesktopWindow::note);
            connect(media.get(),&VoiceEvents::videoModeChanged,this,[this](bool share,int mode,QString error){
                if(!mode)QTimer::singleShot(250,this,releaseUnusedHeapPages);
                if(error.isEmpty() && bool(share?screenMode:cameraMode)!=bool(mode))playCallCue(mode?"video-on":"video-off");
                (share?screenMode:cameraMode)=mode;cameraButton->setEnabled(inCall);screenButton->setEnabled(inCall);
                cameraButton->setText(cameraMode==2?UiText::TurnOffCamera():UiText::Camera());screenButton->setText(screenMode>=3?UiText::StopSharing():UiText::ScreenSharing());refreshDock();updateLocalPerson();note(error);
            });
            connect(media.get(),&VoiceEvents::screenAudioChanged,this,[this](bool on,QString error){sharingSound=on;refreshDock();if(!error.isEmpty())note(error);});
            connect(media.get(),&VoiceEvents::effectFailed,this,[this]{QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("blur",false);if(view=="video")renderView();note(UiText::CouldNotEnableBlurTheCameraIs());});
            connect(media.get(),&VoiceEvents::microphoneMuted,this,[this](bool off){
                if(!inCall || muted==off)return;
                playCallCue(off?"mute":"unmute");muted=off;if(muted)localSpeaking=false;
                muteButton->setText(muted?UiText::UnmuteMicrophone():UiText::MuteMicrophone());
                updateLocalPerson();refreshDock();
            });
            connect(media.get(),&VoiceEvents::audioDeviceSelected,this,[this](bool input,const QByteArray& applied,const QString& error){
                const QString key=input?"input":"output";
                (input?inputDevice:outputDevice)=applied;
                if(error.isEmpty())QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue(key,applied);
                if(auto* combo=detailPage->findChild<QComboBox*>("device-"+key)){
                    QSignalBlocker guard(combo);
                    combo->setCurrentIndex(qMax(0,combo->findData(applied)));combo->setEnabled(true);
                }
                note(error);
            });
            connect(media.get(),&VoiceEvents::localConnectionQualityChanged,this,[this](const QString& state){
                if(!inCall || state==callQuality)return;
                callQuality=state;updateNetworkIndicator();
            });
            connect(media.get(),&VoiceEvents::localSpeakingChanged,this,[this](bool active){
                localSpeaking=active;updateLocalPerson();
            });
            connect(media.get(),&VoiceEvents::participantFrame,this,&DesktopWindow::frameArrived);
            connect(media.get(),&VoiceEvents::callRoster,this,&DesktopWindow::updateCallRoster);
        }
        MediaOptions opts;
        opts.applicationPid=quint32(QCoreApplication::applicationPid());
        QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);opts.blur=prefs.value("blur",false).toBool();opts.blurRadius=prefs.value("blurRadius",12).toInt();opts.noise=prefs.value("noise",true).toBool();opts.echo=prefs.value("echo",true).toBool();opts.agc=prefs.value("agc",false).toBool();opts.noiseEngine=prefs.value("engine","gtcrn").toString();opts.url=data["url"].toString();opts.token=data["token"].toString();opts.inputId=inputDevice;opts.outputId=outputDevice;opts.synthetic=generatedMedia;
        QUrl endpoint(opts.url); if(endpoint.scheme()!="wss"&&!(api.server.scheme()=="http"&&endpoint.scheme()=="ws")){
            setCall(false);note(UiText::InsecureMediaServerAddress());return;
        }
        const auto volumePrefix="volume/"+api.server.toString()+"/";
        for(const auto entry:workspace["members"].toArray()){
            const auto id=entry.toObject()["id"].toString();
            const int level=qBound(0,prefs.value(volumePrefix+id,100).toInt(),200);
            if(level!=100)opts.participantVolumes.insert(id,level);
        }
        media->start(opts);
    });
}
void DesktopWindow::setCall(bool active) {
    if(active && leavingVoice)return; // A stop is already queued on the media worker.
    const bool wasJoining=joining;
    const bool endedHere=!active && !callIdentity.isEmpty();
    if(endedHere){
        retireVoiceIdentity();
        callIdentity.clear();
    }
    if(!active){
        for(auto it=callSounds.begin();it!=callSounds.end();){
            if(it.key().startsWith("voice-")){
                it.value()->stop();
                it.value()->deleteLater();
                it=callSounds.erase(it);
            }else ++it;
        }
        if(media)idleMediaTimer.start();
        ++voiceInviteGeneration;
        const auto next=std::exchange(pendingVoiceRoom,QString{}),origin=std::exchange(pendingVoiceOrigin,QString{});
        if(!next.isEmpty())QTimer::singleShot(0,this,[this,next,origin]{if(selected==next && api.server.toString()==origin && kind=="voice" && !inCall && !joining)join();});
        leavingVoice=false;rosterSeeded=false;callQuality.clear();updateNetworkIndicator();}
    inCall=active;joining=false;muted=false;localSpeaking=false;cameraMode=0;screenMode=0;sharingSound=false;if(!active){mutedStreams.clear();trackFrames.clear();liveRoster={};frameHosts.clear();}
    removeKeys();
    joinButton->setText(active?UiText::Disconnect():UiText::Connect());joinButton->setVisible(active||kind=="voice");joinButton->setEnabled(true);
    for(auto* b:{muteButton,cameraButton,screenButton}){b->setVisible(active);b->setEnabled(active);}
    muteButton->setText(UiText::MuteMicrophone()); cameraButton->setText(UiText::Camera());screenButton->setText(UiText::ScreenSharing());
    if(active){
        note({});bindMute();
    }else{activeVoice.clear();callOrigin.clear();callLabel->clear();video->clear();video->hide();}
    stageSignature.clear();
    if((active||endedHere||wasJoining) && !workspace.isEmpty() && !api.token.isEmpty())renderWorkspace(workspace,WorkspaceRefresh::Channels);
    refreshStage();refreshDock();
    finishVoiceClose();
}
void DesktopWindow::toggleMute() {
    if(!inCall||!media)return;
    muted=!muted;playCallCue(muted?"mute":"unmute");media->setMuted(muted);muteButton->setText(muted?UiText::UnmuteMicrophone():UiText::MuteMicrophone());
    if(muted)localSpeaking=false;
    updateLocalPerson();refreshDock();
}
void DesktopWindow::chooseVideo(bool share) {
    if(!inCall)return;
    int mode=0;QString screen;QCapturableWindow window;
    if(share&&!screenMode){
        const auto identity=callIdentity;
        auto source=chooseShareSource(this);
        if(!source || !inCall || leavingVoice || identity!=callIdentity)return;
        screen=source->screen;window=source->window;mode=screen.isEmpty()?4:3;
    }else if(!share&&!cameraMode)mode=2;
    cameraButton->setEnabled(false);screenButton->setEnabled(false);media->changeVideo(share,mode,cameraDevice,screen,window);
}
void DesktopWindow::devices() {
    QDialog dialog(this);dialog.setWindowTitle(UiText::SettingsSoundAndVideo());dialog.setMinimumWidth(420);
    QFormLayout form(&dialog);QComboBox inputs,outputs,cameras;
    auto fill=[](auto& combo,const auto& devices,QByteArray id){
        combo.addItem(UiText::SystemDefault(),QByteArray{});for(const auto& d:devices)combo.addItem(d.description(),d.id());
        combo.setCurrentIndex(qMax(0,combo.findData(id)));
    };
    fill(inputs,QMediaDevices::audioInputs(),inputDevice);fill(outputs,QMediaDevices::audioOutputs(),outputDevice);fill(cameras,QMediaDevices::videoInputs(),cameraDevice);
    form.addRow(UiText::Microphone(),&inputs);form.addRow(UiText::Speakers(),&outputs);form.addRow(UiText::Camera(),&cameras);
    if(inCall)form.addRow(label(UiText::AudioDevicesWillChangeOnTheNext()));
    QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Save)->setText(UiText::Save());buttons.button(QDialogButtonBox::Cancel)->setText(UiText::Cancel());form.addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){inputDevice=inputs.currentData().toByteArray();outputDevice=outputs.currentData().toByteArray();cameraDevice=cameras.currentData().toByteArray();}
}
