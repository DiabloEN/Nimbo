#include "desktop_updates.h"
#include "interface_text.h"
#include "window.h"
#include "invitation_settings.h"
#include "member_settings.h"
#include "member_server_link.h"
#include "wrap_layout.h"
#include "message_text.h"
#include "message_input.h"
#include "profile.h"
#include "ui.h"
#include "voice_card.h"
#include "voice_channel_list.h"
#include "stream_viewer.h"
#include "presenter_ink.h"
#include "server_picker.h"
#include <QSystemTrayIcon>

using namespace Ui;
namespace {
class MemberAvatar final : public QLabel {
public:
    MemberAvatar(){setObjectName("memberAvatar");setFixedSize(34,34);}
    void setOnline(bool value){
        const QString description=value?QString::fromUtf8(UiText::MemberOnline()):QString();
        if(online==value && accessibleDescription()==description)return;
        online=value;setAccessibleDescription(description);setToolTip(description);update();
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);
        if(!online)return;
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor("#81bc96"));
        painter.drawEllipse(QRectF(width()-7,height()-7,7,7));
    }
private:
    bool online=false;
};
class RosterToggle final : public QPushButton {
public:
    RosterToggle():arrow(Ui::tintedIcon("chevronDown",QColor("#a0a0a0"))),
                  hotArrow(Ui::tintedIcon("chevronDown",QColor("#eeeeee"))) {
        setFixedSize(210,62);setCheckable(true);setCursor(Qt::PointingHandCursor);
        setStyleSheet("background:transparent;border:0;font-size:12px;padding:0");
        curve.setType(QEasingCurve::BezierSpline);
        curve.addCubicBezierSegment({.22,1},{.36,1},{1,1});
    }
    int count=0;
    qreal progress=0;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const bool hot=underMouse();
        if(hasFocus() && testAttribute(Qt::WA_KeyboardFocusChange))painter.fillRect(rect(),QColor("#3b4140"));
        const QString label=QString::fromUtf8(UiText::Members());
        const QRectF content=QRectF(rect()).adjusted(16,0,-16,0);
        painter.setPen(QColor(hot?"#eeeeee":"#a0a0a0"));
        painter.drawText(content,Qt::AlignLeft|Qt::AlignVCenter,label);
        painter.setPen(QColor("#737373"));
        painter.drawText(content.adjusted(painter.fontMetrics().horizontalAdvance(label)+7,0,0,0),Qt::AlignLeft|Qt::AlignVCenter,QString::number(count));
        painter.translate(content.right()-8,content.center().y());
        painter.rotate(-180*curve.valueForProgress(progress));
        (hot?hotArrow:arrow).paint(&painter,QRect(-8,-8,16,16));
    }
private:
    QIcon arrow,hotArrow;QEasingCurve curve;
};
class ChannelDelegate final : public QStyledItemDelegate {
public:
    explicit ChannelDelegate(QListWidget* list):QStyledItemDelegate(list){
        list->viewport()->installEventFilter(this);
        connect(list->model(),&QAbstractItemModel::modelReset,this,[this]{voiceSheets.clear();});
    }
    bool eventFilter(QObject* object,QEvent* event) override{
        if(event->type()==QEvent::Hide && object==static_cast<QListWidget*>(parent())->viewport())
            voiceSheets.clear();
        return QStyledItemDelegate::eventFilter(object,event);
    }
    void updateEditorGeometry(QWidget* widget,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        if(widget->objectName()=="channelLeave"){
            const QRect row=option.rect;
            widget->setGeometry(row.right()-35,row.top()+(row.height()-30)/2,30,30);
            return;
        }
        QStyledItemDelegate::updateEditorGeometry(widget,option,index);
    }
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override {
        const auto room=index.data(Qt::UserRole).toJsonObject();
        const auto person=index.data(Qt::UserRole+2).toJsonObject();
        if(!person.isEmpty()){
            painter->save();
            if(parent()->property("voiceDrag").toString()==person["identity"].toString())painter->setOpacity(.45);
            const QRect avatarRect(option.rect.left()+27,option.rect.top()+8,22,22);
            const auto avatarIcon=index.data(Qt::DecorationRole).value<QIcon>();
            if(avatarIcon.isNull()){
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen);painter->setBrush(QColor("#303030"));
                painter->drawEllipse(QRectF(avatarRect));
                auto font=option.font;font.setPixelSize(8);font.setWeight(QFont::Normal);
                painter->setFont(font);painter->setPen(QColor("#ddd"));
                const auto initials=person["name"].toString().trimmed().left(2).toUpper();
                painter->drawText(avatarRect,Qt::AlignCenter,initials.isEmpty()?QStringLiteral("Н"):initials);
            }else avatarIcon.paint(painter,avatarRect);
            int right=option.rect.right()-6;
            static const QHash<QString,QIcon> icons{
                {"screen",tintedIcon("screen",QColor("#777"))},{"camera",tintedIcon("camera",QColor("#777"))},
                {"mic-off",tintedIcon("mic-off",QColor("#777"))}
            };
            auto hint=[&](const QString& glyph){
                icons.value(glyph).paint(painter,QRect(right-11,avatarRect.center().y()-5,12,12));
                right-=17;
            };
            if(person["screen"].toBool())hint("screen");
            if(person["camera"].toBool())hint("camera");
            if(person["micKnown"].toBool() && !person["mic"].toBool())hint("mic-off");
            const int left=avatarRect.right()+8;
            const auto area=QRect(left,avatarRect.top(),qMax(0,right-left+1),22);
            auto font=option.font;font.setPixelSize(11);painter->setFont(font);
            painter->setPen(QColor(person["speaking"].toBool()?"#fff":"#a0a0a0"));
            painter->drawText(area,Qt::AlignVCenter|Qt::AlignLeft,painter->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,area.width()));
            painter->restore();return;
        }
        if(room.isEmpty()){QStyledItemDelegate::paint(painter,option,index);return;}
        const bool selected=option.state.testFlag(QStyle::State_Selected);
        const bool activeText=selected && room["kind"]=="text";
        const bool focused=option.state.testFlag(QStyle::State_HasFocus) && option.state.testFlag(QStyle::State_KeyboardFocusChange);
        const bool hovered=option.state.testFlag(QStyle::State_MouseOver);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool drop=parent()->property("voiceDrop").toString()==room["id"].toString();
        const bool voice=room["kind"]=="voice";
        const bool keyboardFocus=focused && !drop;
        // The variable-width middle contains only the fill. Both ends retain the
        // CSS corner coverage and the original Iconsax glyph at display scale.
        bool paintedVoice=false;
        if(voice && !keyboardFocus && option.rect.width()>=64){
            const auto dpr=painter->device()->devicePixelRatioF();
            const int scale=dpr<=1?1:2;
            const auto glyph=room["icon"].toString();
            const QString path=QStringLiteral(":/voice-rows/%1@%2.png").arg(glyph.isEmpty()?QStringLiteral("sound"):glyph).arg(scale);
            auto* cached=voiceSheets.object(path);
            if(!cached){
                const QImage image(path);
                if(!image.isNull()){
                    cached=new QPixmap(QPixmap::fromImage(image));voiceSheets.insert(path,cached);
                }
            }
            const QPixmap sheet=cached?*cached:QPixmap{};
            if(!sheet.isNull()){
                const int state=drop?3:selected?2:hovered?1:0;
                const auto row=option.rect;
                const int y=45*scale*state,h=45*scale;
                painter->setRenderHint(QPainter::SmoothPixmapTransform,true);
                painter->drawPixmap(QRect(row.left(),row.top(),34,row.height()),sheet,QRect(0,y,34*scale,h));
                painter->drawPixmap(row.adjusted(34,0,-12,0),sheet,QRect(34*scale,y,scale,h));
                painter->drawPixmap(QRect(row.right()-11,row.top(),12,row.height()),sheet,QRect(52*scale,y,12*scale,h));
                paintedVoice=true;
            }
        }
        if(!paintedVoice && (selected || hovered || focused || drop)){
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(drop?"#080808":focused?"#3b4140":selected?"#252525":"#303030"));
            painter->drawRoundedRect(option.rect,11,11);
        }
        const QRect content=option.rect.adjusted(12,0,-12,0);
        const QRect mark(content.left(),content.top()+qRound((content.height()-18)/2.),18,18);
        const auto icon=index.data(Qt::DecorationRole).value<QIcon>();
        if(!paintedVoice)icon.paint(painter,mark,Qt::AlignCenter,activeText?QIcon::Selected:QIcon::Normal);

        const int indicators=index.data(Qt::UserRole+1).toInt();
        if(indicators&1){
            painter->setBrush(QColor("#81bc96"));painter->setPen(QPen(QColor("#101010"),1));
            painter->drawEllipse(QRectF(mark.right()-3,mark.bottom()-3,5,5));
        }
        if(indicators&2){
            painter->setBrush(QColor("#f05261"));painter->setPen(Qt::NoPen);
            painter->drawEllipse(QRectF(content.right()-6,content.center().y()-3,6,6));
        }
        auto area=content.adjusted(28,0,-((indicators&2)?15:0),0);
        if(room["kind"]=="voice"){
            area.setRight(option.rect.right()-6-(index.data(Qt::UserRole+3).toBool()?36:0));
            const auto since=index.data(Qt::UserRole+6).toLongLong();
            if(since>0){
                const auto seconds=qMax<qint64>(0,(QDateTime::currentMSecsSinceEpoch()-since)/1000);
                const auto elapsed=(seconds>=3600?QString::number(seconds/3600)+":":QString())+
                    QString("%1:%2").arg(seconds/60%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
                auto font=option.font;font.setPixelSize(10);painter->setFont(font);painter->setPen(QColor("#a0a0a0"));
                painter->drawText(area,Qt::AlignRight|Qt::AlignVCenter,elapsed);
                area.adjust(0,0,-painter->fontMetrics().horizontalAdvance(elapsed)-6,0);
            }
        }
        painter->setFont(option.font);painter->setPen(QColor(activeText?"#eeeeee":"#a0a0a0"));
        painter->drawText(area,Qt::AlignLeft|Qt::AlignVCenter,option.fontMetrics.elidedText(index.data().toString(),Qt::ElideRight,area.width()));
        painter->restore();
    }
private:
    mutable QCache<QString,QPixmap> voiceSheets{8};
};
QStringList emojiPalette() {
    return QStringLiteral("😀 😃 😄 😁 😆 😅 😂 🤣 🙂 😉 😊 😍 🥰 😘 😎 🤔 😮 😢 😭 😡 🤯 🥳 😴 🤝 👍 👎 👏 🙌 🙏 💪 👀 ❤️ 💔 🔥 ✨ 🎉 💯 ✅ ❌ 🚀 ☕ 🎮 🐈 🐕 🌈 🎵").split(' ');
}
}
void DesktopWindow::buildWorkspace() {
    auto* page=new QWidget;page->setObjectName("shell");
    page->setStyleSheet(QStringLiteral(
      "QWidget#sidebar,QWidget#membersPanel,QWidget#dock{background:#101010;border-radius:14px}"
      "QWidget#composePanel[focused=true]{background:#1c1c1c} QWidget#composePanel{background:#171717;border-radius:14px} QWidget#composePanel QLineEdit{background:transparent;border:0}"
      "QPushButton[flatIcon=true]{background:transparent;border:0;padding:0;color:#ededed;text-align:center} QPushButton[profileLink=true]{background:transparent;border:0;padding:0;color:#ededed;text-align:left}"
      "QPushButton[flatIcon=true]:hover,QPushButton[profileLink=true]:hover{background:#262626;border-radius:9px}"
      "QPushButton#serverSwitcher[menuOpen=true]{background:#262626;border-radius:6px}"
      "QListWidget#channels,QListWidget#messages,QListWidget#members{background:transparent;border:0;outline:0;padding:0}"
      "QListWidget#channels::item{color:#a0a0a0;padding:0 12px;border-radius:11px} QListWidget#channels::item:selected{background:#252525;color:#eee}"
      "QListWidget#channels::item:hover:!selected{background:#303030} QListWidget#channels::item:disabled{background:transparent;color:#909090}"
      "QListWidget#messages::item,QListWidget#members::item{padding:0;border:0;background:transparent} QListWidget#messages::item:hover{background:transparent}"
      "QScrollArea{border:0;background:transparent} QScrollBar:vertical{width:5px;background:transparent} QScrollBar::handle:vertical{background:#393939;border-radius:2px;min-height:22px}"
      "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}"));
    auto* shell=new QHBoxLayout(page);shell->setContentsMargins(8,8,8,8);shell->setSpacing(8);
    auto* sidebar=new QWidget;sidebar->setObjectName("sidebar");sidebar->setFixedWidth(242);shell->addWidget(sidebar);
    auto* nav=new QVBoxLayout(sidebar);nav->setContentsMargins(18,15,18,14);nav->setSpacing(0);
    auto* brandRow=new QHBoxLayout;brandRow->setContentsMargins(12,0,8,0);
    auto* brand=text("nimbo.","font-size:20px;font-weight:700");
    brand->setMinimumHeight(27);brandRow->addWidget(brand);brandRow->addStretch();serverSwitch=action("servers",UiText::ChooseServer(),26);serverSwitch->setObjectName("serverSwitcher");serverSwitch->setIconSize({19,19});static_cast<IconButton*>(serverSwitch)->setHoverIcons(tintedIcon("servers",QColor("#888")),tintedIcon("servers",QColor("#ddd")));
    brandRow->addWidget(serverSwitch);nav->addLayout(brandRow);nav->addSpacing(12);
    auto* serverBox=new QPushButton;serverBox->setProperty("profileLink",true);serverBox->setFixedHeight(54);
    auto* summary=new QHBoxLayout(serverBox);summary->setContentsMargins(12,6,12,6);summary->setSpacing(10);
    serverBadge=text("");serverBadge->setFixedSize(34,34);summary->addWidget(serverBadge);
    auto* meta=new QVBoxLayout;meta->setSpacing(2);serverTitle=text("","font-size:12px;font-weight:500");
    auto* headingRow=new QHBoxLayout;
    headingRow->setSpacing(5);
    serverTitle->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    headingRow->addWidget(serverTitle,1);
    latencyBadge=text("","font-size:9px;color:#d7b76c");headingRow->addWidget(latencyBadge);
    networkDot=new QLabel;networkDot->setFixedSize(6,6);headingRow->addWidget(networkDot);
    meta->addLayout(headingRow);
    serverDescription=text("","font-size:10px;color:#888");meta->addWidget(serverDescription);
    networkWarning=text("","font-size:10px;color:#dfb76a");meta->addWidget(networkWarning);
    summary->addLayout(meta,1);
    updateNetworkIndicator();
    nav->addWidget(serverBox);nav->addSpacing(22);
    auto* voiceList=new VoiceChannelList;
    voiceList->origin=[this]{return api.server.toString();};
    voiceList->canMove=[this](auto id){return canDragVoice(id);};
    voiceList->moveMember=[this](auto id,auto from,auto to){requestVoiceMove(id,from,to);};
    channels=voiceList;channels->setObjectName("channels");channels->setIconSize({18,18});channels->setItemDelegate(new ChannelDelegate(channels));channels->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channels->setStyleSheet("font-size:12px");nav->addWidget(channels,1);
    auto* footer=new QHBoxLayout;footer->setSpacing(3);footer->setContentsMargins(0,6,0,4);
    profileButton=new QPushButton;profileButton->setObjectName("profileNav");profileButton->setProperty("profileLink",true);
    profileButton->setAccessibleName(UiText::MyProfile());profileButton->setMinimumHeight(52);
    auto* profileLayout=new QHBoxLayout(profileButton);profileLayout->setContentsMargins(4,9,5,9);profileLayout->setSpacing(10);
    selfAvatar=text("");selfAvatar->setFixedSize(34,34);profileLayout->addWidget(selfAvatar);
    profileName=text("","font-family:Roboto;font-size:12px;font-weight:500;color:#ededed");profileName->setFixedHeight(17);
    profileName->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);profileLayout->addWidget(profileName,1);
    settingsButton=action("settings",UiText::Settings());settingsButton->setObjectName("settingsNav");
    settingsUpdateDot=new QLabel(settingsButton);
    settingsUpdateDot->setObjectName("settingsUpdateDot");
    settingsUpdateDot->setFixedSize(6,6);
    settingsUpdateDot->move(settingsButton->width()-12,settingsButton->height()-12);
    settingsUpdateDot->setStyleSheet("background:#64aaf0;border-radius:3px");
    settingsUpdateDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    settingsUpdateDot->hide();
    updates->availabilityChanged=[this]{refreshUpdateBadge();};
    footer->addWidget(profileButton,1);footer->addWidget(settingsButton);nav->addLayout(footer);
    auto* column=new QVBoxLayout;column->setContentsMargins(0,0,0,0);column->setSpacing(6);shell->addLayout(column,1);
    header=new QWidget;header->setFixedHeight(62);
    headerLayout=new QHBoxLayout(header);headerLayout->setContentsMargins(32,0,0,0);headerLayout->setSpacing(10);
    titleIcon=text("");titleIcon->setFixedSize(22,22);headerLayout->addWidget(titleIcon);title=text("","font-size:19px;font-weight:500");headerLayout->addWidget(title);
    roomCount=text("","font-size:11px;color:#888");headerLayout->addWidget(roomCount);
    mentionDot=new QLabel;mentionDot->setFixedSize(6,6);mentionDot->setAccessibleName(UiText::UnreadMentionsOrReplies());
    mentionDot->setStyleSheet("background:#f05261;border-radius:3px");headerLayout->addWidget(mentionDot);mentionDot->hide();
    notifyButton=action("sound-off",UiText::ChannelNotifications(),26);headerLayout->addWidget(notifyButton);headerLayout->addStretch();
    backButton=new QPushButton(UiText::BackToRoom());backButton->setIcon(icon("back"));backButton->setContentsMargins(0,0,32,0);backButton->setStyleSheet("QPushButton{background:transparent;border:0;font-size:12px;padding:8px} QPushButton:hover,QPushButton:focus{background:#202020}");headerLayout->addWidget(backButton);backButton->hide();
    column->addWidget(header);content=new QStackedWidget;column->addWidget(content,1);
    roomPage=new QWidget;roomLayout=new QVBoxLayout(roomPage);roomLayout->setContentsMargins(0,0,0,0);roomLayout->setSpacing(6);content->addWidget(roomPage);
    olderButton=new QPushButton(UiText::ShowEarlierMessages());roomLayout->addWidget(olderButton,0,Qt::AlignHCenter);olderButton->hide();
    messages=new QListWidget;messages->setObjectName("messages");messages->setWordWrap(true);messages->setSelectionMode(QAbstractItemView::NoSelection);
    messages->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    messages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);messages->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    roomLayout->addWidget(messages,1);messages->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(messages,&QListWidget::customContextMenuRequested,this,&DesktopWindow::messageMenu);
    stage=new QScrollArea;auto* scroll=static_cast<QScrollArea*>(stage);scroll->setWidgetResizable(true);
    auto* stageInner=new QWidget;auto* stageLayout=new QVBoxLayout(stageInner);stageLayout->setContentsMargins(32,2,0,16);stageLayout->setSpacing(0);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->viewport()->setObjectName("callViewport");stageLayout->setSizeConstraint(QLayout::SetNoConstraint);
    emptyRoom=new QPushButton(UiText::JoinTheConversation());emptyRoom->setFixedHeight(240);emptyRoom->setObjectName("joinRoomArea");
    emptyRoom->setStyleSheet("QPushButton{background:transparent;border:1px dashed #3b3b3b;border-radius:18px;font-size:16px;font-weight:400;color:#aaa} QPushButton:hover,QPushButton:focus{border-color:#777;background:#101010}");
    stageLayout->addWidget(emptyRoom);stageCards=new QWidget;cardLayout=new QVBoxLayout(stageCards);cardLayout->setContentsMargins(0,0,0,0);cardLayout->setAlignment(Qt::AlignTop);cardLayout->setSpacing(20);cardLayout->setSizeConstraint(QLayout::SetNoConstraint);
    stageLayout->addWidget(stageCards);video=text("");video->setMaximumHeight(460);video->setAlignment(Qt::AlignCenter);stageLayout->addWidget(video);video->hide();
    stageLayout->addStretch();scroll->setWidget(stageInner);roomLayout->addWidget(stage,1);
    dock=new QWidget;dock->setObjectName("dock");auto* controls=new QHBoxLayout(dock);controls->setContentsMargins(12,6,12,6);controls->setSpacing(7);
    callReturn=new QPushButton;callReturn->setCursor(Qt::PointingHandCursor);callReturn->setFixedHeight(42);
    callReturn->setStyleSheet("QPushButton{background:transparent;border:0;padding:0;border-radius:8px} QPushButton:hover,QPushButton:focus{background:#242424}");
    auto* originLabels=new QVBoxLayout(callReturn);originLabels->setContentsMargins(8,4,8,4);originLabels->setSpacing(9);originLabels->setAlignment(Qt::AlignVCenter);
    callServer=text("","color:#a0a0a0;font-size:10px");callLabel=text("","font-size:12px;font-weight:500");
    for(auto* label:{callServer,callLabel}){
        label->setAttribute(Qt::WA_TransparentForMouseEvents);originLabels->addWidget(label);
    }
    controls->addWidget(callReturn);
    connect(callReturn,&QPushButton::clicked,this,[this]{
        for(const auto& value:profile["servers"].toArray()){
            const auto entry=value.toObject();
            if(DesktopProfile::origin(entry["url"].toString()).toString()==callOrigin){
                switchServer(entry,false,{},callIdentity);return;
            }
        }
    });
    muteButton=action("mic",UiText::MuteMicrophone(),40);cameraButton=action("camera",UiText::Camera(),40);screenButton=action("screen",UiText::ScreenSharing(),40);joinButton=action("exit",UiText::Disconnect(),40);
    for(auto* b:{muteButton,cameraButton,screenButton})controls->addWidget(b);
    screenSoundButton=action("sound-off",UiText::ShareSystemAudio(),32);controls->addWidget(screenSoundButton);screenSoundButton->hide();
    connect(screenSoundButton,&QPushButton::clicked,this,[this]{toggleStreamSound(callIdentity+"/screen");});
    sendQuality=Ui::select(UiText::CameraAndScreenSharingQuality());sendQuality->setStyleSheet("QComboBox{background:#242424;color:#ededed;border:0;border-radius:12px;padding:0 10px;font-size:12px} QComboBox::drop-down{width:0;border:0}");sendQuality->addItem(UiText::Auto(),1080);for(int h:{360,720,1080})sendQuality->addItem(QString::number(h)+"p",h);controls->addWidget(sendQuality);sendQuality->hide();
    quickEffects=action("sparkles",UiText::CameraEffects(),40);controls->addWidget(quickEffects);quickEffects->hide();
    connect(quickEffects,&QPushButton::clicked,this,&DesktopWindow::showCameraEffects);
    connect(sendQuality,&QComboBox::currentIndexChanged,this,[this]{if(media)media->setVideoHeight(sendQuality->currentData().toInt());});controls->addStretch();controls->addWidget(joinButton);roomLayout->addWidget(dock);
    buildMessageBanners();
    chatFileError=text("","font-size:12px;color:#aaa");roomLayout->addWidget(chatFileError);chatFileError->hide();
    draftPanel=new QWidget;auto* draftFiles=new WrapLayout(draftPanel);draftFiles->setSpacing(6);
    roomLayout->addWidget(draftPanel);draftPanel->hide();
    roomPage->setAcceptDrops(true);
    dropOverlay=new QLabel(UiText::AttachAFileNupTo10Files(),roomPage);
    static_cast<QLabel*>(dropOverlay)->setAlignment(Qt::AlignCenter);dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    dropOverlay->setStyleSheet("background:rgba(20,25,24,230);border:2px dashed #657b74;border-radius:14px;color:white");dropOverlay->hide();
    composer=new QWidget;composer->setObjectName("composePanel");composer->setFixedHeight(56);
    auto* line=new QHBoxLayout(composer);line->setContentsMargins(12,10,12,10);line->setSpacing(8);
    attachButton=action("paperclip",UiText::AttachFiles(),36);attachButton->setIconSize({20,20});line->addWidget(attachButton);
    compose=new MessageInput;compose->setObjectName("messageText");compose->setPlaceholderText(UiText::WriteAMessageToMention());line->addWidget(compose,1);
    characterCount=text("","font-size:11px;color:#888");line->addWidget(characterCount);characterCount->hide();
    emojiButton=action("reaction-solid",UiText::AddEmoji(),36);emojiButton->setIconSize({20,20});static_cast<IconButton*>(emojiButton)->setHoverIcons(tintedIcon("reaction-solid",QColor("#a0a0a0")),tintedIcon("reaction-solid",Qt::white));line->addWidget(emojiButton);
    sendButton=action("arrow",UiText::SendMessage(),36);sendButton->setIconSize({20,20});sendButton->setIcon(tintedIcon("arrow",QColor("#111")));sendButton->setStyleSheet("QPushButton{background:#737373;border-radius:10px} QPushButton:enabled{background:#e4e4e4} QPushButton:hover:enabled{background:#fff}");line->addWidget(sendButton);roomLayout->addWidget(composer);
    connect(qApp,&QApplication::focusChanged,composer,[this](QWidget*,QWidget* now){
        bool focused=now&&(now==composer||composer->isAncestorOf(now));
        if(composer->property("focused").toBool()!=focused){
            composer->setProperty("focused",focused);composer->style()->unpolish(composer);composer->style()->polish(composer);composer->update();
        }
    });
    detailPage=new QWidget;detailLayout=new QVBoxLayout(detailPage);detailLayout->setContentsMargins(0,0,0,0);content->addWidget(detailPage);
    status=text("","font-size:11px;color:#ccc;background:#242424;border-radius:10px;padding:10px");
    status->setParent(content);status->setWordWrap(true);status->setAttribute(Qt::WA_TransparentForMouseEvents);status->hide();
    noticeTimer.setSingleShot(true);connect(&noticeTimer,&QTimer::timeout,status,&QWidget::hide);
    memberPanel=new QWidget(page);memberPanel->setObjectName("membersPanel");
    memberPanel->setAttribute(Qt::WA_StyledBackground);
    auto* side=new QVBoxLayout(memberPanel);side->setContentsMargins(0,0,0,0);side->setSpacing(0);
    side->setSizeConstraint(QLayout::SetNoConstraint);
    memberToggle=new RosterToggle;memberToggle->setObjectName("memberToggle");side->addWidget(memberToggle);
    members=new QListWidget;members->setObjectName("members");members->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    side->addWidget(members,1);
    memberServerLink=new MemberServerLink;side->addWidget(memberServerLink);
    rosterMotion=new QVariantAnimation(this);rosterMotion->setStartValue(0.);rosterMotion->setEndValue(1.);rosterMotion->setDuration(240);
    connect(rosterMotion,&QVariantAnimation::valueChanged,this,[this](const QVariant& value){
        rosterProgress=value.toReal();layoutMemberPanel();
    });
    pages->addWidget(page);
    auto home=[this]{openView("room");for(int i=0;i<channels->count();++i)if(channels->item(i)->flags()&Qt::ItemIsSelectable){channels->setCurrentRow(i);break;}};
    connect(serverBox,&QPushButton::clicked,this,home);
    connect(serverSwitch,&QPushButton::clicked,this,&DesktopWindow::showServers);
    connect(profileButton,&QPushButton::clicked,this,[this]{openView("profile");});connect(settingsButton,&QPushButton::clicked,this,[this]{openView("general");});
    connect(backButton,&QPushButton::clicked,this,[this]{openView("room");});
    connect(memberToggle,&QPushButton::clicked,this,[this]{setMemberPanel(!expandedMembers,true);});
    channels->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(channels,&QListWidget::customContextMenuRequested,this,[this](QPoint pos){
        auto* item=channels->itemAt(pos);if(!item)return;
        const auto id=item->data(Qt::UserRole+2).toJsonObject()["identity"].toString().section('.',0,0);
        if(!id.isEmpty() && id!=selfId)memberMenu(id,channels->viewport()->mapToGlobal(pos));
    });
    connect(channels,&QListWidget::itemSelectionChanged,this,[this]{
        auto selectedItems=channels->selectedItems();if(selectedItems.size()==1&&channels->currentItem()!=selectedItems.front())channels->setCurrentItem(selectedItems.front());
    });
    connect(channels,&QListWidget::itemActivated,this,[this](QListWidgetItem* item){if(!item->data(Qt::UserRole).toJsonObject().isEmpty())openView("room");});
    connect(channels,&QListWidget::currentRowChanged,this,&DesktopWindow::selectChannel);
    connect(channels,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){
        if(item->data(Qt::UserRole).toJsonObject().isEmpty())return;
        openView("room");if(kind=="voice")join();
    });
    connect(attachButton,&QPushButton::clicked,this,[this]{
        auto files=QFileDialog::getOpenFileNames(this,UiText::AttachFiles());if(files.isEmpty())return;
        attachFiles(files);
    });
    connect(emojiButton,&QPushButton::clicked,this,&DesktopWindow::pickEmoji);
    connect(sendButton,&QPushButton::clicked,this,&DesktopWindow::sendMessage);compose->submit=[this]{if(sendButton->isEnabled())sendButton->click();};
    connect(olderButton,&QPushButton::clicked,this,&DesktopWindow::loadOlder);
    connect(emptyRoom,&QPushButton::clicked,this,&DesktopWindow::join);
    connect(joinButton,&QPushButton::clicked,this,&DesktopWindow::leaveCall);
    connect(muteButton,&QPushButton::clicked,this,&DesktopWindow::toggleMute);
    connect(cameraButton,&QPushButton::clicked,this,[this]{chooseVideo(false);});
    connect(screenButton,&QPushButton::clicked,this,[this]{chooseVideo(true);});
    mentions=new QCompleter(this);mentions->setWidget(compose);mentions->setCaseSensitivity(Qt::CaseInsensitive);
    mentions->setModel(new QStringListModel(mentions));mentions->setFilterMode(Qt::MatchContains);
    connect(compose,&QPlainTextEdit::textChanged,this,[this]{
        if(!selected.isEmpty()){
            auto value=compose->draftText();
            mentionDrafts[selected]=MessageText::moveMentions(drafts.value(selected),value,mentionDrafts.value(selected));
            drafts[selected]=value;draftFormats[selected]=compose->formatMask();
        }
        updateComposer();
    });
    connect(compose,&QPlainTextEdit::textChanged,this,[this]{const auto s=compose->draftText();if(!compose->hasFocus())return;
        auto m=QRegularExpression("(?:^|\\s)@([^\\s@]*)$").match(s.left(compose->textCursor().position()));
        if(!m.hasMatch()){mentions->popup()->hide();return;}
        mentions->setCompletionPrefix(m.captured(1));mentions->complete();
    });
    connect(mentions,qOverload<const QString&>(&QCompleter::activated),this,[this](const QString& choice){
        auto pos=compose->textCursor().position();int start=compose->draftText().left(pos).lastIndexOf('@');if(start<0)return;
        auto i=mentionLabels.indexOf(choice);if(i<0)return;
        auto mention=mentionChoices.at(i).toObject();auto tag="@"+mention["label"].toString();
        auto cursor=compose->textCursor();cursor.setPosition(start);cursor.setPosition(pos,QTextCursor::KeepAnchor);compose->setTextCursor(cursor);compose->insertPlainText(tag+" ");mention["start"]=start;mention["end"]=start+tag.size();mentionDrafts[selected].append(mention);
    });
    connect(notifyButton,&QPushButton::clicked,this,[this]{
        QSettings prefs(dataPath+"/desktop.ini",QSettings::IniFormat);
        auto k="notify/"+api.server.host()+"/"+selfId+"/"+selected;
        prefs.setValue(k,!prefs.value(k,false).toBool());selectChannel();
    });
    members->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(members,&QListWidget::customContextMenuRequested,this,[this](QPoint p){if(auto* item=members->itemAt(p))memberMenu(item->data(Qt::UserRole).toString(),members->viewport()->mapToGlobal(p));});
}
void DesktopWindow::updateComposer(){
    int lines=0;
    for(auto block=compose->document()->begin();block.isValid();block=block.next())lines+=qMax(1,block.lineCount());
    int h=qBound(20,lines*compose->fontMetrics().lineSpacing()+2,5*compose->fontMetrics().lineSpacing()+2);
    compose->setFixedHeight(h);composer->setFixedHeight(qMax(56,h+20));
    const QStringList files=fileDrafts.value(selected);
    refreshFileDrafts();
    const auto length=compose->draftText().size();
    characterCount->setText(QString("%1 / 8000").arg(length));characterCount->setVisible(length>=7800);
    attachButton->setEnabled(compose->isEnabled()&&!sending);
    sendButton->setEnabled(!sending&&(!compose->draftText().trimmed().isEmpty()||!files.isEmpty())&&compose->isEnabled());
}
void DesktopWindow::pickEmoji(){
    showEmojiPicker(emojiButton,[this](QString emoji){compose->insertPlainText(emoji);compose->setFocus();});
}
void DesktopWindow::showEmojiPicker(QWidget* anchor,std::function<void(QString)> select){
    auto* menu=new QFrame(this,Qt::Popup);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("emojiPicker");menu->setFixedSize(288,292);
    menu->setStyleSheet("QFrame#emojiPicker{background:#252525;border-radius:16px} QScrollArea,QWidget#emojiGrid{border:0;background:transparent} QPushButton{background:transparent;border:0;border-radius:8px;font-family:'Segoe UI Emoji';font-size:23px;padding:0} QPushButton:hover,QPushButton:focus{background:#444}");
    auto* layout=new QVBoxLayout(menu);layout->setContentsMargins(12,12,12,12);layout->setSpacing(8);
    auto* search=new QLineEdit;search->setPlaceholderText(UiText::FindEmoji());search->setAccessibleName(UiText::FindEmoji());search->setFixedHeight(36);layout->addWidget(search);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);layout->addWidget(scroll);
    auto* gridPanel=new QWidget;gridPanel->setObjectName("emojiGrid");auto* grid=new QGridLayout(gridPanel);grid->setAlignment(Qt::AlignTop|Qt::AlignLeft);grid->setSpacing(4);grid->setContentsMargins(0,0,0,0);scroll->setWidget(gridPanel);
    auto palette=emojiPalette();auto names=QString::fromUtf8(UiText::SmileJoyHappySmileLaughSweatTears()).split('|');
    auto populate=[=,this](QString query){
        clear(grid);int n=0;for(int i=0;i<palette.size();++i){auto e=palette[i];if(!names.value(i).contains(query,Qt::CaseInsensitive)&&query!=e)continue;
            auto* b=new QPushButton(e);b->setFixedSize(39,36);b->setAccessibleName(names.value(i));b->setToolTip(names.value(i));grid->addWidget(b,n/6,n%6);b->show();++n;
            connect(b,&QPushButton::clicked,menu,[menu,select,e]{menu->close();select(e);});
        }if(!n)grid->addWidget(text(UiText::NoResults(),"color:#999;font-size:12px"),0,0,1,6);
    };
    populate({});connect(search,&QLineEdit::textChanged,menu,populate);
    const QRect bounds(mapToGlobal(QPoint()),size());
    QPoint pos=anchor->mapToGlobal(QPoint(0,anchor->height()+8));
    if(pos.y()+menu->height()>bounds.bottom()-8)pos.setY(qMax(bounds.top()+8,anchor->mapToGlobal(QPoint()).y()-menu->height()-8));
    pos.setX(qBound(bounds.left()+8,pos.x(),bounds.right()-menu->width()-8));menu->move(pos);menu->show();search->setFocus();

}
QString DesktopWindow::roleName(const QJsonObject& m) const {
    const auto roles=workspace.value("roles").toArray();
    if(m["role"]=="owner")return byId(roles,"owner").value("name").toString(UiText::Owner());
    QStringList result;auto ids=m["roleIds"].toArray();if(ids.isEmpty()&&m["role"]=="admin")ids.append("admin");
    for(auto id:ids){auto n=byId(roles,id.toString()).value("name").toString();if(!n.isEmpty())result<<n;}
    return result.isEmpty()?byId(roles,"member").value("name").toString(UiText::Member2()):result.join(", ");
}
bool DesktopWindow::allowed(const QString& permission) const {
    return !self["blocked"].toBool()&&(self["role"]=="owner"||self["permissions"].toArray().contains(permission));
}
void DesktopWindow::updateNetworkIndicator(){
    QString color="#7abf95",message=UiText::ConnectionIsHealthy();
    const bool warning=workspaceFailed || callQuality=="lost" || callQuality=="poor";
    if(workspaceFailed){color="#e78080";message=UiText::CouldNotRefreshTheChannelList();}
    else if(callQuality=="lost"){color="#e78080";message=UiText::CallConnectionLost();}
    else if(callQuality=="poor"){color="#dfb76a";message=UiText::UnstableCallConnection();}
    else if(workspaceLatency>700){
        color=workspaceLatency>2000?"#e78080":"#dfb76a";
        message=QString::number(workspaceLatency)+UiText::Ms();
    }else if(workspace.isEmpty()){color="#dfb76a";message=UiText::CheckingConnection();}
    networkDot->setStyleSheet("background:"+color+";border-radius:3px");
    networkDot->setAccessibleName(message);networkDot->setToolTip(message);
    latencyBadge->setText(QString::number(workspaceLatency)+UiText::Ms());
    latencyBadge->setVisible(workspaceLatency>700 && !workspaceFailed);
    networkWarning->setText(message);networkWarning->setVisible(warning);
}
void DesktopWindow::translateSidebar(){
    for(int i=0;i<channels->count();++i){
        auto* item=channels->item(i);
        auto* widget=channels->itemWidget(item);
        const auto type=item->data(Qt::UserRole+4).toString();
        if(widget && !type.isEmpty()){
            const QString caption=type=="text"?UiText::TextChannels():UiText::VoiceChannels();
            item->setData(Qt::AccessibleTextRole,caption);
            if(auto* text=widget->findChild<QLabel*>("sectionCaption"))text->setText(caption);
        }
        const auto person=item->data(Qt::UserRole+2).toJsonObject();
        if(!person.isEmpty() && person["identity"].toString().section('.',0,0)==selfId)
            item->setText(person["name"].toString()+UiText::You());
        if(auto* leave=qobject_cast<QPushButton*>(widget);leave && leave->objectName()=="channelLeave"){
            const QString tip=UiText::LeaveRoom()+item->text();
            leave->setAccessibleName(tip);leave->setToolTip(tip);
        }
        if(item->data(Qt::UserRole+6).toLongLong()>0)item->setToolTip(UiText::RoomActivityTime());
    }
    if(!workspace.isEmpty() && workspace["description"].toString().isEmpty())serverDescription->setText(UiText::APlaceForFriends());
}

void DesktopWindow::renderWorkspace(const QJsonObject& incoming,WorkspaceRefresh refresh){
    QJsonObject data=incoming;
    if(!endedVoiceSessions.isEmpty()){
        auto presence=data["presence"].toObject();
        for(auto it=presence.begin();it!=presence.end();++it){
            auto channel=it.value().toObject();
            auto people=channel["people"].toArray();
            for(int i=people.size()-1;i>=0;--i){
                if(endedVoiceSessions.contains({api.server.toString(),people[i].toObject()["identity"].toString()}))
                    people.removeAt(i);
            }
            channel["people"]=people;it.value()=channel;
        }
        data["presence"]=presence;
    }
    serverSwitch->setVisible(profile["servers"].toArray().size()>1);
    const auto previous=workspace;workspace=data;updateNetworkIndicator();
    if(previous["members"]!=data["members"] || previous["roles"]!=data["roles"] || previous["selfId"]!=data["selfId"])
        stageSignature.clear();
    selfId=data["selfId"].toString();auto roster=data["members"].toArray();self=byId(roster,selfId);
    syncUpdateContext();
    api.syncMedia(dataPath+"/media-cache-v1",selfId);
    serverTitle->setText(data["name"].toString());serverDescription->setText(data["description"].toString());
    if(serverDescription->text().isEmpty())serverDescription->setText(UiText::APlaceForFriends());
    if(previous.isEmpty() || previous["name"]!=data["name"] || previous["avatar"]!=data["avatar"])
        serverBadge->setPixmap(avatar(serverTitle->text(),imageData(data["avatar"].toString()),34,true));
    if(!profile.value("avatar").toString().isEmpty()&&!avatars.contains(selfId))avatars[selfId]=imageData(profile.value("avatar").toString());
    if(refresh==WorkspaceRefresh::All || previous.isEmpty() || previous["selfId"]!=selfId || profileName->text()!=self["name"].toString() || previous["members"]!=data["members"]){
        profileName->setText(self["name"].toString());selfAvatar->setPixmap(avatar(self["name"].toString(),avatars.value(selfId),34));
    }
    if(view=="roles" && previous["members"]!=data["members"])updateSettingsTitle(UiText::MembersAndRoles());
    if(view=="roles" && detailLayout->count() && previous["invitations"]!=data["invitations"])
        if(auto* panel=detailLayout->itemAt(0)->widget()->findChild<QWidget*>("invitationSettings"))
            static_cast<InvitationSettings*>(panel)->sync(data["invitations"].toArray());
    if(view=="roles" && detailLayout->count() && (previous["members"]!=data["members"] || previous["roles"]!=data["roles"]))
        if(auto* panel=detailLayout->itemAt(0)->widget()->findChild<QWidget*>("memberSettings"))
            static_cast<MemberSettings*>(panel)->render();
    if(!displayMove.identity.isEmpty()){
        auto contains=[this](QString room){
            const auto people=rawRoomPeople(room);
            return std::any_of(people.begin(),people.end(),[this](const auto& p){return p.toObject()["identity"]==displayMove.identity;});
        };
        if(displayMove.origin!=api.server.toString() || (contains(displayMove.to) && !contains(displayMove.from))){
            displayMove={};moveDisplayTimer.stop();refresh=WorkspaceRefresh::Channels;
        }
    }
    auto presence=data["presence"].toObject();
    if(refresh!=WorkspaceRefresh::Changed||previous["channels"]!=data["channels"]||previous["sections"]!=data["sections"]||previous["sectionOrder"]!=data["sectionOrder"]||
       previous["channelSections"]!=data["channelSections"]||previous["presence"]!=data["presence"]){
        rooms=data["channels"].toArray();QSignalBlocker block(channels);channels->clear();int next=-1,first=-1;bool hasActivity=false;
        auto groups=data["sections"].toArray(),order=data["sectionOrder"].toArray();
        if(order.isEmpty())order={"text","voice"};
        for(auto groupId:order){
            QString id=groupId.toString();auto group=byId(groups,id);QString type=group["kind"].toString(id);
            const int gap=channels->count()==0?0:22;
            auto* heading=new QListWidgetItem(channels);
            const QString title=group["name"].toString(type=="text"?UiText::TextChannels():UiText::VoiceChannels());
            heading->setData(Qt::AccessibleTextRole,title);
            if(!group["name"].isString())heading->setData(Qt::UserRole+4,type);
            heading->setFlags(Qt::NoItemFlags);
            heading->setSizeHint({0,gap+19});
            auto* labelRow=new QWidget;
            auto* labels=new QHBoxLayout(labelRow);
            labels->setContentsMargins(0,gap,0,5);labels->setSpacing(5);
            auto* mark=text("");
            mark->setFixedSize(12,12);mark->setPixmap(tintedIcon(type=="text"?"section-hash":"sound",QColor("#737373")).pixmap(12,12));
            labels->addWidget(mark);
            auto* caption=text(title,"color:#737373;font-size:10px");caption->setObjectName("sectionCaption");
            caption->setAlignment(Qt::AlignLeft|Qt::AlignTop);
            labels->addWidget(caption);labels->addStretch();
            labelRow->setStyleSheet("background:transparent");channels->setItemWidget(heading,labelRow);
            bool firstInSection=true;
            for(auto value:rooms){
                auto room=value.toObject();auto rid=room["id"].toString();
                if(data["channelSections"].toObject().value(rid).toString(room["section"].toString(room["kind"].toString()))!=id)continue;
                QString ico=room["icon"].toString();if(ico.isEmpty())ico=type=="text"?"hash":"sound";
                if(!firstInSection){auto* gapItem=new QListWidgetItem(channels);gapItem->setFlags(Qt::NoItemFlags);gapItem->setSizeHint({0,6});}
                firstInSection=false;
                auto channelIcon=tintedIcon(ico,QColor("#a0a0a0"));
                channelIcon.addPixmap(tintedIcon(ico,QColor("#eeeeee")).pixmap(18,18),QIcon::Selected);
                auto* item=new QListWidgetItem(channelIcon,room["name"].toString(),channels);
                item->setSizeHint({0,45});item->setData(Qt::UserRole,room);if(first<0)first=channels->row(item);
                if(rid==selected)next=channels->row(item);
                const auto activity=presence[rid].toObject();
                const auto people=roomPeople(rid);
                const bool canLeave=!leavingVoice&&(inCall||joining)&&activeVoice==rid&&callOrigin==api.server.toString();
                const auto since=activity["since"].toInteger();
                if(since>0 && !people.isEmpty()){
                    item->setData(Qt::UserRole+6,since);
                    item->setToolTip(UiText::RoomActivityTime());
                    hasActivity=true;
                }
                if(canLeave){
                    auto* leave=action("exit",UiText::LeaveRoom()+room["name"].toString(),30);
                    leave->setObjectName("channelLeave");leave->setHoverIcons(tintedIcon("exit",QColor("#a0a0a0")),tintedIcon("exit",QColor("#f4b5b5")));leave->setStyleSheet("QPushButton{background:transparent;border:0;border-radius:8px;padding:0} QPushButton:hover{background:#39282a}");
                    item->setData(Qt::UserRole+3,true);
                    channels->setItemWidget(item,leave);
                    connect(leave,&QPushButton::clicked,leave,[this]{leaveCall();});
                }
                for(auto p:people){
                    auto person=p.toObject();auto name=person["name"].toString();auto mid=person["identity"].toString().section('.',0,0);
                    const auto photo=avatars.value(mid);
                    auto* entry=new QListWidgetItem(photo.isNull()?QIcon{}:QIcon(avatar(name,photo,22)),name+(mid==selfId?UiText::You():""),channels);
                    entry->setData(Qt::UserRole+2,person);entry->setData(Qt::UserRole+5,rid);entry->setFlags(Qt::NoItemFlags);entry->setSizeHint({0,30});
                }
                if(!people.isEmpty()){
                    auto* space=new QListWidgetItem(channels);space->setFlags(Qt::NoItemFlags);space->setSizeHint({0,8});space->setData(Qt::UserRole+5,rid);
                }
            }
        }
        static_cast<VoiceChannelList*>(channels)->setActivityVisible(hasActivity);
        channels->setCurrentRow(next<0?first:next);selectChannel();
    }
    const bool rebuildMembers=refresh==WorkspaceRefresh::All||previous["selfId"]!=selfId||previous["members"]!=data["members"]||previous["roles"]!=data["roles"];
    if(rebuildMembers && !awaitingMemberRoster){
        members->clear();mentionChoices={};mentionLabels.clear();
        auto choice=[this](QString kind,QString id,QString label){mentionChoices.append(QJsonObject{{"kind",kind},{"id",id},{"label",label}});mentionLabels<<label;};
        choice("everyone","everyone","everyone");
        for(auto v:data["roles"].toArray()){auto role=v.toObject();if(role["id"]!="owner"&&(self["role"]=="owner"||(!role["permissions"].toArray().contains("manage_roles")&&role["id"]!="admin")))choice("role",role["id"].toString(),role["name"].toString());}
        for(auto v:roster){
            auto person=v.toObject();auto id=person["id"].toString(),name=person["name"].toString();
            if(id!=selfId&&!person["blocked"].toBool())choice("member",id,name);
            auto* item=new QListWidgetItem(members);item->setData(Qt::UserRole,id);item->setSizeHint({0,53});
            auto* row=new QWidget;row->setAttribute(Qt::WA_TransparentForMouseEvents);row->setStyleSheet("background:transparent");
            auto* h=new QHBoxLayout(row);h->setContentsMargins(16,0,16,0);h->setSpacing(10);
            auto* image=new MemberAvatar;image->setPixmap(avatar(name,avatars.value(id),34));h->addWidget(image);
            auto* labels=new QVBoxLayout;labels->setSpacing(2);labels->setAlignment(Qt::AlignVCenter);labels->addWidget(text(name+(id==selfId?UiText::You():""),"font-size:12px;font-weight:500"));
            labels->addWidget(text(roleName(person),"font-size:10px;color:#737373"));h->addLayout(labels,1);members->setItemWidget(item,row);
        }
        static_cast<QStringListModel*>(mentions->model())->setStringList(mentionLabels);
    }
    if(!awaitingMemberRoster && (rebuildMembers || previous["onlineMembers"]!=data["onlineMembers"]))
        updateMemberPresence(data["onlineMembers"].toArray());
    auto* toggle=static_cast<RosterToggle*>(memberToggle);toggle->count=roster.size();
    toggle->setAccessibleName(QString(UiText::Members1()).arg(roster.size()));toggle->update();
    updateChatSound(previous.isEmpty() || previous["selfId"]!=data["selfId"]);
    updateMentionIndicators();refreshStage();updateAvatars();
}
void DesktopWindow::updateMemberPresence(const QJsonArray& online){
    QSet<QString> ids;
    for(const auto& value:online)ids.insert(value.toString());
    for(int i=0;i<members->count();++i){
        auto* item=members->item(i);
        auto* row=members->itemWidget(item);
        if(auto* avatar=row?row->findChild<QLabel*>("memberAvatar",Qt::FindDirectChildrenOnly):nullptr)
            static_cast<MemberAvatar*>(avatar)->setOnline(ids.contains(item->data(Qt::UserRole).toString()));
    }
}
void DesktopWindow::updateAvatars(){
    QSet<QString> present;
    for(auto item:workspace.value("members").toArray()){
        auto person=item.toObject();auto id=person["id"].toString(),version=person["avatarVersion"].toString();
        present.insert(id);
        if(id.isEmpty()||avatarVersions.value(id)==version)continue;
        avatarVersions[id]=version;avatars.remove(id);
        if(version.isEmpty())continue;
        api.bytes("/api/avatars/"+QString::fromLatin1(QUrl::toPercentEncoding(id)),1024*1024,[this,id,version](QByteArray bytes,QString error){
            if(avatarVersions.value(id)!=version)return;
            if(!error.isEmpty()){avatarVersions.remove(id);return;}
            QPixmap pix;if(!pix.loadFromData(bytes))return;avatars[id]=pix;stageSignature.clear();renderWorkspace(workspace,WorkspaceRefresh::All);updateMessageRows();
        });
    }
    avatarVersions.removeIf([&](QHash<QString,QString>::iterator it){return !present.contains(it.key());});
    avatars.removeIf([&](QHash<QString,QPixmap>::iterator it){return !present.contains(it.key());});
    if(gameOverlay && callOrigin==api.server.toString())gameOverlay->syncPortraits(&avatars,callStateApi);
}
void DesktopWindow::setMemberPanel(bool expanded,bool animate){
    expandedMembers=expanded;memberToggle->setChecked(expanded);
    QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("membersExpanded",expanded);
    if(!expanded && (members->hasFocus() || memberServerLink->isAncestorOf(QApplication::focusWidget())))memberToggle->setFocus();
    members->setEnabled(expanded);
    memberServerLink->setEnabled(expanded);
    rosterMotion->setDirection(expanded?QAbstractAnimation::Forward:QAbstractAnimation::Backward);
    if(animate && Ui::animationsEnabled()){
        if(rosterMotion->state()!=QAbstractAnimation::Running)rosterMotion->start();
    }else{
        rosterMotion->stop();rosterMotion->setCurrentTime(expanded?240:0);
        rosterProgress=expanded?1.:0.;layoutMemberPanel();
    }
}
void DesktopWindow::layoutMemberPanel(){
    if(viewer && viewer->isVisible())viewer->position(view=="room" && expandedMembers);
    auto smooth=[](qreal x){x=qBound(0.,x,1.);return x*x*(3.-2.*x);};
    const bool room=view=="room";
    const int stageInset=width()>800?32:12;
    const auto columnFraction=smooth(2*rosterProgress);
    emptyRoom->parentWidget()->layout()->setContentsMargins(stageInset,2,qRound(stageInset*columnFraction),16);
    const int column=room?qRound(218*columnFraction):0;
    auto* page=memberPanel->parentWidget();
    page->layout()->setContentsMargins(8,8,8+column,8);
    const int inset=width()>=1700?52:width()<=1200?17:32;
    headerLayout->setContentsMargins(room?stageInset:inset,0,room?222-column:inset,0);
    memberPanel->setVisible(room);
    if(!room)return;
    const int height=62+qRound(qMax(0,page->height()-16-62)*smooth(2*rosterProgress-1));
    memberPanel->setGeometry(page->width()-218,8,210,height);memberPanel->raise();
    members->setVisible(height>62);
    memberServerLink->setServer(api.server);
    memberServerLink->setVisible(height>=114);
    auto* toggle=static_cast<RosterToggle*>(memberToggle);toggle->progress=rosterProgress;toggle->update();
}
QJsonObject DesktopWindow::localPerson() const {
    return {{"identity",callIdentity},{"name",self["name"]},{"micKnown",true},{"mic",!muted},
            {"speaking",localSpeaking&&!muted},{"camera",cameraMode!=0},{"screen",screenMode!=0}};
}
void DesktopWindow::updateLocalPerson(){
    if(gameOverlay)syncGameOverlay();
    if(!inCall || callOrigin!=api.server.toString())return;
    updateSpeakingPerson(localPerson());
}
void DesktopWindow::updateSpeakingPerson(const QJsonObject& person){
    const auto identity=person["identity"].toString();
    for(int i=0;i<channels->count();++i){
        auto* item=channels->item(i);
        if(item->data(Qt::UserRole+2).toJsonObject()["identity"]==identity){
            item->setData(Qt::UserRole+2,person);
            break;
        }
    }
    if(auto card=frameHosts.value(identity+"/camera"))card->setMicrophone(person["speaking"].toBool() && person["mic"].toBool(),person["micKnown"].toBool()&&!person["mic"].toBool());
}
bool DesktopWindow::canDragVoice(const QString& identity) const{
    auto member=byId(workspace["members"].toArray(),identity.section('.',0,0));
    return displayMove.identity.isEmpty() && allowed("move_members") && identity.contains('.') &&
           !member.isEmpty() && !member["blocked"].toBool();
}
void DesktopWindow::requestVoiceMove(QString id,QString from,QString to){
    if(!canDragVoice(id) || from==to || byId(rooms,to)["kind"]!="voice")return;
    const auto origin=api.server.toString();
    for(const auto value:rawRoomPeople(from)){
        auto person=value.toObject();
        if(person["identity"]!=id)continue;
        displayMove={origin,id,from,to,person};moveDisplayTimer.start(15000);
        renderWorkspace(workspace,WorkspaceRefresh::Channels);
        api.request("POST","/api/voice-move",{{"identity",id},{"from",from},{"to",to}},
            [this,origin,id,from,to](QJsonObject,QString error){
            if(error.isEmpty() || origin!=api.server.toString())return;
            if(displayMove.identity==id && displayMove.from==from && displayMove.to==to){
                displayMove={};moveDisplayTimer.stop();renderWorkspace(workspace,WorkspaceRefresh::Channels);
            }
            note(error);
        });
        return;
    }
}
QJsonArray DesktopWindow::roomPeople(const QString& room) const{
    auto people=rawRoomPeople(room);
    if(displayMove.identity.isEmpty() || displayMove.origin!=api.server.toString())return people;
    auto person=displayMove.person;
    for(int i=people.size()-1;i>=0;--i){
        if(people[i].toObject()["identity"]==displayMove.identity){person=people[i].toObject();people.removeAt(i);}
    }
    if(room==displayMove.to)people.append(person);
    return people;
}
QJsonArray DesktopWindow::rawRoomPeople(const QString& room) const{
    if(!inCall || callOrigin!=api.server.toString() || activeVoice!=room)
    {
        auto people=workspace["presence"].toObject().value(room).toObject()["people"].toArray();
        if(inCall && callOrigin==api.server.toString())
            for(int i=people.size()-1;i>=0;--i)if(people[i].toObject()["identity"]==callIdentity)people.removeAt(i);
        return people;
    }
    QJsonArray people;
    if(!leavingVoice)people.append(localPerson());
    for(const auto entry:liveRoster){
        const auto id=entry.toObject()["identity"].toString();
        if(id!=callIdentity && !endedVoiceSessions.contains({callOrigin,id}))people.append(entry);
    }
    return people;
}
void DesktopWindow::addMediaAction(VoiceCard* card,const QString& id,const QString& op,const QString& caption,const QString& glyph){
    auto* button=action(glyph,caption,32);button->setObjectName("mediaModeration");button->setEnabled(!mediaModerationBusy);
    card->addModeration(button);
    connect(button,&QPushButton::clicked,this,[this,id,op]{
        if(mediaModerationBusy)return;
        mediaModerationBusy=true;
        for(auto* b:stageCards->findChildren<QPushButton*>("mediaModeration"))b->setEnabled(false);
        api.request("POST","/api/members/"+QString::fromLatin1(QUrl::toPercentEncoding(id))+"/moderate",{{"action",op}},
            [this](QJsonObject,QString issue){
                mediaModerationBusy=false;
                for(auto* b:stageCards->findChildren<QPushButton*>("mediaModeration"))b->setEnabled(true);
                note(issue);if(issue.isEmpty())refresh();
            });
    });
}
void DesktopWindow::refreshStage(){
    if(kind!="voice"||view!="room"){
        if(cardLayout->count()){clear(cardLayout);frameHosts.clear();stageSignature.clear();}
        return;
    }
    const bool local=inCall&&!leavingVoice&&callOrigin==api.server.toString()&&selected==activeVoice;
    const bool connecting=(joining&&!leavingVoice&&selected==activeVoice&&callOrigin==api.server.toString())
        || (selected==pendingVoiceRoom && pendingVoiceOrigin==api.server.toString());
    const auto people=roomPeople(selected);
    roomCount->setText(QString::number(people.size()));roomCount->show();
    emptyRoom->setVisible(!local&&people.isEmpty());emptyRoom->setEnabled(!connecting);
    const bool showProgress=connecting && joinProgress.isValid() && joinProgress.elapsed()>=500;
    emptyRoom->setText(showProgress?UiText::ConnectingToTheConversation():UiText::JoinTheConversation());
    auto* viewport=static_cast<QScrollArea*>(stage)->viewport();const auto margins=emptyRoom->parentWidget()->layout()->contentsMargins();int avail=qMax(1,viewport->width()-margins.left()-margins.right());bool narrow=width()<=700;auto signature=QString::number(avail)+":"+QString::fromUtf8(QJsonDocument(people).toJson(QJsonDocument::Compact))+selected+QString::number(local)+QString::number(connecting)+QString::number(muted);
    if(signature==stageSignature)return;stageSignature=signature;
    clear(cardLayout);frameHosts.clear();QHBoxLayout* row=nullptr;int used=0;int gap=narrow?14:20;cardLayout->setSpacing(gap);
    auto place=[&](QWidget* group,int w){if(!row||narrow||used+w+gap>avail){row=new QHBoxLayout;row->setContentsMargins(0,0,0,0);row->setSpacing(gap);row->setAlignment(Qt::AlignLeft|Qt::AlignTop);cardLayout->addLayout(row);used=0;}row->addWidget(group,0,Qt::AlignTop);used+=w+(used?gap:0);};
    for(auto v:people){
        const auto person=v.toObject();
        const auto identity=person["identity"].toString();
        const auto id=identity.section('.',0,0);
        const auto name=person["name"].toString();
        const bool mine=local && identity==callIdentity;
        const bool speak=person["speaking"].toBool() && person["mic"].toBool();
        QString camera=identity+"/camera",screen=identity+"/screen";bool cam=local&&trackFrames.contains(camera),share=local&&trackFrames.contains(screen);
        int groupW=share?qMin(792,avail):qMin(cam?(narrow?avail:380):230,avail);int cardW=share?qMax(1,(groupW-20)/2):groupW;
        auto* tile=new VoiceCard(cardW,name,id==selfId,speak,person["micKnown"].toBool()&&!person["mic"].toBool(),avatars.value(id),cam,false,share,narrow);
        tile->setProperty("connectionIdentity",identity);
        if(cam)tile->setFrame(trackFrames.value(camera));frameHosts[camera]=tile;
        if(local){auto* act=action(mine?"exit":"headphones",mine?QString(UiText::LeaveVoiceChannel()):UiText::Volume2()+name,32);
            if(mine){act->setIcon(tintedIcon("exit",QColor("#efaaaa")));act->setStyleSheet("QPushButton{background:#302124;border:0;border-radius:9px;padding:0} QPushButton:hover{background:#452a2e}");}
            else act->setIconSize({15,15});
            tile->addOverlay(act);
            connect(act,&QPushButton::clicked,tile,[this,mine,id,act]{if(mine)leaveCall();else memberMenu(id,act->mapToGlobal(QPoint(0,32)));});}
        tile->setContextMenuPolicy(Qt::CustomContextMenu);connect(tile,&QWidget::customContextMenuRequested,tile,[this,tile,id](QPoint p){memberMenu(id,tile->mapToGlobal(p));});
        const auto member=byId(workspace["members"].toArray(),id);
        const bool canModerate=local && allowed("kick_members") && !member.isEmpty() && id!=selfId && member["role"]!="owner"
            && (self["role"]=="owner" || !member["permissions"].toArray().contains("manage_roles"));
        if(canModerate && person["mic"].toBool())addMediaAction(tile,id,"mute_mic",UiText::MuteMicrophone2()+name,"mic-off");
        if(canModerate && cam)addMediaAction(tile,id,"mute_camera",UiText::TurnOffCamera2()+name,"camera-off");
        if(share){auto* group=new QWidget;group->setFixedWidth(groupW);group->setObjectName("shareGroup");group->setStyleSheet("QWidget#shareGroup{background:#15191b;border-radius:22px}");auto* duo=new QHBoxLayout(group);duo->setContentsMargins(6,6,6,6);duo->setSpacing(8);duo->addWidget(tile,0,Qt::AlignTop);
            auto* shareCard=new VoiceCard(cardW,name,false,false,false,{},true,true,true);
            shareCard->setFrame(trackFrames[screen]);frameHosts[screen]=shareCard;
            auto* expand=action("expand",UiText::ExpandScreen()+name,32);shareCard->addOverlay(expand,true);
            auto* sound=action("sound-off",UiText::StreamAudio(),32);
            sound->setObjectName("streamCardSound");shareCard->addOverlay(sound,true);
            sound->move(expand->x()-sound->width()-5,expand->y());
            connect(sound,&QPushButton::clicked,shareCard,[this,screen]{toggleStreamSound(screen);});
            if(canModerate)addMediaAction(shareCard,id,"mute_screen",UiText::StopScreenSharing()+name,"screen-off");
            connect(expand,&QPushButton::clicked,shareCard,[this,screen,name]{viewTrack(screen,name);});
            duo->addWidget(shareCard,0,Qt::AlignTop);place(group,groupW);}
        else place(tile,cardW);
    }
    refreshStreamSound();
    if(!local&&!people.isEmpty()){auto* enter=new QPushButton(showProgress?UiText::ConnectingToTheConversation():UiText::Join());enter->setEnabled(!connecting);enter->setIcon(icon("plus"));enter->setFixedSize(qMin(230,avail),190);place(enter,enter->width());connect(enter,&QPushButton::clicked,this,&DesktopWindow::join);}
}
bool DesktopWindow::eventFilter(QObject* o,QEvent* event){
    if(o==content && event->type()==QEvent::Resize && !status->isHidden())positionNotice();
    if((event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove || event->type()==QEvent::Drop) && kind=="text" && view=="room"){
        auto* target=qobject_cast<QWidget*>(o);
        auto* drop=static_cast<QDropEvent*>(event);
        if(target && (target==roomPage||roomPage->isAncestorOf(target)) && drop->mimeData()->hasUrls()){
            QStringList files;
            for(const auto& url:drop->mimeData()->urls())if(url.isLocalFile())files<<url.toLocalFile();
            if(!files.isEmpty()){
                if(compose->isEnabled()&&!sending){
                    drop->setDropAction(Qt::CopyAction);drop->accept();
                    if(event->type()==QEvent::Drop){dropOverlay->hide();attachFiles(files);}
                    else {dropOverlay->setGeometry(roomPage->rect());dropOverlay->show();dropOverlay->raise();}
                }else drop->ignore();
                return true;
            }
        }
    }
    if(event->type()==QEvent::DragLeave && dropOverlay->isVisible())dropOverlay->hide();
    if(shortcutEvent(o,event))return true;
    if(event->type()==QEvent::Resize&&o->objectName()=="callViewport"&&kind=="voice"&&view=="room")QTimer::singleShot(0,this,[this]{refreshStage();});
    return QMainWindow::eventFilter(o,event);
}
void DesktopWindow::frameArrived(QString id,bool screen,const QImage& frame){
    if(!inCall)return;if(id.isEmpty())id=callIdentity;QString key=id+(screen?"/screen":"/camera");
    bool had=trackFrames.contains(key);if(frame.isNull())trackFrames.remove(key);else trackFrames[key]=frame;
    if(screen){
        if(viewer && viewer->track==key){
            if(frame.isNull())viewer->clear();else viewer->setFrame(frame);
        }
        if(!had && !frame.isNull())syncViewer(true);
    }
    if(view!="room"||kind!="voice"||selected!=activeVoice||callOrigin!=api.server.toString())return;
    if(had!=!frame.isNull()){stageSignature.clear();refreshStage();}
    if(auto host=frameHosts.value(key);host&&!frame.isNull())host->setFrame(frame);
}
void DesktopWindow::displayInk(const ScreenInk::Strokes& strokes){
    if(ownInkSession.isEmpty() || strokes.isEmpty()){presenterInk.reset();return;}
    if(!presenterInk){
        QScreen* screen=nullptr;
        for(auto* candidate:QGuiApplication::screens())if(candidate->name()==inkMonitor)screen=candidate;
        if(!screen && !inkProgram)return;
        presenterInk=std::make_unique<PresenterInk>(screen,reinterpret_cast<HWND>(inkProgram));
    }
    presenterInk->strokes=strokes;
    presenterInk->refresh();
}
void DesktopWindow::toggleStreamSound(const QString& key){
    if(!media || !inCall)return;
    if(key==callIdentity+"/screen"){
        if((screenMode!=3 && screenMode!=4) || screenAudioPending)return;
        screenAudioPending=true;
        media->setScreenAudio(!sharingSound);
    }else{
        const QString peer=key.chopped(7);
        const bool mute=!mutedStreams.contains(peer);
        if(mute)mutedStreams.insert(peer);else mutedStreams.remove(peer);
        media->muteStreamPlayback(peer,mute);
    }
    refreshStreamSound();
}
void DesktopWindow::refreshStreamSound(){
    const auto own=callIdentity+"/screen";
    const bool available=inCall && (screenMode==3 || screenMode==4) && !screenAudioPending;
    const QString ownAction=sharingSound?UiText::TurnOffSharedAudio():screenMode==4?UiText::ShareWindowAudio():UiText::ShareSystemAudio();
    auto apply=[](QPushButton* button,bool enabled,bool on,const QString& label){
        button->setEnabled(enabled);button->setIcon(icon(on?"sound":"sound-off"));
        button->setAccessibleName(label);button->setToolTip(label);
    };
    apply(screenSoundButton,available,sharingSound,ownAction);
    for(auto it=frameHosts.cbegin();it!=frameHosts.cend();++it){
        if(!it.value() || !it.key().endsWith("/screen"))continue;
        auto* button=it.value()->findChild<QPushButton*>("streamCardSound");
        if(!button)continue;
        const bool local=it.key()==own;
        const bool on=local?sharingSound:!mutedStreams.contains(it.key().chopped(7));
        apply(button,local?available:inCall,on,local?ownAction:on?UiText::MuteStreamForMe():UiText::UnmuteStreamForMe());
    }
    if(viewer && !viewer->track.isEmpty()){
        const bool local=viewer->track==own;
        const bool on=local?sharingSound:!mutedStreams.contains(viewer->track.chopped(7));
        viewer->setSound(local?available:inCall,on,local?ownAction:on?UiText::MuteStreamForMe():UiText::UnmuteStreamForMe());
    }
}
void DesktopWindow::viewTrack(QString key,QString name,bool mini){
    if(!trackFrames.contains(key))return;
    if(!viewer)viewer=new StreamViewer(this,[this]{toggleStreamSound(viewer->track);},[this]{
        QWidget* area=messages->isVisible()?messages->viewport():content->currentWidget();
        return QRect(area->mapTo(this,QPoint()),area->size());
    });
    viewer->restoreInkColor(profile.value("screenInkColor").toString());
    viewer->onInkColorChanged=[this](const QString& color){
        profile["screenInkColor"]=color;
        saveLocal();
    };
    viewer->open(key,name,trackFrames[key],mini,key==callIdentity+"/screen",!mini);
    viewer->onInk=[this](QString host,QJsonObject command){if(media)media->sendInk(host,command);};
    viewer->setInkTransport(inkTransport);
    if(key==callIdentity+"/screen")viewer->setInkSession(ownInkSession,true);
    else if(media)media->sendInk(key.chopped(7),{{"type","query"}});
    viewer->position(view=="room" && expandedMembers);
    refreshStreamSound();
}
void DesktopWindow::syncViewer(bool newScreen){
    const bool watching=inCall && view=="room" && kind=="voice" && selected==activeVoice && callOrigin==api.server.toString();
    const bool hasViewer=viewer && !viewer->track.isEmpty();
    if(hasViewer){
        if(!trackFrames.contains(viewer->track) || (watching && !wasViewingCall && viewer->mini))viewer->clear();
        else if(!watching && wasViewingCall)viewer->setMini(true);
    }else if(inCall && !watching && (wasViewingCall || (newScreen && callOrigin!=api.server.toString()))){
        for(auto p:liveRoster){
            const auto person=p.toObject();
            const auto key=person["identity"].toString()+"/screen";
            if(trackFrames.contains(key)){viewTrack(key,person["name"].toString(),true);break;}
        }
    }
    wasViewingCall=watching;
    if(viewer && viewer->isVisible()){
        viewer->position(view=="room" && expandedMembers);
    }
}
void DesktopWindow::relayoutChat(){
    auto* scroll=messages->verticalScrollBar();
    const bool bottom=scroll->value()==scroll->maximum();
    auto* first=messages->itemAt(1,0);
    const int top=first?messages->visualItemRect(first).top():0;
    sizeMessageRows();
    messages->doItemsLayout();
    if(bottom)messages->scrollToBottom();
    else if(first)scroll->setValue(scroll->value()+messages->visualItemRect(first).top()-top);
}
void DesktopWindow::sizeMessageRows(){
    const int w=messages->viewport()->width();
    for(int i=0;i<messages->count();++i){
        auto* item=messages->item(i);
        if(auto* row=messages->itemWidget(item)){
            auto* layout=row->layout();
            auto invalidate=[](auto&& self,QLayout* l)->void{
                for(int j=0;j<l->count();++j)if(auto* nested=l->itemAt(j)->layout())self(self,nested);
                l->invalidate();
            };
            invalidate(invalidate,layout);row->ensurePolished();
            int height=layout->hasHeightForWidth()?layout->totalHeightForWidth(w):layout->totalSizeHint().height();
            height=qMax(height,layout->totalMinimumSize().height());

            item->setSizeHint({w,qMax(66,height)});
        }
    }
}
void DesktopWindow::resizeEvent(QResizeEvent* event){
    QMainWindow::resizeEvent(event);
    if(switchNotice && switchNotice->isVisible())switchNotice->move((width()-switchNotice->width())/2,16);
    QTimer::singleShot(0,this,[this]{layoutForWidth();relayoutChat();});
}
void DesktopWindow::layoutForWidth(){
    const bool medium=width()<=1200;
    int inset=width()>=1700?52:medium?17:32;
    layoutMemberPanel();
    const int chatInset=width()<=800?12:32;
    if(messages->property("contentInset").toInt()!=chatInset){
        messages->setProperty("contentInset",chatInset);
        messages->setStyleSheet(QString("QListWidget#messages{padding:0 4px 0 %1px} "
            "QListWidget#messages QScrollBar:vertical{width:4px}").arg(chatInset));
    }
    if(view!="profile"&&view!="room"){
        for(auto* page:content->currentWidget()->findChildren<QWidget*>("settingsPage")){
            if(!detailLayout->count()||!detailLayout->itemAt(0)->widget()->isAncestorOf(page))continue;
            const bool horizontalTabs=width()<=1050;
            auto* columns=static_cast<QBoxLayout*>(page->layout());
            auto* host=detailLayout->itemAt(0)->widget();
            auto* frame=static_cast<QHBoxLayout*>(host->layout());
            auto* nav=host->findChild<QWidget*>("settingsNavPanel");
            auto* section=page->findChild<QWidget*>("settingsSection");
            if(!nav||!section)continue;
            if(horizontalTabs && nav->parentWidget()!=page){
                columns->insertWidget(0,nav);nav->show();
            }else if(!horizontalTabs && nav->parentWidget()!=host){
                frame->insertWidget(0,nav,0,Qt::AlignTop);nav->show();
            }
            frame->setContentsMargins(horizontalTabs?0:inset,0,0,0);
            columns->setContentsMargins(horizontalTabs?inset:0,26,inset,24);
            columns->setSpacing(horizontalTabs?25:0);
            nav->setMinimumWidth(horizontalTabs?0:170);nav->setMaximumWidth(horizontalTabs?QWIDGETSIZE_MAX:170);
            auto* tabs=static_cast<WrapLayout*>(nav->layout());tabs->setStacked(!horizontalTabs);
            tabs->setContentsMargins(0,horizontalTabs?0:42,0,0);
            for(auto* tab:nav->findChildren<QPushButton*>())tab->setFixedHeight(horizontalTabs?41:45);
            section->setMaximumWidth(horizontalTabs?QWIDGETSIZE_MAX:680);
        }
    }
    if(auto* page=detailPage->findChild<QWidget*>("profileContents"))page->layout()->setContentsMargins(inset,26,inset,24);

    if(auto* list=findChild<QWidget*>("profileServers"))list->setFixedWidth(width()<=1150?170:190);
    if(auto* details=findChild<QWidget*>("profileDetails"))details->setMaximumWidth(width()<=1150?QWIDGETSIZE_MAX:680);
}
void DesktopWindow::refreshDock(){
    syncViewer();
    const bool foreign=inCall && callOrigin!=api.server.toString();
    const bool compact=kind=="text" || foreign;
    if(inCall&&compact&&dock->parentWidget()!=header){headerLayout->insertWidget(headerLayout->indexOf(backButton),dock);}
    if((!compact||!inCall)&&dock->parentWidget()!=roomPage){roomLayout->insertWidget(roomLayout->indexOf(replyBanner),dock);}
    dock->setVisible(!leavingVoice&&(inCall||joining)&&view=="room");
    joinButton->setVisible(inCall||joining);joinButton->setEnabled(!leavingVoice);
    for(auto* b:{muteButton,cameraButton,screenButton})b->setVisible(inCall);
    sendQuality->setVisible(inCall&&(screenMode||cameraMode));
    quickEffects->setVisible(inCall&&cameraMode!=0);
    screenSoundButton->setVisible(inCall&&screenMode!=0);
    refreshStreamSound();
    sendQuality->setFixedSize(compact?54:72,42);
    auto* layout=static_cast<QHBoxLayout*>(dock->layout());
    layout->setContentsMargins(compact?10:24,compact?10:14,compact?10:24,compact?10:14);layout->setSpacing(compact?8:10);
    // Only the full-width footer pushes the leave button to the far side.
    layout->itemAt(layout->count()-2)->spacerItem()->changeSize(0,0,compact?QSizePolicy::Fixed:QSizePolicy::Expanding,QSizePolicy::Minimum);
    dock->setSizePolicy(compact?QSizePolicy::Fixed:QSizePolicy::Expanding,QSizePolicy::Fixed);
    layout->invalidate();
    callReturn->setVisible(foreign);
    if(foreign){
        const int limit=qBound(72,qRound(width()*0.12),160)-16;
        callServer->setText(callServer->fontMetrics().elidedText(voiceServerName,Qt::ElideRight,limit));
        callLabel->setText(callLabel->fontMetrics().elidedText(voiceRoomName,Qt::ElideRight,limit));
        const auto caption=UiText::ReturnToCall()+voiceServerName+" · "+voiceRoomName;
        callReturn->setAccessibleName(caption);callReturn->setToolTip(caption);
        callReturn->setFixedWidth(16+qMax(callServer->sizeHint().width(),callLabel->sizeHint().width()));
    }
    auto style=[compact](QPushButton* button,QString glyph,bool active,QString label){
        button->setText({});button->setIcon(active?tintedIcon(glyph,QColor("#111")):icon(glyph));button->setIconSize(QSize(22,22));
        button->setAccessibleName(label);button->setToolTip(label);button->setFixedSize(compact?42:48,compact?42:46);
        button->setStyleSheet(QString("QPushButton{padding:0;background:%1;border:0;border-radius:%2px} QPushButton:hover{background:%3}")
          .arg(active?"#dedede":"#242424").arg(compact?12:13).arg(active?"#fff":"#303030"));
    };
    style(muteButton,muted?"mic-off":"mic",!muted,muted?UiText::UnmuteMicrophone():UiText::MuteMicrophone());
    style(cameraButton,cameraMode!=0?"camera":"camera-off",cameraMode!=0,cameraMode!=0?UiText::TurnOffCamera():UiText::TurnOnCamera());
    style(quickEffects,"sparkles",true,UiText::CameraEffects());
    style(screenButton,"screen",screenMode!=0,screenMode!=0?UiText::StopScreenSharing2():UiText::ShareScreen());
    joinButton->setAccessibleName(UiText::LeaveVoiceChannel());joinButton->setToolTip(UiText::LeaveRoom2());joinButton->setText(compact?"":UiText::Leave2());
    joinButton->setFixedSize(compact?42:92,compact?42:46);joinButton->setIconSize(QSize(22,22));
    joinButton->setIcon(tintedIcon("exit",QColor("#efaaaa")));
    joinButton->setStyleSheet("QPushButton{padding:0;background:#302124;color:#efaaaa;border:0;border-radius:12px;font-size:12px} QPushButton:hover{background:#452a2e}");

}
void DesktopWindow::showServers(){
    if(auto* existing=findChild<QGraphicsView*>("serverMenu");existing && existing->isVisible()){static_cast<ServerPicker*>(existing)->dismiss();return;}
    warmSavedServers();
    new ServerPicker(serverSwitch,profile["servers"].toArray(),api.server,[this](QJsonObject entry){
        if(DesktopProfile::origin(entry["url"].toString())!=api.server){switchServer(entry);return;}
        for(int i=0;i<channels->count();++i)if(channels->item(i)->flags()&Qt::ItemIsSelectable){channels->setCurrentRow(i);break;}
        openView("room");
    });

}
void DesktopWindow::showSwitchNotice(QString message,bool busy){
    if(!switchNotice){
        switchNotice=new QFrame(this);switchNotice->setObjectName("serverSwitchStatus");
        switchNotice->setStyleSheet("#serverSwitchStatus{background:#252525;border-radius:12px} QPushButton{background:transparent;border:0;padding:4px;font-size:12px}");
        auto* line=new QHBoxLayout(switchNotice);line->setContentsMargins(16,10,16,10);line->setSpacing(14);
        switchMessage=text("","font-size:12px;color:#ddd");switchMessage->setWordWrap(true);line->addWidget(switchMessage);
        switchAction=new QPushButton;line->addWidget(switchAction);
        connect(switchAction,&QPushButton::clicked,this,[this]{finishServerSwitch();});
    }
    switchAction->setText(busy?UiText::Cancel():UiText::Close());switchMessage->setText(message);
    switchMessage->setFixedWidth(qBound(50,switchMessage->fontMetrics().horizontalAdvance(message)+2,qMax(50,width()-160)));
    switchNotice->adjustSize();switchNotice->move((width()-switchNotice->width())/2,16);switchNotice->show();switchNotice->raise();
}
void DesktopWindow::finishServerSwitch(const QString& error,bool denied){
    if(denied){deniedOrigin=QUrl(switchTarget);deniedProfile=localId;}
    pendingUpdateView={};
    if(switchProbe){
        auto* probe=switchProbe.data();
        auto it=preparedServers.find(probe->server);
        if(it!=preparedServers.end() && it->probe==probe)it->probe=nullptr;
        switchProbe=nullptr;probe->reset();probe->deleteLater();
    }
    switchTarget.clear();serverSwitch->setEnabled(true);
    if(localHome)localHome->setEnabled(true);
    if(!error.isEmpty() && authRestorePage && pages->currentWidget()==authRestorePage){
        if(denied){
            const auto url=authRestoreEntry["url"].toString();
            showLogin();clearAuthRestore();
            address->setText(url);loginLinkTarget->setText(QString(UiText::ServerLinkTarget()).arg(url));loginLinkTarget->show();
            QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("resumeProfile",QString());
            note(error);
        }else{
            authRestoreStatus->setText(error);authRestoreStatus->show();authRestoreRetry->show();
        }
        return;
    }
    if(!error.isEmpty()){
        showSwitchNotice(error,false);
        if(denied){
            const bool local=pages->currentWidget()==localHome;
            if(local)openLocalHome();else openView("profile");
            auto* page=(local?localLayout:detailLayout)->itemAt(0)->widget();
            if(auto* start=page->findChild<QPushButton*>("startAddServer"))start->click();
            if(auto* input=page->findChild<QLineEdit*>("addServerAddress"))input->setText(deniedOrigin.toString());
            if(auto* code=page->findChild<QLineEdit*>("addServerCode"))code->setFocus();
        }
        if(pages->currentWidget()==localHome)note(error);
    }else if(switchNotice)switchNotice->hide();
}
void DesktopWindow::switchServer(const QJsonObject& entry,bool enroll,const QString& code,QString returnIdentity){
    if(sending || !outbox.isEmpty()){showSwitchNotice(UiText::YouHaveUnsentAttachmentsWaitForThem(),false);return;}
    auto target=DesktopProfile::origin(entry["url"].toString());if(target.isEmpty()||target==api.server||!switchTarget.isEmpty())return;
    switchTarget=target.toString();switchReturnIdentity=returnIdentity;
    serverSwitch->setEnabled(false);
    if(localHome && pages->currentWidget()==localHome){localHome->setEnabled(false);note(UiText::Connecting());}
    if(!authRestorePage || pages->currentWidget()!=authRestorePage)
        showSwitchNotice(UiText::ConnectingTo()+entry["name"].toString(target.authority()),true);
    prepareServer(entry,enroll,code);
}

void DesktopWindow::clearPreparedServers(){
    const auto saved=std::exchange(preparedServers,{});
    for(const auto& state:saved)if(state.probe){state.probe->reset();state.probe->deleteLater();}
}

void DesktopWindow::warmSavedServers(){
    // Prepare at most two alternatives. Only an explicit click changes the active server.
    if(pages->currentIndex()==0 || (authRestorePage && pages->currentWidget()==authRestorePage))return;
    int remaining=2;
    for(const auto& value:profile["servers"].toArray()){
        const auto entry=value.toObject();
        const auto target=DesktopProfile::origin(entry["url"].toString());
        if(target.isEmpty() || target==api.server || target.toString()==switchTarget || entry["profileKey"].toString().isEmpty())continue;
        auto it=preparedServers.constFind(target);
        if(it==preparedServers.cend() || it->key!=entry["profileKey"].toString() || !it->received.isValid() || it->received.elapsed()>=15000)
            prepareServer(entry);
        if(--remaining==0)break;
    }
}

void DesktopWindow::prepareServer(QJsonObject entry,bool enroll,const QString& code){
    const auto target=DesktopProfile::origin(entry["url"].toString());
    const auto key=entry["profileKey"].toString();
    auto& state=preparedServers[target];
    if(state.key!=key || enroll){
        if(state.probe){state.probe->reset();state.probe->deleteLater();}
        state={};state.key=key;
    }
    if(state.probe){
        if(switchTarget==target.toString())switchProbe=state.probe;
        return;
    }
    auto* probe=new ServerApi(this);probe->server=target;state.probe=probe;
    if(switchTarget==target.toString())switchProbe=probe;
    auto finish=[this,target,probe,key,enroll](QString error,bool denied=false){
        auto it=preparedServers.find(target);
        if(it==preparedServers.end() || it->probe!=probe)return;
        it->probe=nullptr;
        if(!error.isEmpty()){it->received.invalidate();it->workspace={};it->token.clear();}
        const bool requested=switchProbe==probe;
        if(requested)switchProbe=nullptr;
        probe->reset();probe->deleteLater();
        if(requested && !error.isEmpty()){
            const auto servers=profile["servers"].toArray();
            const bool known=denied && !enroll && std::any_of(servers.begin(),servers.end(),[&](auto value){
                const auto saved=value.toObject();
                return DesktopProfile::origin(saved["url"].toString())==target && saved["profileKey"].toString()==key;
            });
            qWarning("WIN_AUTH_SWITCH_%s",known?"DENIED":"RETRY");
            finishServerSwitch(error,known);
        }
    };
    QTimer::singleShot(15000,probe,[finish]{finish(UiText::TheServerDidNotRespondInTime2());});
    auto workspaceRequest=[this,target,probe,finish](QJsonObject saved,QString member,bool cachedSession=false){
        const bool opening=switchProbe==probe;
        probe->requestWithStatus("GET",opening?"/api/workspace?webTab=open":"/api/workspace",{},[this,target,probe,saved,member,finish,opening,cachedSession](QJsonObject data,QString error,int status){
            auto it=preparedServers.find(target);
            if(it==preparedServers.end() || it->probe!=probe)return;
            // Check the selected destination on the server before showing a prefetched roster.
            // Expired sessions get one fresh sign-in using this destination's saved key.
            if(cachedSession && status==401){
                it->received.invalidate();it->workspace={};it->token.clear();finish({});
                prepareServer(saved);return;
            }
            if(error.isEmpty() && (!data["channels"].isArray() || !data["members"].isArray() || data["selfId"]!=member))
                error=UiText::InvalidServerWorkspace();
            if(!error.isEmpty()){finish(error,status==401 || status==403);return;}
            it->token=probe->token;it->member=member;it->key=saved["profileKey"].toString();
            it->workspace=data;it->received.start();
            const bool requested=switchProbe==probe;
            finish({});
            if(requested && !opening){prepareServer(saved);return;}
            if(requested)usePreparedServer(target,saved,opening);
        });
    };
    if(!enroll && !state.token.isEmpty()){
        probe->token=state.token;probe->activeProfileKey=[key]{return key;};
        workspaceRequest(entry,state.member,true);return;
    }
    QJsonObject body{{"profileKey",key}};
    if(enroll){body["name"]=entry["displayName"];body["password"]=code;}
    auto accept=[probe,entry,workspaceRequest,finish](QJsonObject auth,QString error,int status) mutable {
        const auto member=auth["member"].toObject()["id"].toString();
        if(!error.isEmpty() || member.isEmpty() || auth["session"].toString().isEmpty()){
            finish(error.isEmpty()?QString::fromUtf8(UiText::NoServerSessionReceived()):error,status==401 || status==403);return;
        }
        probe->token=auth["session"].toString();
        entry["profileKey"]=auth["profileKey"].toString(entry["profileKey"].toString());
        workspaceRequest(entry,member);
    };
    probe->requestWithStatus("POST",enroll?"/api/auth":"/api/profile-session",body,std::move(accept));
}

void DesktopWindow::usePreparedServer(const QUrl& target,QJsonObject entry,bool registered){
    const bool restoring=authRestorePage && pages->currentWidget()==authRestorePage;
    const bool fromHome=restoring || (localHome && pages->currentWidget()==localHome);
    const auto ready=preparedServers.take(target);
    if(attachmentViewer){pages->setCurrentIndex(1);pages->removeWidget(attachmentViewer);attachmentViewer->deleteLater();attachmentViewer=nullptr;}
    const auto data=ready.workspace;
    const auto session=ready.token;
    const auto returnIdentity=switchReturnIdentity;
    const auto route=pendingUpdateView;
    finishServerSwitch();
    if(!api.token.isEmpty() && !workspace.isEmpty()){
        auto& old=preparedServers[api.server];
        old.key=api.activeProfileKey();old.member=selfId;old.token=api.token;old.workspace=workspace;
        old.received=workspaceReceived;
    }
    // Keep only recent destinations, regardless of how many servers the profile contains.
    while(preparedServers.size()>3){
        auto oldest=preparedServers.end();
        for(auto it=preparedServers.begin();it!=preparedServers.end();++it){
            if(it->probe || it.key()==api.server)continue;
            if(!it->received.isValid()){oldest=it;break;}
            if(oldest==preparedServers.end() || it->received.elapsed()>oldest->received.elapsed())oldest=it;
        }
        if(oldest==preparedServers.end())break;
        preparedServers.erase(oldest);
    }
    if(joining && callIdentity.isEmpty()){pendingVoiceRoom.clear();setCall(false);}

    api.reset();imageRequests.clear();api.server=target;api.token=session;workspaceLatency=0;workspaceFailed=false;rememberActiveServer();
    auto servers=profile.value("servers").toArray();auto saved=entry;if(!saved["customName"].toBool())saved["name"]=data["name"];
    int index=0;while(index<servers.size()&&DesktopProfile::origin(servers[index].toObject()["url"].toString())!=target)++index;
    if(index==servers.size())servers.append(saved);else servers[index]=saved;if(servers!=profile["servers"].toArray()){profile["servers"]=servers;saveLocal();}
    ++channelGeneration;loadingMessages=false;loadingOlder=false;refreshing=false;sending=false;selected.clear();
    clearMessageActions();outbox.clear();outboxOrder.clear();drafts.clear();draftFormats.clear();fileDrafts.clear();mentionDrafts.clear();replyId.clear();sendSignature.clear();rooms={};workspace={};lastMessages={};avatars.clear();avatarVersions.clear();
    if(!returnIdentity.isEmpty() && inCall && callIdentity==returnIdentity && callOrigin==target.toString())selected=activeVoice;
    if(!route.isEmpty())selected=route["channel"].toString();
    // A prefetched roster is passive. Switch channels immediately, but paint the
    // roster from the first authenticated snapshot of this visit, including self.
    awaitingMemberRoster=!registered && data["onlineMembers"].isArray();
    if(awaitingMemberRoster){members->clear();mentionChoices={};mentionLabels.clear();static_cast<QStringListModel*>(mentions->model())->setStringList({});}
    else api.syncMemberPresence(data);
    clearChatHistory();compose->clear();stageSignature.clear();note({});renderWorkspace(data,WorkspaceRefresh::All);openView("room");
    if(fromHome){
        pages->setCurrentIndex(1);
        if(restoring){password->clear();clearAuthRestore();}
        setMemberPanel(QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).value("membersExpanded",false).toBool(),false);
        bindMute();timer.start(3000);
    }
    workspaceReceived=ready.received;
    const auto section=route["view"].toString();
    if(QStringList{"general","audio","video","roles","keys","overlay","profile"}.contains(section) ||
       (section=="channels" && (allowed("manage_server") || allowed("manage_channels")))){
        openView(section);
        auto* current=detailLayout->itemAt(0)->widget();
        if(auto* scroll=current->findChild<QScrollArea*>("settingsScroll")){
            const bool bottom=route["bottom"].toBool();
            const int position=route["scroll"].toInt();
            auto* bar=scroll->verticalScrollBar();
            if(bottom || position>0){
                // Storage and update responses can expand this page after its first layout.
                auto* anchor=new QObject(bar);
                auto restore=[bar,anchor,bottom,position]{
                    bar->setValue(bottom?bar->maximum():position);
                    if(!bottom && bar->maximum()>=position)anchor->deleteLater();
                };
                connect(bar,&QScrollBar::rangeChanged,anchor,restore);
                connect(bar,&QScrollBar::actionTriggered,anchor,&QObject::deleteLater);
                connect(bar,&QScrollBar::sliderPressed,anchor,&QObject::deleteLater);
                QTimer::singleShot(0,anchor,restore);
            }
        }
    }
    if(awaitingMemberRoster || workspaceReceived.elapsed()>500)QTimer::singleShot(0,this,&DesktopWindow::refresh);
    QTimer::singleShot(0,this,&DesktopWindow::warmSavedServers);
}
void DesktopWindow::logout(){
    api.request("DELETE","/api/access-session",{},[this](auto,QString error){
        if(!error.isEmpty()){note(error);return;}
        QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("resumeProfile",QString());showLogin();
    });
}
