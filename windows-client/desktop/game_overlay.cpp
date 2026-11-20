#include "game_overlay.h"
#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include <QtWidgets>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <windows.h>

namespace {
// Out-of-context foreground notifications run on the registering GUI thread.
QHash<HWINEVENTHOOK,GameOverlay*> foregroundOverlays;
void CALLBACK foregroundChanged(HWINEVENTHOOK hook,DWORD,HWND, LONG,LONG,DWORD,DWORD) {
    auto* overlay=foregroundOverlays.value(hook);
    if(overlay && overlay->isVisible()){
        SetWindowPos(reinterpret_cast<HWND>(overlay->winId()),HWND_TOPMOST,0,0,0,0,
                     SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOOWNERZORDER);
    }
}
constexpr int margin = 8, rowHeight = 36, avatarSize = 28, contentInset = 8;
constexpr int rowInset = contentInset - (rowHeight - avatarSize) / 2;
constexpr int textLeft = contentInset + avatarSize + 8;

QString overlayNickname(const QString& name) {
    constexpr int maxCharacters=25;
    if(name.size()<=maxCharacters)return name;
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme,name);
    for(int i=0;i<maxCharacters;++i){
        if(graphemes.toNextBoundary()<0)return name;
    }
    const auto end=graphemes.position();
    return end<name.size() ? name.left(end)+QChar(0x2026) : name;
}

QPoint boundedPosition(QPoint p) {
    return {qBound(0, p.x(), 100), qBound(0, p.y(), 100)};
}

QPoint offset(const QSize& travel, QPoint percent) {
    return {qRound(travel.width() * percent.x() / 100.), qRound(travel.height() * percent.y() / 100.)};
}

class OverlayPlacement final : public QWidget {
public:
    OverlayPlacement(QPoint initial, std::function<void(QPoint)> save)
        : position(initial), save(std::move(save)) {
        setFixedSize(234, 126);
        setObjectName("overlayPlacement");
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::OpenHandCursor);
        setAccessibleName(UiText::OverlayPosition());
    }
private:
    QPoint position, grabOffset;
    std::function<void(QPoint)> save;
    QRect area() const { return rect().adjusted(8,8,-8,-8); }
    QSize travel() const { return area().size() - QSize(64,24); }
    QRect marker() const { return {area().topLeft() + offset(travel(), position), QSize(64,24)}; }
    void change(QPoint p) {
        p = boundedPosition(p);
        if (position != p) { position = p; update(); save(p); }
    }
    void moveMarker(QPoint cursor) {
        auto pos = cursor - area().topLeft() - grabOffset;
        change({qRound(pos.x()*100./travel().width()), qRound(pos.y()*100./travel().height())});
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button()!=Qt::LeftButton) return;
        const auto p=e->position().toPoint();
        grabOffset=marker().contains(p) ? p-marker().topLeft() : QPoint(32,12);
        setCursor(Qt::ClosedHandCursor);
        moveMarker(p);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) moveMarker(e->position().toPoint());
    }
    void mouseReleaseEvent(QMouseEvent*) override { setCursor(Qt::OpenHandCursor); }
    void keyPressEvent(QKeyEvent* e) override {
        switch(e->key()) {
        case Qt::Key_Left: change(position-QPoint(5,0)); break;
        case Qt::Key_Right: change(position+QPoint(5,0)); break;
        case Qt::Key_Up: change(position-QPoint(0,5)); break;
        case Qt::Key_Down: change(position+QPoint(0,5)); break;
        default: QWidget::keyPressEvent(e);
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen); p.setBrush(QColor(hasFocus() ? "#202020" : "#101010"));
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1),10,10);
        auto box=marker(); p.setPen(Qt::NoPen); p.setBrush(QColor("#555555"));
        p.drawEllipse(box.topLeft()+QPoint(9,12),8,8);
        p.setPen(QColor("#eeeeee")); p.drawText(box.adjusted(20,0,0,0),Qt::AlignVCenter,"Nimbo");
    }
};
}

GameOverlay::GameOverlay(QWidget& mainWindow) : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus), mainWindow(mainWindow) {
    setObjectName("gameOverlay");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_QuitOnClose,false);
    setFocusPolicy(Qt::NoFocus);
    QFont labelFont=font(); labelFont.setPixelSize(14); labelFont.setWeight(QFont::Medium);
    setFont(labelFont);
    auto hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,
                              foregroundChanged,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    if(hook){foregroundHook=hook;foregroundOverlays.insert(hook,this);}
    for (auto signal : {&QGuiApplication::screenAdded, &QGuiApplication::screenRemoved, &QGuiApplication::primaryScreenChanged})
        connect(qApp,signal,this,[this]{ place(); });
    languageChanged();
}

GameOverlay::~GameOverlay(){
    if(auto hook=static_cast<HWINEVENTHOOK>(foregroundHook)){
        foregroundOverlays.remove(hook);UnhookWinEvent(hook);
    }
}

void GameOverlay::languageChanged() { setWindowTitle(UiText::GameOverlay()); update(); }

void GameOverlay::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if(event->type()==QEvent::FontChange && target)place();
}

void GameOverlay::closeEvent(QCloseEvent* event) {
    // Windows can address the topmost unowned window when closing the process.
    // Let the main window perform its normal call departure before shutting down.
    event->ignore();
    QTimer::singleShot(0,&mainWindow,&QWidget::close);
}

void GameOverlay::configure(const OverlayPreferences& prefs) {
    preferences=prefs;
    place();
    update();
}

int GameOverlay::visibleRows() const {
    return target ? qBound(1,(target->availableGeometry().height()-2*margin-2*rowInset)/rowHeight,10) : 10;
}

QList<GameOverlay::Person> GameOverlay::visiblePeople() const {
    const int limit=visibleRows();
    if(people.size()<=limit)return people;
    QList<Person> rows;
    rows.reserve(limit);
    for(bool speaking:{true,false}){
        for(const auto& person:people){
            if(person.speaking==speaking)rows.append(person);
            if(rows.size()==limit)return rows;
        }
    }
    return rows;
}

void GameOverlay::place() {
    auto* screen=QGuiApplication::primaryScreen();
    for (auto* candidate : QGuiApplication::screens())
        if (candidate->name()==preferences.screen) { screen=candidate; break; }
    if (!screen) return;
    if (target!=screen) {
        disconnect(geometryChanged);
        target=screen;
        geometryChanged=connect(screen,&QScreen::availableGeometryChanged,this,[this]{ place(); });
        setScreen(screen);
    }
    const auto area=screen->availableGeometry().adjusted(margin,margin,-margin,-margin);
    const auto rows=visiblePeople();
    const QFontMetricsF metrics(font(),this);
    qreal nameWidth=0;
    for(const auto& person:rows)nameWidth=qMax(nameWidth,metrics.horizontalAdvance(person.name));
    const int width=qMin(area.width(),textLeft+qCeil(nameWidth)+contentInset);
    const QSize size(width,2*rowInset+qMax(1,int(rows.size()))*rowHeight);
    setGeometry({area.topLeft()+offset(area.size()-size,preferences.position),size});
}

void GameOverlay::setPeople(const QJsonObject& local,const QJsonArray& peers) {
    QList<Person> next;
    QSet<QString> seen;
    auto append=[&](const QJsonObject& person) {
        const auto id=person["identity"].toString();
        if (id.isEmpty() || seen.contains(id)) return;
        seen.insert(id);
        const bool muted=person["micKnown"].toBool() && !person["mic"].toBool();
        next.append({id,overlayNickname(person["name"].toString(UiText::Member2())),person["speaking"].toBool() && !muted});
    };
    append(local);
    for (auto peer : peers) append(peer.toObject());
    if (next==people) return;
    const bool resized=people.size()!=next.size() || !std::equal(people.cbegin(),people.cend(),next.cbegin(),
        [](const Person& a,const Person& b){return a.name==b.name;});
    people=std::move(next);
    portraits.removeIf([&](auto it){ return !seen.contains(it.key()); });
    requested.intersect(seen);
    if (resized || people.size()>visibleRows()) place();
    update();
}

void GameOverlay::syncPortraits(const QHash<QString,QPixmap>* roomAvatars,const ServerApi& call) {
    for(const auto& person : people){
        const auto member=person.id.section('.',0,0);
        const auto source=roomAvatars ? roomAvatars->value(member) : QPixmap{};
        auto& portrait=portraits[person.id];
        if(portrait.image.isNull() || portrait.name!=person.name || (roomAvatars && portrait.sourceKey!=source.cacheKey())){
            portrait={person.name,source.cacheKey(),Ui::avatar(person.name,source,avatarSize)};
            update();
        }
        // The call can continue while the main window browses another server.
        if (roomAvatars || portrait.sourceKey || requested.contains(person.id) || call.token.isEmpty()) continue;
        if(!portraitApi){
            portraitApi=std::make_unique<ServerApi>();
            portraitApi->server=call.server;
        }
        portraitApi->token=call.token;
        requested.insert(person.id);
        portraitApi->bytes("/api/avatars/"+QString::fromLatin1(QUrl::toPercentEncoding(member)),1024*1024,
            [this,id=person.id](QByteArray bytes,const QString& error){
                auto it=portraits.find(id);
                if(it==portraits.end() || it->sourceKey || !error.isEmpty()) return;
                QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                auto size=reader.size();
                if(!size.isValid() || size.width()>4096 || size.height()>4096) return;
                reader.setScaledSize(size.scaled(avatarSize*2,avatarSize*2,Qt::KeepAspectRatio));
                auto photo=QPixmap::fromImage(reader.read());
                if(photo.isNull())return;
                it->sourceKey=photo.cacheKey();it->image=Ui::avatar(it->name,photo,avatarSize);
                update();
            });
    }
}

void GameOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto visible=visiblePeople();
    const QFontMetricsF metrics(p.font(),p.device());
    p.setOpacity(preferences.opacity/100.);
    int y=rowInset;
    for (const auto& person : visible) {
        const int mid=y+rowHeight/2;
        const auto portrait=portraits.constFind(person.id);
        if(portrait!=portraits.cend()){
            QRect box(contentInset,mid-avatarSize/2,avatarSize,avatarSize);
            p.drawPixmap(box.topLeft(),portrait->image);
            if(!person.speaking){
                p.save();p.setOpacity(1.);p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,76));p.drawEllipse(box);p.restore();
            }
        }
        const QRect line(textLeft,y,width()-textLeft-contentInset,rowHeight);
        p.setPen(person.speaking?Qt::white:QColor("#b3b3b3"));
        p.drawText(line,Qt::AlignLeft|Qt::AlignVCenter,metrics.elidedText(person.name,Qt::ElideRight,line.width()));
        y+=rowHeight;
    }
    if(preferences.backgroundOpacity && !visible.isEmpty()){
        // Compose behind the participants so the two alpha settings stay independent.
        p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
        p.setOpacity(preferences.backgroundOpacity/100.);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#101010"));
        p.drawRoundedRect(QRectF(rect()),10,10);
    }
}

void DesktopWindow::syncGameOverlay() {
    if(inCall || !overlayPrefs.enabled || leavingVoice || closingWindow)previewingOverlay=false;
    if(overlayPreviewButton){
        overlayPreviewButton->setEnabled(overlayPrefs.enabled && !inCall);
        overlayPreviewButton->setText(previewingOverlay?UiText::StopOverlayPreview():UiText::TestGameOverlay());
    }
    if (!overlayPrefs.enabled || (!inCall && !previewingOverlay) || leavingVoice || closingWindow) {
        previewingOverlay=false;
        gameOverlay.reset();
        return;
    }
    if (!gameOverlay) {
        gameOverlay=std::make_unique<GameOverlay>(*this);
        gameOverlay->configure(overlayPrefs);
    }
    if(previewingOverlay && !inCall){
        const auto name=self["name"].toString(profile["name"].toString("Nimbo"));
        const auto id=selfId.isEmpty()?QStringLiteral("preview"):selfId;
        gameOverlay->setPeople({{"identity",id},{"name",name},{"speaking",true}},{});
        gameOverlay->syncPortraits(&avatars,callStateApi);
        gameOverlay->show();
        return;
    }
    previewingOverlay=false;
    auto local=localPerson();
    local["name"]=overlayLocalName;
    gameOverlay->setPeople(local,liveRoster);
    gameOverlay->syncPortraits(callOrigin==api.server.toString()?&avatars:nullptr,callStateApi);
    if (!gameOverlay->isVisible()) gameOverlay->show();
}

void DesktopWindow::renderOverlaySettings(QVBoxLayout* body) {
    auto* toggleRow=new QHBoxLayout;
    auto* title=Ui::text({},"font-size:14px;font-weight:500");
    auto* enabled=new Ui::ToggleSwitch(UiText::GameOverlay());
    enabled->setChecked(overlayPrefs.enabled);
    auto updateTitle=[title,enabled]{title->setText(enabled->isChecked()?UiText::DisableGameOverlay():UiText::EnableGameOverlay());};
    updateTitle();
    toggleRow->addWidget(title,1);toggleRow->addWidget(enabled);body->addLayout(toggleRow);
    connect(enabled,&QCheckBox::toggled,title,updateTitle);
    auto* help=Ui::text(UiText::OverlayHelp(),"color:#aaa;font-size:12px");help->setWordWrap(true);
    body->addWidget(help);
    auto change=[this](const QString& key,const QVariant& value){
        QSettings(dataPath+"/desktop.ini",QSettings::IniFormat).setValue("overlay/"+key,value);
        if(gameOverlay) gameOverlay->configure(overlayPrefs);
        syncGameOverlay();
    };
    connect(enabled,&QCheckBox::toggled,enabled,[this,change](bool on){
        previewingOverlay=on && !inCall;
        overlayPrefs.enabled=on; change("enabled",on);
    });
    body->addSpacing(12);
    auto* screen=Ui::select(UiText::OverlayMonitor());
    body->addWidget(Ui::text(UiText::OverlayMonitor()));
    body->addWidget(screen);
    auto populate=[this,screen]{
        QSignalBlocker block(screen);
        screen->clear(); screen->addItem(UiText::OverlayPrimaryMonitor(),QString());
        for(auto* s:QGuiApplication::screens()) {
            const auto label=s->model().isEmpty()?s->name():s->model()+" · "+s->name();
            screen->addItem(label,s->name());
        }
        screen->setCurrentIndex(qMax(0,screen->findData(overlayPrefs.screen)));
    };
    populate();
    connect(qApp,&QGuiApplication::screenAdded,screen,populate);
    connect(qApp,&QGuiApplication::screenRemoved,screen,populate);
    connect(screen,&QComboBox::currentIndexChanged,screen,[this,change,screen]{
        overlayPrefs.screen=screen->currentData().toString(); change("screen",overlayPrefs.screen);
    });
    auto* opacityGrid=new QGridLayout;
    opacityGrid->setContentsMargins(0,0,0,0);opacityGrid->setHorizontalSpacing(16);
    opacityGrid->setColumnStretch(0,1);opacityGrid->setColumnStretch(1,1);
    body->addLayout(opacityGrid);
    auto addOpacity=[this,opacityGrid,change](int column,const QString& label,const QString& key,int minimum,int OverlayPreferences::*setting){
        auto caption=[label](int value){return label+QStringLiteral(" · %1%").arg(value);};
        auto* amount=Ui::text(caption(overlayPrefs.*setting));amount->setWordWrap(true);
        opacityGrid->addWidget(amount,0,column,Qt::AlignBottom);
        auto* slider=new QSlider(Qt::Horizontal);slider->setAccessibleName(label);slider->setObjectName(key);
        amount->setBuddy(slider);
        slider->setRange(minimum,100);slider->setValue(overlayPrefs.*setting);opacityGrid->addWidget(slider,1,column);
        connect(slider,&QSlider::valueChanged,slider,[this,amount,caption,change,key,setting](int value){
            overlayPrefs.*setting=value;amount->setText(caption(value));change(key,value);
        });
    };
    addOpacity(0,UiText::OverlayPeopleOpacity(),"opacity",10,&OverlayPreferences::opacity);
    addOpacity(1,UiText::OverlayBackgroundOpacity(),"backgroundOpacity",0,&OverlayPreferences::backgroundOpacity);
    body->addWidget(Ui::text(UiText::OverlayPosition()));
    body->addWidget(new OverlayPlacement(overlayPrefs.position,[this,change](QPoint pos){
        overlayPrefs.position=pos; change("position",pos);
    }),0,Qt::AlignLeft);
    auto* preview=new QPushButton(previewingOverlay?UiText::StopOverlayPreview():UiText::TestGameOverlay());Ui::compactButton(preview);
    overlayPreviewButton=preview;
    body->addWidget(preview,0,Qt::AlignLeft);preview->setEnabled(enabled->isChecked() && !inCall);
    connect(preview,&QPushButton::clicked,preview,[this]{
        previewingOverlay=!previewingOverlay;syncGameOverlay();
    });
}
