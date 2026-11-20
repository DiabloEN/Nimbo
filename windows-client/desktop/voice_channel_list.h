#pragma once
#include <QApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <functional>
#include <memory>

class VoiceChannelList final : public QListWidget {
public:
    VoiceChannelList(){
        setMouseTracking(true);
        connect(model(),&QAbstractItemModel::modelReset,this,[this]{setActivityVisible(false);});
        connect(&activityTimer,&QTimer::timeout,viewport(),qOverload<>(&QWidget::update));
    }
    void setActivityVisible(bool visible){
        hasActivity=visible;
        if(hasActivity && isVisible()){
            if(!activityTimer.isActive())activityTimer.start(1000);
        }else activityTimer.stop();
    }
    std::function<bool(QString)> canMove;
    std::function<QString()> origin;
    std::function<void(QString,QString,QString)> moveMember;
protected:
    void hideEvent(QHideEvent* event) override{
        activityTimer.stop();cancel();QListWidget::hideEvent(event);
    }
    void showEvent(QShowEvent* event) override{
        QListWidget::showEvent(event);setActivityVisible(hasActivity);
    }
    void mousePressEvent(QMouseEvent* event) override{
        cancel();
        start=event->position().toPoint();
        auto* row=itemAt(start);
        if(row && event->button()==Qt::LeftButton){
            const auto id=draggablePerson(row);
            if(!id.isEmpty()){
                identity=id;
                from=row->data(Qt::UserRole+5).toString();
                server=origin();
                event->accept();return;
            }
            const auto room=row->data(Qt::UserRole).toJsonObject();
            if(room["kind"]=="voice"){
                // Presence refreshes can replace this item before the mouse is released.
                pressedRoom=room["id"].toString();
                server=origin();
                event->accept();return;
            }
        }
        QListWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override{
        const auto pos=event->position().toPoint();
        if(identity.isEmpty()){
            updateCursor(pos);
            if(pressedRoom.isEmpty())QListWidget::mouseMoveEvent(event);
            return;
        }
        auto* source=sourceRow();
        if(!event->buttons().testFlag(Qt::LeftButton) || !source){cancel();return;}
        if(!ghost && (pos-start).manhattanLength()<QApplication::startDragDistance())return;
        if(!ghost)startPreview(source);
        if(viewport()->rect().contains(pos)){
            auto* bar=verticalScrollBar();
            if(pos.y()<12)bar->setValue(bar->value()-bar->singleStep());
            if(pos.y()>viewport()->height()-12)bar->setValue(bar->value()+bar->singleStep());
        }
        ghost->move(viewport()->mapToGlobal(pos-hotspot));
        const auto destination=target(pos);
        setProperty("voiceDrop",destination);
        viewport()->setCursor(destination.isEmpty()?Qt::ForbiddenCursor:Qt::ClosedHandCursor);
        viewport()->update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override{
        const auto pos=event->position().toPoint();
        if(!pressedRoom.isEmpty()){
            auto* row=itemAt(pos);
            const auto clicked=row?row->data(Qt::UserRole).toJsonObject():QJsonObject{};
            const bool activate=event->button()==Qt::LeftButton && server==origin() && clicked["id"]==pressedRoom;
            cancel();
            if(activate){
                setCurrentItem(row);
                auto* current=currentItem();
                if(current && current->data(Qt::UserRole).toJsonObject()["id"]==clicked["id"])emit itemClicked(current);
            }
            event->accept();return;
        }
        if(identity.isEmpty()){QListWidget::mouseReleaseEvent(event);return;}
        const QString id=identity,source=from,destination=target(pos);
        const bool drop=ghost && event->button()==Qt::LeftButton && !destination.isEmpty() && sourceRow();
        cancel();
        if(drop)moveMember(id,source,destination);
        updateCursor(pos);
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override{
        if(event->key()==Qt::Key_Escape && (!identity.isEmpty() || !pressedRoom.isEmpty())){cancel();event->accept();return;}
        QListWidget::keyPressEvent(event);
    }
    bool viewportEvent(QEvent* event) override{
        if(event->type()==QEvent::UngrabMouse || event->type()==QEvent::Hide)cancel();
        return QListWidget::viewportEvent(event);
    }
private:
    QTimer activityTimer;
    bool hasActivity=false;
    QPoint start,hotspot;
    QString identity,from,server,pressedRoom;
    std::unique_ptr<QLabel> ghost;
    QString draggablePerson(const QListWidgetItem* row) const{
        if(!row)return {};
        const auto person=row->data(Qt::UserRole+2).toJsonObject();
        const auto id=person["identity"].toString();
        return !id.isEmpty() && !person["pending"].toBool() && !person["reconnecting"].toBool() && canMove(id)?id:QString{};
    }
    QListWidgetItem* sourceRow() const{
        if(server!=origin() || !canMove(identity))return nullptr;
        for(int i=0;i<count();++i){
            auto* row=item(i);
            if(row->data(Qt::UserRole+5).toString()==from && row->data(Qt::UserRole+2).toJsonObject()["identity"]==identity)
                return draggablePerson(row).isEmpty()?nullptr:row;
        }
        return nullptr;
    }
    void startPreview(QListWidgetItem* row){
        QStyleOptionViewItem option;initViewItemOption(&option);
        const QRect rect=visualItemRect(row);
        option.rect=QRect(QPoint(),rect.size());
        QPixmap pixmap(rect.size()*viewport()->devicePixelRatioF());
        pixmap.setDevicePixelRatio(viewport()->devicePixelRatioF());
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);painter.setOpacity(.65);
        itemDelegate()->paint(&painter,option,indexFromItem(row));painter.end();
        ghost=std::make_unique<QLabel>(this,Qt::ToolTip|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint|
                                           Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        ghost->setAttribute(Qt::WA_TranslucentBackground);
        ghost->setAttribute(Qt::WA_ShowWithoutActivating);
        ghost->setAttribute(Qt::WA_DontShowOnScreen,window()->testAttribute(Qt::WA_DontShowOnScreen));
        ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
        ghost->setStyleSheet("background:transparent;padding:0;border:0");
        ghost->setPixmap(pixmap);ghost->resize(rect.size());
        hotspot=start-rect.topLeft();ghost->move(viewport()->mapToGlobal(rect.topLeft()));ghost->show();
        setProperty("voiceDrag",identity);
    }
    QString target(QPoint pos) const{
        auto* row=itemAt(pos);
        if(!row)return {};
        const auto room=row->data(Qt::UserRole).toJsonObject();
        const auto id=room["kind"]=="voice"?room["id"].toString():row->data(Qt::UserRole+5).toString();
        return id!=from?id:QString{};
    }
    void updateCursor(QPoint pos){
        auto* row=itemAt(pos);
        if(!draggablePerson(row).isEmpty())viewport()->setCursor(Qt::OpenHandCursor);
        else if(row && !row->data(Qt::UserRole).toJsonObject().isEmpty())viewport()->setCursor(Qt::PointingHandCursor);
        else viewport()->unsetCursor();
    }
    void cancel(){
        identity.clear();from.clear();server.clear();pressedRoom.clear();
        if(ghost){
            ghost.reset();
            setProperty("voiceDrag",QString{});setProperty("voiceDrop",QString{});
            viewport()->unsetCursor();viewport()->update();
        }
    }
};
