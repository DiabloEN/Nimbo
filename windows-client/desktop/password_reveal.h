#pragma once
#include "interface_text.h"
#include "ui.h"

class PasswordReveal final:public QToolButton{
public:
    explicit PasswordReveal(QLineEdit* input):QToolButton(input),input(input){
        setFixedSize(34,32);setIcon(Ui::icon("password-eye"));setIconSize({18,18});
        setAccessibleName(UiText::HoldToShowPassword());setToolTip(accessibleName());setCursor(Qt::PointingHandCursor);
        setStyleSheet("QToolButton{background:transparent;border:0;padding:0} QToolButton:focus{background:#333;border-radius:8px}");
        input->setTextMargins(0,0,34,0);input->installEventFilter(this);move(input->width()-38,(input->height()-height())/2);
        connect(this,&QToolButton::pressed,this,[this]{this->input->setEchoMode(QLineEdit::Normal);});
        connect(this,&QToolButton::released,this,[this]{hideValue();});
        connect(qApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){if(state!=Qt::ApplicationActive)hideValue();});
    }
protected:
    bool eventFilter(QObject* o,QEvent* e)override{if(o==input&&e->type()==QEvent::Resize)move(input->width()-38,(input->height()-height())/2);return QToolButton::eventFilter(o,e);}
    void leaveEvent(QEvent* e)override{hideValue();QToolButton::leaveEvent(e);}
    void focusOutEvent(QFocusEvent* e)override{hideValue();QToolButton::focusOutEvent(e);}
    void hideEvent(QHideEvent* e)override{hideValue();QToolButton::hideEvent(e);}
private:
    QLineEdit* input;
    void hideValue(){input->setEchoMode(QLineEdit::Password);}
};

