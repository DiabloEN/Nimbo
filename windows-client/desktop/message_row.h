#pragma once
#include "ui.h"

class MessageRow final : public QWidget {
public:
    MessageRow() {
        setFocusPolicy(Qt::TabFocus);
        setObjectName("chatMessage");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet("QWidget#chatMessage{background:transparent;border-radius:12px}"
                      "QWidget#chatMessage:hover,QWidget#chatMessage[focused=true]{background:#111}");
        connect(qApp,&QApplication::focusChanged,this,[this](QWidget*,QWidget* now){
            const bool active=now && (now==this || isAncestorOf(now)) && now->testAttribute(Qt::WA_KeyboardFocusChange);
            if(active==focused) return;
            focused=active;
            setProperty("focused",active);
            style()->unpolish(this);style()->polish(this);
            positionActions();
        });
    }

    std::function<void()> createActions;
    Ui::IconButton* actionButton(QString glyph,QString name,QString tip) {
        if(!bar) {
            bar=new QWidget(this);
            bar->setObjectName("messageActions");
            bar->setAttribute(Qt::WA_StyledBackground);
            bar->setStyleSheet("QWidget#messageActions{background:#262626;border-radius:10px}"
                "QPushButton{border:0;background:transparent;border-radius:8px;padding:0}"
                "QPushButton:hover,QPushButton:focus{background:#3b3b3b}");
            buttons=new QHBoxLayout(bar);
            buttons->setContentsMargins(3,2,3,2);buttons->setSpacing(2);
        }
        auto* button=Ui::action(glyph,name,30);
        button->setFocusPolicy(Qt::TabFocus);button->setToolTip(tip);
        button->setHoverIcons(Ui::tintedIcon(glyph,QColor("#b8b8b8")),Ui::tintedIcon(glyph,Qt::white));
        buttons->addWidget(button);
        return button;
    }
protected:
    void enterEvent(QEnterEvent* e) override { QWidget::enterEvent(e);hover=true;positionActions(); }
    void leaveEvent(QEvent* e) override { QWidget::leaveEvent(e);hover=false;positionActions(); }
    void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e);positionActions(); }
private:
    void positionActions() {
        if((hover || focused) && createActions){
            auto create=std::exchange(createActions,{});
            create();
        }
        if(!bar) return;
        bar->adjustSize();
        bar->move(qMax(0,width()-bar->width()-8),5);
        bar->setVisible(hover || focused);
        bar->raise();
    }
    QWidget* bar=nullptr;
    QHBoxLayout* buttons=nullptr;
    bool hover=false,focused=false;
};
