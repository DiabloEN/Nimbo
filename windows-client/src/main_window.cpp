#include "main_window.h"
#ifdef NIMBO_MEDIA
#include "media_dialog.h"
#endif

#include "audio_probe.h"
#include "local_store.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QAudioDevice>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSizePolicy>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

namespace {

constexpr auto kText = "#dce5f1";
constexpr auto kMuted = "#8795ab";
constexpr auto kAccent = "#92dfd5";

QFont uiFont(int pixels, bool bold = false) {
    QFont result(QStringLiteral("Segoe UI"));
    result.setPixelSize(pixels);
    result.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return result;
}

QIcon icon(const QString& name, QColor color = QColor(kMuted), int size = 20) {
    QPixmap pixmap(size * 2, size * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (name == "hash") {
        p.drawLine(QPointF(9, 4), QPointF(7, 20));
        p.drawLine(QPointF(17, 4), QPointF(15, 20));
        p.drawLine(QPointF(4, 9), QPointF(21, 9));
        p.drawLine(QPointF(3, 15), QPointF(20, 15));
    } else if (name == "speaker") {
        QPainterPath shape;
        shape.moveTo(4, 9); shape.lineTo(8, 9); shape.lineTo(12, 5);
        shape.lineTo(12, 19); shape.lineTo(8, 15); shape.lineTo(4, 15); shape.closeSubpath();
        p.drawPath(shape);
        p.drawArc(QRectF(9, 7, 9, 10), -65 * 16, 130 * 16);
        p.drawArc(QRectF(8, 3, 14, 18), -55 * 16, 110 * 16);
    } else if (name == "mic" || name == "mic-off") {
        p.drawRoundedRect(QRectF(9, 3, 6, 11), 3, 3);
        p.drawArc(QRectF(6, 7, 12, 11), 180 * 16, 180 * 16);
        p.drawLine(QPointF(12, 18), QPointF(12, 21));
        p.drawLine(QPointF(9, 21), QPointF(15, 21));
        if (name == "mic-off") p.drawLine(QPointF(3, 3), QPointF(21, 21));
    } else if (name == "headphones" || name == "headphones-off") {
        p.drawArc(QRectF(4, 4, 16, 17), 0, 180 * 16);
        p.drawRoundedRect(QRectF(3, 11, 4, 8), 1.5, 1.5);
        p.drawRoundedRect(QRectF(17, 11, 4, 8), 1.5, 1.5);
        if (name == "headphones-off") p.drawLine(QPointF(3, 3), QPointF(21, 21));
    } else if (name == "settings") {
        p.drawEllipse(QRectF(8, 8, 8, 8));
        p.drawEllipse(QRectF(3.5, 3.5, 17, 17));
        for (int i = 0; i < 8; ++i) {
            p.save(); p.translate(12, 12); p.rotate(i * 45.0);
            p.drawLine(QPointF(0, -8.5), QPointF(0, -10.5)); p.restore();
        }
    } else if (name == "plus") {
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
    } else if (name == "send") {
        QPainterPath path;
        path.moveTo(4, 4); path.lineTo(21, 12); path.lineTo(4, 20);
        path.lineTo(7, 12); path.closeSubpath(); p.drawPath(path);
        p.drawLine(QPointF(7, 12), QPointF(16, 12));
    } else if (name == "search") {
        p.drawEllipse(QRectF(4, 4, 11, 11));
        p.drawLine(QPointF(14, 14), QPointF(20, 20));
    } else if (name == "invite") {
        p.drawEllipse(QRectF(5, 4, 7, 7));
        p.drawArc(QRectF(2, 13, 13, 11), 0, 180 * 16);
        p.drawLine(QPointF(19, 8), QPointF(19, 16));
        p.drawLine(QPointF(15, 12), QPointF(23, 12));
    } else if (name == "info") {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawPoint(QPointF(12, 8));
        p.drawLine(QPointF(12, 11), QPointF(12, 17));
    } else if (name == "chevron") {
        p.drawPolyline(QPolygonF{QPointF(8, 10), QPointF(12, 14), QPointF(16, 10)});
    } else if (name == "cloud") {
        QPainterPath path;
        path.moveTo(6, 18); path.cubicTo(0, 18, 0, 10, 6, 10);
        path.cubicTo(6, 2, 17, 2, 18, 10);
        path.cubicTo(25, 10, 25, 18, 18, 18); path.closeSubpath(); p.drawPath(path);
    } else if (name == "arrow") {
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        p.drawPolyline(QPolygonF{QPointF(14, 7), QPointF(19, 12), QPointF(14, 17)});
    }
    return QIcon(pixmap);
}

QLabel* label(const QString& text, int size = 14, QColor color = QColor(kText), bool bold = false,
              QWidget* parent = nullptr) {
    auto* result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setFont(uiFont(size, bold));
    result->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: %2px; font-weight: %3;")
        .arg(color.name()).arg(size).arg(bold ? 600 : 400));
    return result;
}

QToolButton* tool(const QString& name, const QString& tooltip, QWidget* parent = nullptr) {
    auto* result = new QToolButton(parent);
    result->setIcon(icon(name));
    result->setIconSize(QSize(20, 20));
    result->setFixedSize(34, 34);
    result->setToolTip(tooltip);
    result->setAccessibleName(tooltip);
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

class Avatar final : public QWidget {
public:
    Avatar(QString initials, QColor color, int size, QWidget* parent = nullptr)
        : QWidget(parent), initials_(std::move(initials)), color_(std::move(color)) {
        setFixedSize(size, size);
    }
    void setInitials(QString initials) { initials_ = std::move(initials); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawRoundedRect(rect(), width() * .34, height() * .34);
        p.setPen(QColor("#e8f1f7"));
        p.setFont(uiFont(width() > 36 ? 15 : 12, true));
        p.drawText(rect(), Qt::AlignCenter, initials_);
    }
private:
    QString initials_;
    QColor color_;
};

QString initials(const QString& name) {
    const auto words = name.simplified().split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) return QStringLiteral("ВЫ");
    QString result = words.first().left(1);
    if (words.size() > 1) result += words.last().left(1);
    return result.toUpper();
}

class MessageModel final : public QAbstractListModel {
public:
    explicit MessageModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(items_.size());
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
        const auto& item = items_.at(index.row());
        if (role == Qt::DisplayRole) return item.body;
        if (role == Qt::AccessibleTextRole)
            return item.author + QStringLiteral(": ") + item.body;
        return {};
    }
    const ChatMessage& message(int row) const { return items_.at(row); }
    void replace(QList<ChatMessage> items) {
        beginResetModel(); items_ = std::move(items); endResetModel();
    }
private:
    QList<ChatMessage> items_;
};

class MessageDelegate final : public QStyledItemDelegate {
public:
    explicit MessageDelegate(QListView* view) : QStyledItemDelegate(view), view_(view) {}
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const int width = std::max(180, option.rect.width() > 0 ? option.rect.width() : view_->viewport()->width());
        const QFontMetrics metrics(uiFont(14));
        const auto bounds = metrics.boundingRect(QRect(0, 0, width - 104, 100000),
                                                  Qt::TextWordWrap, index.data().toString());
        return QSize(width, std::max(83, 49 + bounds.height() + 15));
    }
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto* model = static_cast<const MessageModel*>(index.model());
        const auto& message = model->message(index.row());
        p->save();
        p->setClipRect(option.rect);
        p->setRenderHint(QPainter::Antialiasing);
        const int x = option.rect.x();
        const int y = option.rect.y();
        if (option.state & QStyle::State_MouseOver) p->fillRect(option.rect, QColor("#151e2c"));
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(message.own ? "#315d65" : "#41415f"));
        p->drawRoundedRect(QRectF(x + 25, y + 18, 35, 35), 12, 12);
        if (message.author == QStringLiteral("Nimbo")) {
            icon("cloud", QColor("#d1cafa"), 21).paint(p, QRect(x + 32, y + 25, 21, 21));
        } else {
            p->setPen(QColor("#e2f5f1")); p->setFont(uiFont(12, true));
            p->drawText(QRect(x + 25, y + 18, 35, 35), Qt::AlignCenter, initials(message.author));
        }
        p->setFont(uiFont(13, true));
        p->setPen(QColor(message.own ? "#a9e5dd" : "#c9c3ea"));
        const QFontMetrics nameMetrics(p->font(), p->device());
        const int nameWidth = std::min(nameMetrics.horizontalAdvance(message.author) + 4,
                                       std::max(60, option.rect.width() - 240));
        p->drawText(QRect(x + 76, y + 17, nameWidth + 2, 20), Qt::AlignVCenter,
                    nameMetrics.elidedText(message.author, Qt::ElideRight, nameWidth));
        p->setFont(uiFont(11)); p->setPen(QColor("#64758c"));
        const QString timestamp = message.createdAt.toLocalTime().date() == QDate::currentDate()
            ? message.createdAt.toLocalTime().toString(QStringLiteral("HH:mm"))
            : message.createdAt.toLocalTime().toString(QStringLiteral("dd.MM · HH:mm"));
        p->drawText(QRect(x + 76 + nameWidth + 12, y + 18, 125, 18), Qt::AlignVCenter, timestamp);
        p->setFont(uiFont(14)); p->setPen(QColor("#c1cddd"));
        p->drawText(QRect(x + 76, y + 42, option.rect.width() - 104, option.rect.height() - 48),
                    Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, message.body);
        p->restore();
    }
private:
    QListView* view_;
};

class Composer final : public QPlainTextEdit {
public:
    explicit Composer(QWidget* parent = nullptr) : QPlainTextEdit(parent) {
        setFont(uiFont(14));
        setFrameShape(QFrame::NoFrame);
        setTabChangesFocus(true);
        setFixedHeight(58);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    std::function<void()> sendRequested;
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))) {
            if (sendRequested) sendRequested();
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};

const QString kStyle = QStringLiteral(R"(
    QMainWindow, QDialog { background: #111824; }
    QWidget { color: #dce5f1; font-family: 'Segoe UI'; font-size: 14px; }
    QFrame#rail { background: #0d131d; border-right: 1px solid #202a3a; }
    QFrame#sidebar { background: #151e2b; border-right: 1px solid #263141; }
    QFrame#members { background: #141c29; border-left: 1px solid #263141; }
    QFrame#header { background: #141d2a; border-bottom: 1px solid #263141; }
    QFrame#roomCard { background: #1d2938; border: 1px solid #2b3b4d; border-radius: 12px; }
    QFrame#profileCard { background: #111925; border-top: 1px solid #273244; }
    QFrame#composerCard { background: #1c2737; border: 1px solid #314053; border-radius: 14px; }
    QFrame#localNotice { background: #182735; border: 1px solid #2a414e; border-radius: 9px; }
    QFrame#divider { background: #2a3545; border: none; }
    QPushButton, QToolButton { background: transparent; border: none; border-radius: 7px; padding: 6px; }
    QPushButton:hover, QToolButton:hover { background: #253246; }
    QPushButton:pressed, QToolButton:pressed { background: #304057; }
    QPushButton:focus, QToolButton:focus { border: 1px solid #759bba; }
    QPushButton#serverButton { background: #202c3e; color: #c0cbdc; border-radius: 15px; font-size: 16px; font-weight: 600; }
    QPushButton#serverButton:hover { background: #324258; }
    QPushButton#serverButton:checked { background: #29505a; color: #b9eee6; border: 1px solid #508d91; }
    QPushButton[channelButton="true"] { text-align: left; padding: 9px 12px; color: #98a7bb; border-radius: 8px; }
    QPushButton[channelButton="true"]:hover { background: #202c3d; color: #e0e8f5; }
    QPushButton[channelButton="true"]:checked { background: #263847; color: #b5e7e1; font-weight: 600; }
    QPushButton#primary, QPushButton#sendButton { background: #96dfd5; color: #102a2c; font-weight: 600; padding: 9px 16px; border-radius: 8px; }
    QPushButton#primary:hover, QPushButton#sendButton:hover { background: #b4eee4; }
    QPushButton#primary:pressed, QPushButton#sendButton:pressed { background: #78c6bc; }
    QPushButton#secondary { background: #29374b; color: #d9e3ef; padding: 9px 14px; border-radius: 8px; }
    QPushButton:disabled, QToolButton:disabled { color: #65738a; background: transparent; }
    QToolButton:checked { background: #49313d; }
    QLineEdit, QComboBox { background: #111b29; border: 1px solid #344357; border-radius: 7px; padding: 9px 11px; selection-background-color: #365c66; }
    QLineEdit:focus, QComboBox:focus { border: 1px solid #82b9bb; }
    QLineEdit#search { background: #0f1825; border: 1px solid #283749; font-size: 12px; padding: 7px 10px; }
    QComboBox::drop-down { width: 26px; border: none; }
    QComboBox QAbstractItemView { background: #1c293b; color: #dce5f1; border: 1px solid #3a4a61; selection-background-color: #30515c; }
    QPlainTextEdit { background: transparent; border: none; selection-background-color: #365c66; color: #d4dfed; }
    QListView { background: #111824; border: none; outline: none; }
    QScrollBar:vertical { background: transparent; width: 8px; margin: 5px 1px; }
    QScrollBar::handle:vertical { background: #354359; border-radius: 3px; min-height: 35px; }
    QScrollBar::handle:vertical:hover { background: #53657e; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    QProgressBar { border: none; border-radius: 4px; background: #243347; min-height: 9px; max-height: 9px; }
    QProgressBar::chunk { background: #92dfd5; border-radius: 4px; }
    QTabWidget::pane { border: none; border-top: 1px solid #2d394c; margin-top: 12px; }
    QTabBar::tab { color: #93a3b9; padding: 11px 20px; background: transparent; border-bottom: 2px solid transparent; }
    QTabBar::tab:selected { color: #a7e5dd; border-bottom: 2px solid #92dfd5; }
    QTabBar::tab:hover { color: #e2eaf5; }
    QMenu { background: #1c293a; color: #dce5f1; border: 1px solid #3b4a5f; padding: 5px; }
    QMenu::item { padding: 7px 20px; border-radius: 4px; }
    QMenu::item:selected { background: #304456; }
    QToolTip { background: #26364b; color: #e2eaf5; border: 1px solid #465c74; padding: 6px; }
)");

} // namespace

struct MainWindow::Impl {
    explicit Impl(MainWindow* owner, const QString& dataDirectory) : window(owner), store(dataDirectory) {
        ready = store.open();
        build();
        selectChannel(QStringLiteral("general"));
        refreshProfile();
        refreshAudioButtons();
    }

    MainWindow* window;
    LocalStore store;
    bool ready = false;
    QString channelId = QStringLiteral("general");
    QString channelName = QStringLiteral("общий");
    QHash<QString, QString> drafts;
    QLabel* headerTitle = nullptr;
    QLabel* headerDescription = nullptr;
    QLabel* welcomeTitle = nullptr;
    QLabel* welcomeDescription = nullptr;
    QLabel* messageCount = nullptr;
    QLabel* profileLabel = nullptr;
    QLabel* memberName = nullptr;
    QLabel* localNotice = nullptr;
    QLabel* voiceName = nullptr;
    Avatar* profileAvatar = nullptr;
    Avatar* memberAvatar = nullptr;
    Composer* composer = nullptr;
    QLineEdit* search = nullptr;
    QListView* messages = nullptr;
    MessageModel* model = nullptr;
    QToolButton* micButton = nullptr;
    QToolButton* deafenButton = nullptr;
    QPushButton* sendButton = nullptr;
    QList<QPushButton*> channelButtons;

    void offline(const QString& action) {
        QMessageBox::information(window, QStringLiteral("Сервер пока не подключён"),
            QStringLiteral("%1\n\nСейчас Nimbo работает локально: сообщения хранятся только на этом компьютере. "
                           "Сетевые комнаты, приглашения и голосовая связь появятся после подключения сервера.")
                .arg(action));
    }

    void build() {
        window->setWindowTitle(QStringLiteral("Nimbo — место для своих"));
        window->setWindowIcon(icon("cloud", QColor(kAccent), 48));
        window->setMinimumSize(1100, 720);
        window->resize(1360, 850);
        window->setStyleSheet(kStyle);
        auto* root = new QWidget(window);
        auto* layout = new QHBoxLayout(root);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        window->setCentralWidget(root);
        buildRail(layout);
        buildSidebar(layout);
        buildChat(layout);
        buildMembers(layout);
    }

    void buildRail(QHBoxLayout* outer) {
        auto* rail = new QFrame;
        rail->setObjectName(QStringLiteral("rail"));
        rail->setFixedWidth(72);
        auto* layout = new QVBoxLayout(rail);
        layout->setContentsMargins(12, 18, 12, 16);
        layout->setSpacing(14);
        auto* brand = new QToolButton;
        brand->setFixedSize(46, 46);
        brand->setIcon(icon("cloud", QColor(kAccent), 32));
        brand->setIconSize(QSize(32, 32));
        brand->setToolTip(QStringLiteral("Nimbo · место для своих"));
        brand->setAccessibleName(QStringLiteral("О Nimbo"));
        QObject::connect(brand, &QToolButton::clicked, window, [this] { showAbout(); });
        layout->addWidget(brand);
        auto* divider = new QFrame;
        divider->setObjectName(QStringLiteral("divider"));
        divider->setFixedSize(32, 1);
        layout->addWidget(divider, 0, Qt::AlignHCenter);
        auto* friends = new QPushButton(QStringLiteral("Д"));
        friends->setObjectName(QStringLiteral("serverButton"));
        friends->setFixedSize(46, 46);
        friends->setCheckable(true);
        friends->setChecked(true);
        friends->setToolTip(QStringLiteral("Друзья · локальная демонстрация"));
        friends->setAccessibleName(QStringLiteral("Комната Друзья, локальная демонстрация"));
        QObject::connect(friends, &QPushButton::clicked, window, [this, friends] {
            friends->setChecked(true);
            selectChannel(QStringLiteral("general"));
        });
        layout->addWidget(friends);
        auto* studio = new QPushButton(QStringLiteral("М"));
        studio->setObjectName(QStringLiteral("serverButton"));
        studio->setFixedSize(46, 46);
        studio->setToolTip(QStringLiteral("Мастерская · пример второй комнаты"));
        studio->setAccessibleName(QStringLiteral("Комната Мастерская, пример"));
        QObject::connect(studio, &QPushButton::clicked, window, [this] {
            offline(QStringLiteral("«Мастерская» — пример второй комнаты. Создание отдельных серверов ещё не подключено."));
        });
        layout->addWidget(studio);
        auto* add = tool("plus", QStringLiteral("Создать или добавить комнату"));
        add->setFixedSize(46, 46);
        add->setIcon(icon("plus", QColor(kAccent)));
        QObject::connect(add, &QToolButton::clicked, window, [this] {
            offline(QStringLiteral("Создание комнаты будет доступно после настройки сервера."));
        });
        layout->addWidget(add);
        layout->addStretch();
        auto* help = tool("info", QStringLiteral("О прототипе"));
        QObject::connect(help, &QToolButton::clicked, window, [this] { showAbout(); });
        layout->addWidget(help, 0, Qt::AlignHCenter);
        outer->addWidget(rail);
    }

    void buildSidebar(QHBoxLayout* outer) {
        auto* sidebar = new QFrame;
        sidebar->setObjectName(QStringLiteral("sidebar"));
        sidebar->setFixedWidth(234);
        auto* layout = new QVBoxLayout(sidebar);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* heading = new QWidget;
        auto* headingLayout = new QVBoxLayout(heading);
        headingLayout->setContentsMargins(20, 24, 16, 19);
        headingLayout->setSpacing(7);
        headingLayout->addWidget(label(QStringLiteral("ВАШЕ ПРОСТРАНСТВО"), 10, QColor("#72849c"), true));
        auto* nameRow = new QHBoxLayout;
        nameRow->addWidget(label(QStringLiteral("Друзья"), 22, QColor(kText), true));
        nameRow->addStretch();
        auto* invite = tool("invite", QStringLiteral("Пригласить друзей"));
        QObject::connect(invite, &QToolButton::clicked, window, [this] {
            offline(QStringLiteral("Код приглашения пока недоступен."));
        });
        nameRow->addWidget(invite);
        headingLayout->addLayout(nameRow);
        headingLayout->addWidget(label(QStringLiteral("Свои люди. Спокойное место."), 12, QColor(kMuted)));
        layout->addWidget(heading);

        auto* channels = new QWidget;
        auto* channelLayout = new QVBoxLayout(channels);
        channelLayout->setContentsMargins(12, 12, 12, 0);
        channelLayout->setSpacing(5);
        auto* section = label(QStringLiteral("  ТЕКСТОВЫЕ КАНАЛЫ"), 10, QColor("#7889a0"), true);
        section->setContentsMargins(0, 0, 0, 7);
        channelLayout->addWidget(section);
        const QList<QPair<QString, QString>> definitions = {
            {QStringLiteral("general"), QStringLiteral("общий")},
            {QStringLiteral("ideas"), QStringLiteral("идеи")},
            {QStringLiteral("updates"), QStringLiteral("объявления")}
        };
        auto* group = new QButtonGroup(window);
        group->setExclusive(true);
        for (const auto& [id, name] : definitions) {
            auto* button = new QPushButton(QStringLiteral("  ") + name);
            button->setObjectName(QStringLiteral("channel") + id.left(1).toUpper() + id.mid(1));
            button->setProperty("channelButton", true);
            button->setProperty("channelId", id);
            button->setIcon(icon("hash", QColor("#869bb0"), 18));
            button->setIconSize(QSize(18, 18));
            button->setFixedHeight(40);
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            group->addButton(button);
            channelButtons.push_back(button);
            QObject::connect(button, &QPushButton::clicked, window, [this, id] { selectChannel(id); });
            channelLayout->addWidget(button);
        }
        channelLayout->addSpacing(25);
        section = label(QStringLiteral("  ГОЛОСОВЫЕ КАНАЛЫ"), 10, QColor("#7889a0"), true);
        section->setContentsMargins(0, 0, 0, 7);
        channelLayout->addWidget(section);
        for (const QString& name : {QStringLiteral("Гостиная"), QStringLiteral("За игрой")}) {
            auto* button = new QPushButton(QStringLiteral("  ") + name);
            button->setObjectName(QStringLiteral("channel"));
            button->setProperty("channelButton", true);
            button->setIcon(icon("speaker", QColor("#869bb0"), 18));
            button->setFixedHeight(40);
            button->setCursor(Qt::PointingHandCursor);
            QObject::connect(button, &QPushButton::clicked, window, [this, name] {
                voiceName->setText(name);
                offline(QStringLiteral("Голосовой канал «%1» пока недоступен.").arg(name));
            });
            channelLayout->addWidget(button);
        }
        layout->addWidget(channels);
        layout->addStretch();

        auto* roomHolder = new QWidget;
        auto* roomHolderLayout = new QVBoxLayout(roomHolder);
        roomHolderLayout->setContentsMargins(12, 15, 12, 14);
        auto* roomCard = new QFrame;
        roomCard->setObjectName(QStringLiteral("roomCard"));
        auto* roomLayout = new QVBoxLayout(roomCard);
        roomLayout->setContentsMargins(14, 14, 14, 13);
        roomLayout->setSpacing(8);
        roomLayout->addWidget(label(QStringLiteral("ГОЛОС РЯДОМ"), 10, QColor("#91b9bd"), true));
        auto* voiceRow = new QHBoxLayout;
        auto* voiceIcon = new QLabel;
        voiceIcon->setPixmap(icon("headphones", QColor("#b1d8dc"), 20).pixmap(20, 20));
        voiceRow->addWidget(voiceIcon);
        voiceName = label(QStringLiteral("Гостиная"), 14, QColor(kText), true);
        voiceRow->addWidget(voiceName);
        voiceRow->addStretch();
        roomLayout->addLayout(voiceRow);
        auto* voiceHint = label(QStringLiteral("Сервер пока не подключён.\nМикрофон не используется."), 11, QColor(kMuted));
        voiceHint->setWordWrap(true);
        roomLayout->addWidget(voiceHint);
        auto* join = new QPushButton(QStringLiteral("Подключиться"));
        join->setObjectName(QStringLiteral("secondary"));
        join->setFixedHeight(35);
        join->setIcon(icon("arrow", QColor("#c1d1de"), 17));
        QObject::connect(join, &QPushButton::clicked, window, [this] {
#ifdef NIMBO_MEDIA
            showMediaDialog(window);
#else
            offline(QStringLiteral("Голосовая связь ещё не подключена. Можно проверить свой микрофон в настройках звука."));
#endif
        });
        roomLayout->addWidget(join);
        roomHolderLayout->addWidget(roomCard);
        layout->addWidget(roomHolder);

        auto* profile = new QFrame;
        profile->setObjectName(QStringLiteral("profileCard"));
        auto* profileLayout = new QVBoxLayout(profile);
        profileLayout->setContentsMargins(16, 15, 14, 12);
        profileLayout->setSpacing(10);
        auto* identityRow = new QHBoxLayout;
        profileAvatar = new Avatar(QStringLiteral("ВЫ"), QColor("#315d65"), 35);
        identityRow->addWidget(profileAvatar);
        auto* identityText = new QVBoxLayout;
        identityText->setSpacing(3);
        profileLabel = label(QStringLiteral("Гость"), 13, QColor(kText), true);
        profileLabel->setMaximumWidth(150);
        identityText->addWidget(profileLabel);
        identityText->addWidget(label(QStringLiteral("Локальный профиль"), 10, QColor(kMuted)));
        identityRow->addLayout(identityText);
        identityRow->addStretch();
        profileLayout->addLayout(identityRow);
        auto* controls = new QHBoxLayout;
        controls->setSpacing(7);
        micButton = tool("mic", QStringLiteral("Микрофон: локальная настройка"));
        deafenButton = tool("headphones", QStringLiteral("Звук: локальная настройка"));
        micButton->setCheckable(true);
        deafenButton->setCheckable(true);
        QObject::connect(micButton, &QToolButton::clicked, window, [this](bool checked) {
            QSettings().setValue(QStringLiteral("audio/microphoneMuted"), checked); refreshAudioButtons();
        });
        QObject::connect(deafenButton, &QToolButton::clicked, window, [this](bool checked) {
            QSettings().setValue(QStringLiteral("audio/deafened"), checked); refreshAudioButtons();
        });
        controls->addWidget(micButton);
        controls->addWidget(deafenButton);
        controls->addStretch();
        auto* settings = tool("settings", QStringLiteral("Настройки профиля и звука"));
        settings->setObjectName(QStringLiteral("settingsButton"));
        QObject::connect(settings, &QToolButton::clicked, window, [this] { showSettings(); });
        controls->addWidget(settings);
        profileLayout->addLayout(controls);
        layout->addWidget(profile);
        outer->addWidget(sidebar);
    }

    void buildChat(QHBoxLayout* outer) {
        auto* center = new QWidget;
        center->setMinimumWidth(440);
        auto* layout = new QVBoxLayout(center);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* header = new QFrame;
        header->setObjectName(QStringLiteral("header"));
        header->setFixedHeight(76);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(24, 12, 20, 12);
        headerLayout->setSpacing(12);
        auto* hash = new QLabel;
        hash->setPixmap(icon("hash", QColor("#91b6c1"), 24).pixmap(24, 24));
        headerLayout->addWidget(hash);
        auto* titles = new QVBoxLayout;
        titles->setSpacing(3);
        headerTitle = label(QStringLiteral("общий"), 15, QColor(kText), true);
        headerDescription = label(QStringLiteral("Обо всём, что хочется обсудить"), 11, QColor(kMuted));
        headerDescription->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        titles->addWidget(headerTitle);
        titles->addWidget(headerDescription);
        headerLayout->addLayout(titles, 1);
        search = new QLineEdit;
        search->setObjectName(QStringLiteral("search"));
        search->setFixedWidth(155);
        search->setPlaceholderText(QStringLiteral("Поиск в канале"));
        search->setClearButtonEnabled(true);
        search->setAccessibleName(QStringLiteral("Поиск локальных сообщений в канале"));
        QObject::connect(search, &QLineEdit::textChanged, window, [this] { refreshMessages(false); });
        headerLayout->addWidget(search);
        layout->addWidget(header);

        auto* noticeHolder = new QWidget;
        auto* noticeHolderLayout = new QVBoxLayout(noticeHolder);
        noticeHolderLayout->setContentsMargins(24, 17, 24, 0);
        auto* notice = new QFrame;
        notice->setObjectName(QStringLiteral("localNotice"));
        auto* noticeLayout = new QHBoxLayout(notice);
        noticeLayout->setContentsMargins(12, 9, 12, 9);
        auto* noticeIcon = new QLabel;
        noticeIcon->setPixmap(icon("info", QColor("#83b9bf"), 16).pixmap(16, 16));
        noticeLayout->addWidget(noticeIcon);
        localNotice = label(ready
            ? QStringLiteral("Локальный прототип · сообщения остаются на этом устройстве")
            : QStringLiteral("База данных недоступна · отправка отключена"), 11,
            QColor(ready ? "#9ebfc9" : "#f0b1ad"));
        localNotice->setWordWrap(true);
        if (!ready) localNotice->setToolTip(store.errorString());
        noticeLayout->addWidget(localNotice, 1);
        noticeHolderLayout->addWidget(notice);
        layout->addWidget(noticeHolder);

        auto* welcome = new QWidget;
        auto* welcomeLayout = new QVBoxLayout(welcome);
        welcomeLayout->setContentsMargins(27, 29, 27, 15);
        welcomeLayout->setSpacing(9);
        auto* channelMark = new QLabel;
        channelMark->setFixedSize(46, 46);
        channelMark->setAlignment(Qt::AlignCenter);
        channelMark->setStyleSheet(QStringLiteral("background: #253347; border-radius: 15px;"));
        channelMark->setPixmap(icon("hash", QColor("#b8c6e5"), 26).pixmap(26, 26));
        welcomeLayout->addWidget(channelMark);
        welcomeLayout->addSpacing(3);
        welcomeTitle = label(QStringLiteral("Здесь начинается разговор"), 23, QColor("#e1e9f5"), true);
        welcomeTitle->setWordWrap(true);
        welcomeLayout->addWidget(welcomeTitle);
        welcomeDescription = label(QStringLiteral("Ваш небольшой уголок для больших и маленьких историй."), 12, QColor(kMuted));
        welcomeDescription->setWordWrap(true);
        welcomeLayout->addWidget(welcomeDescription);
        welcomeLayout->addSpacing(8);
        auto* lineRow = new QHBoxLayout;
        auto* leftLine = new QFrame;
        auto* rightLine = new QFrame;
        leftLine->setObjectName(QStringLiteral("divider"));
        rightLine->setObjectName(QStringLiteral("divider"));
        leftLine->setFixedHeight(1); rightLine->setFixedHeight(1);
        lineRow->addWidget(leftLine, 1);
        messageCount = label(QStringLiteral("ЛОКАЛЬНАЯ ИСТОРИЯ"), 9, QColor("#61758d"), true);
        lineRow->addWidget(messageCount);
        lineRow->addWidget(rightLine, 1);
        welcomeLayout->addLayout(lineRow);
        layout->addWidget(welcome);

        messages = new QListView;
        messages->setObjectName(QStringLiteral("messageList"));
        messages->setAccessibleName(QStringLiteral("История сообщений"));
        messages->setSelectionMode(QAbstractItemView::NoSelection);
        messages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        messages->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        messages->setResizeMode(QListView::Adjust);
        messages->setMouseTracking(true);
        model = new MessageModel(messages);
        messages->setModel(model);
        messages->setItemDelegate(new MessageDelegate(messages));
        messages->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(messages, &QWidget::customContextMenuRequested, window, [this](const QPoint& point) {
            const auto index = messages->indexAt(point);
            if (!index.isValid()) return;
            QMenu menu(window);
            auto* copy = menu.addAction(QStringLiteral("Копировать сообщение"));
            if (menu.exec(messages->viewport()->mapToGlobal(point)) == copy)
                QApplication::clipboard()->setText(index.data().toString());
        });
        layout->addWidget(messages, 1);

        auto* composerHolder = new QWidget;
        auto* holderLayout = new QVBoxLayout(composerHolder);
        holderLayout->setContentsMargins(24, 12, 24, 15);
        holderLayout->setSpacing(9);
        auto* composerCard = new QFrame;
        composerCard->setObjectName(QStringLiteral("composerCard"));
        auto* composerLayout = new QVBoxLayout(composerCard);
        composerLayout->setContentsMargins(14, 8, 12, 10);
        composerLayout->setSpacing(0);
        composer = new Composer;
        composer->setObjectName(QStringLiteral("messageComposer"));
        composer->setAccessibleName(QStringLiteral("Текст сообщения"));
        composer->setEnabled(ready);
        composer->sendRequested = [this] { sendMessage(); };
        composerLayout->addWidget(composer);
        auto* actionRow = new QHBoxLayout;
        auto* saveHint = label(QStringLiteral("Только на этом устройстве"), 10, QColor("#7d91a9"));
        actionRow->addWidget(saveHint);
        actionRow->addStretch();
        sendButton = new QPushButton(QStringLiteral("Отправить"));
        sendButton->setObjectName(QStringLiteral("sendButton"));
        sendButton->setIcon(icon("send", QColor("#1d4144"), 17));
        sendButton->setEnabled(false);
        sendButton->setCursor(Qt::PointingHandCursor);
        QObject::connect(sendButton, &QPushButton::clicked, window, [this] { sendMessage(); });
        QObject::connect(composer, &QPlainTextEdit::textChanged, window, [this] {
            sendButton->setEnabled(ready && !composer->toPlainText().trimmed().isEmpty());
        });
        actionRow->addWidget(sendButton);
        composerLayout->addLayout(actionRow);
        holderLayout->addWidget(composerCard);
        holderLayout->addWidget(label(QStringLiteral("Enter — отправить     Shift+Enter — новая строка"), 10,
                                      QColor("#61748d")), 0, Qt::AlignLeft);
        layout->addWidget(composerHolder);
        outer->addWidget(center, 1);
    }

    void buildMembers(QHBoxLayout* outer) {
        auto* members = new QFrame;
        members->setObjectName(QStringLiteral("members"));
        members->setFixedWidth(206);
        auto* layout = new QVBoxLayout(members);
        layout->setContentsMargins(18, 27, 16, 20);
        layout->setSpacing(18);
        layout->addWidget(label(QStringLiteral("УЧАСТНИКИ"), 10, QColor("#8294ac"), true));
        auto* selfRow = new QHBoxLayout;
        selfRow->setSpacing(10);
        memberAvatar = new Avatar(QStringLiteral("ВЫ"), QColor("#315d65"), 35);
        selfRow->addWidget(memberAvatar);
        auto* selfText = new QVBoxLayout;
        selfText->setSpacing(4);
        memberName = label(QStringLiteral("Гость"), 13, QColor(kText), true);
        memberName->setMaximumWidth(112);
        selfText->addWidget(memberName);
        selfText->addWidget(label(QStringLiteral("Вы · локально"), 10, QColor("#8eaea9")));
        selfRow->addLayout(selfText, 1);
        layout->addLayout(selfRow);
        auto* separator = new QFrame;
        separator->setObjectName(QStringLiteral("divider"));
        separator->setFixedHeight(1);
        layout->addWidget(separator);
        auto* note = label(QStringLiteral("Здесь будут ваши люди"), 13, QColor("#abb9cf"), true);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* detail = label(QStringLiteral("После подключения сервера здесь появятся участники комнаты и их настоящий статус."),
                              12, QColor("#788ca5"));
        detail->setWordWrap(true);
        layout->addWidget(detail);
        auto* invite = new QPushButton(QStringLiteral("Пригласить друзей"));
        invite->setObjectName(QStringLiteral("secondary"));
        invite->setFont(uiFont(11));
        QObject::connect(invite, &QPushButton::clicked, window, [this] {
            offline(QStringLiteral("Приглашения заработают вместе с серверными комнатами."));
        });
        layout->addWidget(invite);
        layout->addStretch();
        auto* mark = new QLabel;
        mark->setPixmap(icon("cloud", QColor("#435c72"), 30).pixmap(30, 30));
        layout->addWidget(mark);
        layout->addWidget(label(QStringLiteral("Меньше шума.\nБольше общения."), 13, QColor("#6c829d")));
        layout->addWidget(label(QStringLiteral("NIMBO  /  LOCAL PREVIEW"), 8, QColor("#485c75"), true));
        outer->addWidget(members);
    }

    void selectChannel(const QString& id) {
        drafts.insert(channelId, composer->toPlainText());
        channelId = id;
        composer->setPlainText(drafts.value(channelId));
        QString description;
        if (id == QStringLiteral("ideas")) {
            channelName = QStringLiteral("идеи");
            description = QStringLiteral("Мысли, планы и то, что хочется попробовать");
            welcomeTitle->setText(QStringLiteral("У хороших идей есть место"));
            welcomeDescription->setText(QStringLiteral("Сохраните мысль сейчас, чтобы вернуться к ней позже."));
        } else if (id == QStringLiteral("updates")) {
            channelName = QStringLiteral("объявления");
            description = QStringLiteral("Важное, чтобы ничего не потерялось");
            welcomeTitle->setText(QStringLiteral("Всё важное — рядом"));
            welcomeDescription->setText(QStringLiteral("Место для новостей, встреч и маленьких общих планов."));
        } else {
            channelName = QStringLiteral("общий");
            description = QStringLiteral("Обо всём, что хочется обсудить");
            welcomeTitle->setText(QStringLiteral("Здесь начинается разговор"));
            welcomeDescription->setText(QStringLiteral("Ваш небольшой уголок для больших и маленьких историй."));
        }
        headerTitle->setText(channelName);
        headerDescription->setText(description);
        headerDescription->setToolTip(description);
        composer->setPlaceholderText(QStringLiteral("Написать в #%1…").arg(channelName));
        for (auto* button : channelButtons) button->setChecked(button->property("channelId").toString() == id);
        if (!search->text().isEmpty()) search->clear();
        refreshMessages(true);
        window->setWindowTitle(QStringLiteral("#%1 · Nimbo").arg(channelName));
    }

    void refreshMessages(bool scrollToBottom) {
        auto items = ready ? store.messages(channelId) : QList<ChatMessage>{};
        if (ready && !store.errorString().isEmpty()) {
            model->replace({});
            messageCount->setText(QStringLiteral("История недоступна"));
            localNotice->setText(QStringLiteral("Не удалось прочитать историю. Попробуйте открыть канал снова."));
            localNotice->setToolTip(store.errorString());
            return;
        }
        if (ready) {
            localNotice->setText(QStringLiteral("Локальный прототип · сообщения остаются на этом устройстве"));
            localNotice->setToolTip(QString());
        }
        const auto query = search->text().trimmed();
        if (!query.isEmpty()) {
            items.erase(std::remove_if(items.begin(), items.end(), [&query](const ChatMessage& message) {
                return !message.body.contains(query, Qt::CaseInsensitive)
                    && !message.author.contains(query, Qt::CaseInsensitive);
            }), items.end());
        }
        const int position = messages->verticalScrollBar()->value();
        model->replace(std::move(items));
        messageCount->setText(query.isEmpty()
            ? QStringLiteral("ЛОКАЛЬНАЯ ИСТОРИЯ · %1").arg(model->rowCount())
            : QStringLiteral("НАЙДЕНО · %1").arg(model->rowCount()));
        if (scrollToBottom) QTimer::singleShot(0, messages, [this] { messages->scrollToBottom(); });
        else messages->verticalScrollBar()->setValue(position);
    }

    void sendMessage() {
        const auto body = composer->toPlainText().trimmed();
        if (!ready || body.isEmpty()) return;
        if (body.size() > 4000) {
            QMessageBox::information(window, QStringLiteral("Сообщение слишком длинное"),
                                      QStringLiteral("Разделите текст на несколько сообщений: максимум 4000 символов."));
            return;
        }
        if (!store.addMessage(channelId, store.profileName(), body)) {
            QMessageBox::warning(window, QStringLiteral("Не удалось сохранить сообщение"), store.errorString());
            return;
        }
        composer->clear();
        search->clear();
        refreshMessages(true);
        composer->setFocus();
    }

    void refreshProfile() {
        const auto name = store.profileName().isEmpty() ? QStringLiteral("Гость") : store.profileName();
        profileLabel->setText(QFontMetrics(profileLabel->font()).elidedText(name, Qt::ElideRight, 145));
        memberName->setText(QFontMetrics(memberName->font()).elidedText(name, Qt::ElideRight, 112));
        profileLabel->setToolTip(name); memberName->setToolTip(name);
        profileAvatar->setInitials(initials(name)); memberAvatar->setInitials(initials(name));
    }

    void refreshAudioButtons() {
        const QSettings settings;
        const bool muted = settings.value(QStringLiteral("audio/microphoneMuted"), false).toBool();
        const bool deafened = settings.value(QStringLiteral("audio/deafened"), false).toBool();
        micButton->setChecked(muted);
        deafenButton->setChecked(deafened);
        micButton->setIcon(icon(muted ? "mic-off" : "mic", QColor(muted ? "#e3a7b0" : "#9caec4")));
        deafenButton->setIcon(icon(deafened ? "headphones-off" : "headphones", QColor(deafened ? "#e3a7b0" : "#9caec4")));
        const auto micHint = muted
            ? QStringLiteral("Микрофон выключен в локальных настройках. Голосового подключения нет.")
            : QStringLiteral("Микрофон разрешён в локальных настройках. Сейчас запись не идёт.");
        const auto soundHint = deafened
            ? QStringLiteral("Звук выключен в локальных настройках. Голосового подключения нет.")
            : QStringLiteral("Звук разрешён в локальных настройках. Голосового подключения нет.");
        micButton->setToolTip(micHint); micButton->setAccessibleName(micHint);
        deafenButton->setToolTip(soundHint); deafenButton->setAccessibleName(soundHint);
    }

    void showAbout() {
        QMessageBox message(window);
        message.setWindowTitle(QStringLiteral("О Nimbo"));
        message.setTextFormat(Qt::PlainText);
        message.setText(QStringLiteral("Nimbo\nМесто для своих."));
        message.setInformativeText(QStringLiteral(
            "Локальный нативный прототип. Навигация, история сообщений, профиль и проверка микрофона работают на этом компьютере.\n\n"
            "Сервер, сетевые комнаты, звонки и приглашения пока не подключены. Сообщения не отправляются другим людям."));
        message.setDetailedText(QStringLiteral("Локальная база:\n%1").arg(store.databasePath()));
        message.exec();
    }

    void showSettings() {
        QDialog dialog(window);
        dialog.setWindowTitle(QStringLiteral("Настройки Nimbo"));
        dialog.setMinimumWidth(550);
        dialog.resize(580, 580);
        dialog.setStyleSheet(kStyle);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(25, 23, 25, 20);
        layout->setSpacing(12);
        layout->addWidget(label(QStringLiteral("Устроиться поудобнее"), 22, QColor(kText), true));
        layout->addWidget(label(QStringLiteral("Профиль и звук на этом устройстве"), 12, QColor(kMuted)));
        auto* tabs = new QTabWidget;
        auto* audioPage = new QWidget;
        auto* audioLayout = new QVBoxLayout(audioPage);
        audioLayout->setContentsMargins(0, 22, 0, 12);
        audioLayout->setSpacing(11);
        audioLayout->addWidget(label(QStringLiteral("МИКРОФОН"), 10, QColor("#9baec4"), true));
        auto* inputs = new QComboBox;
        inputs->setAccessibleName(QStringLiteral("Устройство ввода звука"));
        audioLayout->addWidget(inputs);
        audioLayout->addSpacing(5);
        audioLayout->addWidget(label(QStringLiteral("НАУШНИКИ ИЛИ ДИНАМИКИ"), 10, QColor("#9baec4"), true));
        auto* outputs = new QComboBox;
        outputs->setAccessibleName(QStringLiteral("Устройство вывода звука"));
        audioLayout->addWidget(outputs);
        auto* savedDeviceNote = label(QStringLiteral("Выбор устройств сохраняется. Голосовой сервер пока не подключён."), 11, QColor(kMuted));
        savedDeviceNote->setWordWrap(true);
        audioLayout->addWidget(savedDeviceNote);
        audioLayout->addSpacing(14);
        auto* testRow = new QHBoxLayout;
        auto* testButton = new QPushButton(QStringLiteral("Проверить микрофон"));
        testButton->setObjectName(QStringLiteral("secondary"));
        testButton->setIcon(icon("mic", QColor("#bfd9df"), 17));
        testRow->addWidget(testButton);
        testRow->addStretch();
        auto* testStatus = label(QStringLiteral("Проверка не запущена"), 11, QColor(kMuted));
        testRow->addWidget(testStatus);
        audioLayout->addLayout(testRow);
        auto* meter = new QProgressBar;
        meter->setRange(0, 100); meter->setValue(0); meter->setTextVisible(false);
        meter->setAccessibleName(QStringLiteral("Уровень микрофона"));
        audioLayout->addWidget(meter);
        auto* privacy = label(QStringLiteral("Во время проверки микрофон включается локально.\nЗвук никуда не передаётся и не сохраняется."), 11, QColor(kMuted));
        privacy->setWordWrap(true);
        audioLayout->addWidget(privacy);
        auto* error = label(QString(), 11, QColor("#e5a9b0"));
        error->setWordWrap(true);
        error->setMinimumHeight(20);
        audioLayout->addWidget(error);
        audioLayout->addStretch();
        tabs->addTab(audioPage, QStringLiteral("Звук"));

        auto* profilePage = new QWidget;
        auto* profileLayout = new QVBoxLayout(profilePage);
        profileLayout->setContentsMargins(0, 22, 0, 12);
        profileLayout->setSpacing(12);
        profileLayout->addWidget(label(QStringLiteral("ВАШЕ ИМЯ"), 10, QColor("#9baec4"), true));
        auto* name = new QLineEdit(store.profileName());
        name->setMaxLength(32);
        name->setPlaceholderText(QStringLiteral("Как вас называть?"));
        name->setAccessibleName(QStringLiteral("Имя локального профиля"));
        profileLayout->addWidget(name);
        auto* profileNote = label(QStringLiteral("Имя используется в новых локальных сообщениях. Это ещё не сетевой аккаунт."), 12, QColor(kMuted));
        profileNote->setWordWrap(true);
        profileLayout->addWidget(profileNote);
        profileLayout->addStretch();
        auto* databaseLabel = label(QStringLiteral("ЛОКАЛЬНАЯ БАЗА"), 10, QColor("#8294ac"), true);
        profileLayout->addWidget(databaseLabel);
        auto* path = label(store.databasePath(), 11, QColor(kMuted));
        path->setWordWrap(true);
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        profileLayout->addWidget(path);
        tabs->addTab(profilePage, QStringLiteral("Профиль"));
        layout->addWidget(tabs, 1);

        auto* probe = new AudioProbe(&dialog);
        auto populate = [probe, inputs, outputs](const QByteArray& inputId, const QByteArray& outputId) {
            inputs->blockSignals(true); outputs->blockSignals(true);
            inputs->clear(); outputs->clear();
            const auto recording = probe->inputDevices();
            const auto playback = probe->outputDevices();
            inputs->addItem(recording.isEmpty() ? QStringLiteral("Микрофоны не найдены") : QStringLiteral("Системный микрофон"), QByteArray());
            outputs->addItem(playback.isEmpty() ? QStringLiteral("Устройства вывода не найдены") : QStringLiteral("Системное устройство вывода"), QByteArray());
            for (const auto& device : recording) inputs->addItem(device.description(), device.id());
            for (const auto& device : playback) outputs->addItem(device.description(), device.id());
            inputs->setCurrentIndex(std::max(0, inputs->findData(inputId)));
            outputs->setCurrentIndex(std::max(0, outputs->findData(outputId)));
            inputs->setEnabled(!recording.isEmpty()); outputs->setEnabled(!playback.isEmpty());
            inputs->blockSignals(false); outputs->blockSignals(false);
        };
        const QSettings settings;
        populate(settings.value(QStringLiteral("audio/inputDevice")).toByteArray(),
                 settings.value(QStringLiteral("audio/outputDevice")).toByteArray());
        testButton->setEnabled(!probe->inputDevices().isEmpty());
        QObject::connect(probe, &AudioProbe::devicesChanged, &dialog, [=] {
            const auto inputId = inputs->currentData().toByteArray();
            const auto outputId = outputs->currentData().toByteArray();
            probe->stop();
            populate(inputId, outputId);
            testButton->setEnabled(!probe->inputDevices().isEmpty());
        });
        QObject::connect(inputs, &QComboBox::currentIndexChanged, &dialog, [probe, error] {
            probe->stop(); error->clear();
        });
        QObject::connect(testButton, &QPushButton::clicked, &dialog, [=] {
            error->clear();
            if (probe->isRunning()) probe->stop();
            else probe->start(inputs->currentData().toByteArray());
        });
        QObject::connect(probe, &AudioProbe::levelChanged, &dialog, [meter](float value) {
            meter->setValue(std::clamp(static_cast<int>(value * 100.0f), 0, 100));
        });
        QObject::connect(probe, &AudioProbe::errorOccurred, &dialog, [error](const QString& text) { error->setText(text); });
        QObject::connect(probe, &AudioProbe::runningChanged, &dialog, [=](bool running) {
            testButton->setText(running ? QStringLiteral("Остановить проверку") : QStringLiteral("Проверить микрофон"));
            testStatus->setText(running ? QStringLiteral("Микрофон используется") : QStringLiteral("Проверка не запущена"));
            if (!running) meter->setValue(0);
        });
        QObject::connect(&dialog, &QDialog::finished, probe, [probe] { probe->stop(); });

        auto* buttons = new QDialogButtonBox;
        auto* cancel = buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
        auto* save = buttons->addButton(QStringLiteral("Сохранить"), QDialogButtonBox::AcceptRole);
        cancel->setObjectName(QStringLiteral("secondary"));
        save->setObjectName(QStringLiteral("primary"));
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, name, inputs, outputs] {
            const auto profile = name->text().simplified();
            store.setProfileName(profile.isEmpty() ? QStringLiteral("Гость") : profile);
            const auto profileError = store.errorString();
            if (!profileError.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("Не удалось сохранить профиль"), profileError);
                return;
            }
            QSettings preferences;
            preferences.setValue(QStringLiteral("audio/inputDevice"), inputs->currentData().toByteArray());
            preferences.setValue(QStringLiteral("audio/outputDevice"), outputs->currentData().toByteArray());
            preferences.sync();
            if (preferences.status() != QSettings::NoError) {
                QMessageBox::warning(&dialog, QStringLiteral("Не удалось сохранить настройки"),
                    QStringLiteral("Проверьте доступ к папке данных Nimbo и попробуйте ещё раз."));
                return;
            }
            refreshProfile();
            dialog.accept();
        });
        layout->addWidget(buttons);
        dialog.exec();
        probe->stop();
    }
};

MainWindow::MainWindow(const QString& dataDirectory, QWidget* parent)
    : QMainWindow(parent), impl_(std::make_unique<Impl>(this, dataDirectory)) {}

MainWindow::~MainWindow() = default;
