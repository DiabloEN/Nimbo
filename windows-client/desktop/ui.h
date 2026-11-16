#pragma once
#include <QtWidgets>

namespace Ui {
bool animationsEnabled();
inline void compactButton(QPushButton* b){
    b->setFixedHeight(38);b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet("QPushButton{background:#202020;color:#ededed;border:1px solid transparent;border-radius:11px;padding:0 15px;font-size:12px} QPushButton:hover:enabled{background:#303030} QPushButton:focus{border-color:#ddd} QPushButton:disabled{background:#151515;color:#777}");
}
inline QIcon icon(const QString& name) {
    static const QRegularExpression valid("^[a-zA-Z0-9_-]+$");
    static QCache<QString,QIcon> icons(64);
    if(auto* cached=icons.object(name))return *cached;
    if (!valid.match(name).hasMatch()) return {};
    QIcon result(QCoreApplication::applicationDirPath()+"/assets/"+name+".png");
    icons.insert(name,new QIcon(result));
    return result;
}
inline QIcon tintedIcon(const QString& name,QColor color){
    static QCache<QPair<QString,QRgb>,QIcon> icons(64);
    const auto key=qMakePair(name,color.rgba());
    if(auto* cached=icons.object(key))return *cached;
    QPixmap glyph=icon(name).pixmap(48,48);
    QPainter paint(&glyph);paint.setCompositionMode(QPainter::CompositionMode_SourceIn);paint.fillRect(glyph.rect(),color);paint.end();
    QIcon result;result.addPixmap(glyph,QIcon::Normal);result.addPixmap(glyph,QIcon::Disabled);
    icons.insert(key,new QIcon(result));
    return result;
}
class CheckBox final : public QCheckBox {
public:
    explicit CheckBox(const QString& label,int size=15,int gap=8,QColor checked=QColor("#ddd")):QCheckBox(label),side(size),checkedColor(checked){
        setStyleSheet(QString("QCheckBox{background:transparent;font-size:12px;spacing:%2px} "
                              "QCheckBox::indicator{width:%1px;height:%1px;border:0;background:transparent}").arg(side).arg(gap));
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option;initStyleOption(&option);
        auto slot=style()->subElementRect(QStyle::SE_CheckBoxIndicator,&option,this);
        QRectF box(0,0,side-1,side-1);box.moveCenter(QRectF(slot).center());
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        if(!isEnabled())p.setOpacity(.45);
        p.setPen(QPen(isChecked()?checkedColor:QColor("#858585"),1));
        p.setBrush(isChecked()?checkedColor:QColor("#3b3b3b"));p.drawRoundedRect(box,2,2);
        if(isChecked()){
            QPainterPath tick;tick.moveTo(box.left()+.18*side,box.top()+.48*side);
            tick.lineTo(box.left()+.40*side,box.top()+.70*side);tick.lineTo(box.left()+.80*side,box.top()+.22*side);
            p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#303030"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(tick);
        }
    }
private:
    int side;
    QColor checkedColor;
};
// QComboBox's platform delegate measures menu entries differently from list-view rows.
class SelectOption final : public QStyledItemDelegate {
public:
    explicit SelectOption(QComboBox* box):QStyledItemDelegate(box),box(box){}
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        auto size=QStyledItemDelegate::sizeHint(option,index);
        const int available=qMax(1,box->width()-34);
        const int wrappedHeight=option.fontMetrics.boundingRect(QRect(0,0,available,0),Qt::TextWordWrap|Qt::TextWrapAnywhere,index.data().toString()).height();
        size.setHeight(option.fontMetrics.horizontalAdvance(index.data().toString())<=available ? 36 : qMax(36,wrappedHeight+22));
        return size;
    }
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        const bool active=option.state.testFlag(QStyle::State_Selected);
        p->save();p->setClipRect(option.rect);
        p->fillRect(option.rect,active?QColor("#353535"):QColor("#242424"));
        p->setFont(option.font);p->setPen(active || index.row()==box->currentIndex()?Qt::white:QColor("#a0a0a0"));
        p->drawText(option.rect.adjusted(17,0,-17,0),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextWordWrap|Qt::TextWrapAnywhere,index.data().toString());
        p->restore();
    }
private:
    QComboBox* box;
};
class SelectField final : public QComboBox {
public:
    void showPopup() override {
        if(!count())return;
        setProperty("expanded",true);style()->unpolish(this);style()->polish(this);
        QComboBox::showPopup();
        view()->viewport()->installEventFilter(this);
        auto* popup=view()->window();
        const auto below=mapToGlobal(QPoint(0,height()));
        const auto screenRect=screen()->availableGeometry();
        int rows=view()->contentsMargins().top()+view()->contentsMargins().bottom();
        for(int i=0;i<count() && rows<270;++i)rows+=view()->sizeHintForRow(i);
        rows=qMin(rows,270);
        const int top=below.y()+rows<=screenRect.bottom()+1?below.y():qMax(screenRect.top(),below.y()-height()-rows);
        popup->setGeometry(below.x(),top,width(),rows);
        QPainterPath corners;
        corners.setFillRule(Qt::WindingFill);corners.addRoundedRect(QRectF(0,0,width(),rows),12,12);
        corners.addRect(QRectF(0,top==below.y()?0:rows-12,width(),12));
        popup->setMask(QRegion(corners.simplified().toFillPolygon().toPolygon()));
    }
    bool eventFilter(QObject* object,QEvent* e)override{
        if(e->type()==QEvent::MouseButtonPress && object==view()->viewport()){
            auto* press=static_cast<QMouseEvent*>(e);
            const auto target=view()->indexAt(press->position().toPoint());
            if(press->button()==Qt::LeftButton && target.isValid() && (target.flags()&Qt::ItemIsEnabled)){
                auto guard=QPointer<SelectField>(this);
                const auto label=target.data().toString();const int row=target.row();
                view()->window()->setAttribute(Qt::WA_NoMouseReplay);
                hidePopup();setCurrentIndex(row);
                if(guard)emit activated(row);
                if(guard)emit textActivated(label);
                return true;
            }
        }
        return QComboBox::eventFilter(object,e);
    }
    void keyPressEvent(QKeyEvent* e)override{
        switch(e->key()){
        case Qt::Key_Space:case Qt::Key_Return:case Qt::Key_Enter:
        case Qt::Key_Up:case Qt::Key_Down:case Qt::Key_Home:case Qt::Key_End:
            if(!view()->isVisible()){showPopup();e->accept();return;}
        }
        QComboBox::keyPressEvent(e);
    }
    void hidePopup() override {
        QComboBox::hidePopup();setProperty("expanded",false);style()->unpolish(this);style()->polish(this);update();
    }
};
inline QComboBox* select(const QString& label,int symmetricPadding=-1){
    auto* box=new SelectField;
    auto* list=new QListView;list->setFrameShape(QFrame::NoFrame);
    list->setStyleSheet(QString("QListView{background:#242424;border:0;padding:%1px 0 %2px;outline:0;font-size:12px}")
                       .arg(symmetricPadding<0?5:symmetricPadding).arg(symmetricPadding<0?8:symmetricPadding));
    list->setMouseTracking(true);list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    box->setView(list);box->setItemDelegate(new SelectOption(box));
    box->setAccessibleName(label);box->setFixedHeight(45);
    const QString arrow=QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()+"/assets/chevronDown.png");
    box->setStyleSheet(QString("QComboBox{background:#202020;color:#ededed;border:0;border-radius:10px;padding:0 40px 0 16px;font-size:12px}"
        "QComboBox:hover{background:#303030} QComboBox[expanded=true]{border-bottom-left-radius:0;border-bottom-right-radius:0} QComboBox::drop-down{background:transparent;border:0;width:48px}"
        "QComboBox::down-arrow{image:url(\"%1\");width:16px;height:16px}").arg(arrow));
    return box;
}
inline QLabel* text(const QString& value, const QString& style = {}) {
    auto* label = new QLabel(value);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet("background:transparent;border:0;"+style);
    return label;
}
class IconButton final : public QPushButton {
public:
    void setHoverIcons(const QIcon& normal,const QIcon& hot){resting=normal;hovered=hot;setIcon(normal);}
protected:
    void enterEvent(QEnterEvent* event) override {QPushButton::enterEvent(event);if(!hovered.isNull())setIcon(hovered);}
    void leaveEvent(QEvent* event) override {QPushButton::leaveEvent(event);if(!resting.isNull())setIcon(resting);}
private:
    QIcon resting,hovered;
};
inline IconButton* action(const QString& name,const QString& tip,int size=34) {
    auto* b=new IconButton;
    b->setIcon(icon(name)); b->setIconSize(QSize(18,18)); b->setFixedSize(size,size);
    b->setProperty("flatIcon",true); b->setAccessibleName(tip); b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor); return b;
}
inline QPixmap avatar(QString name,const QPixmap& source,int size,bool square=false) {
    QPixmap result(size*2,size*2);result.setDevicePixelRatio(2);result.fill(Qt::transparent);
    QPainter p(&result);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal radius=square?(size>=60?16:10):size/2.;
    QPainterPath clip;clip.addRoundedRect(QRectF(0,0,size,size),radius,radius);
    p.setClipPath(clip);
    if(!source.isNull()){
        const int side=qMin(source.width(),source.height());
        p.drawPixmap(QRect(0,0,size,size),source,QRect((source.width()-side)/2,(source.height()-side)/2,side,side));
    }else{
        p.fillRect(QRect(0,0,size,size),square?QColor("#ddd"):QColor("#303030"));
        QFont font("Roboto");font.setPixelSize(square?(size>=60?36:24):qRound(size*.34));
        font.setWeight(square?QFont::Normal:QFont::Medium);p.setFont(font);
        if(square){
            auto chars=name.trimmed().toUcs4();
            auto initial=chars.empty()?QStringLiteral("N"):QString::fromUcs4(chars.constData(),1).toUpper();
            font.setLetterSpacing(QFont::AbsoluteSpacing,-1);p.setFont(font);
            auto dotFont=font;dotFont.setWeight(QFont::Medium);
            const QFontMetricsF fm(font);const qreal advance=fm.horizontalAdvance(initial);
            const qreal left=(size-advance-QFontMetricsF(dotFont).horizontalAdvance("."))/2;
            const qreal baseline=(size-fm.height())/2+fm.ascent();
            p.setPen(QColor("#111"));p.drawText(QPointF(left,baseline),initial);
            p.setFont(dotFont);p.setPen(QColor("#888"));p.drawText(QPointF(left+advance,baseline),".");
        }else{
            p.setPen(Qt::white);p.drawText(QRect(0,0,size,size),Qt::AlignCenter,name.left(2).toUpper());
        }
    }
    return result;
}
inline QPixmap imageData(QString url){
    QPixmap p;if(url.startsWith("data:image/")&&url.size()<=200000)p.loadFromData(QByteArray::fromBase64(url.section(',',1).toLatin1()));return p;
}
inline void clear(QLayout* layout){
    while(auto* item=layout->takeAt(0)){
        if(auto* w=item->widget()){w->hide();w->deleteLater();}
        if(auto* child=item->layout())clear(child);
        delete item;
    }
}
inline void fade(QWidget* widget,int duration=150) {
    if(!animationsEnabled())return;
    auto* effect=new QGraphicsOpacityEffect(widget);
    widget->setGraphicsEffect(effect);
    auto* animation=new QPropertyAnimation(effect,"opacity",effect);
    animation->setStartValue(0.2);animation->setEndValue(1.);animation->setDuration(duration);
    QObject::connect(animation,&QPropertyAnimation::finished,widget,[widget,effect]{
        if(widget->graphicsEffect()==effect)widget->setGraphicsEffect(nullptr);
    });
    animation->start();
}
inline QJsonObject byId(const QJsonArray& items,const QString& id){
    for(const auto& v:items)if(v.toObject()["id"]==id)return v.toObject();return {};
}
}
