#pragma once
#include "interface_text.h"
#include "server_links.h"
#include "ui.h"

class MemberServerLink final : public QWidget {
public:
    MemberServerLink() {
        setObjectName("memberServerLink");
        setStyleSheet("QWidget#memberServerLink{background:transparent}");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);layout->setSpacing(6);
        button=new QPushButton(UiText::ServerLink());button->setObjectName("copyServerLink");
        button->setToolTip(UiText::CopyServerLink());button->setCursor(Qt::PointingHandCursor);
        for(auto key:{Qt::Key_Return,Qt::Key_Enter}){
            auto* shortcut=new QShortcut(QKeySequence(key),button);
            shortcut->setContext(Qt::WidgetShortcut);
            connect(shortcut,&QShortcut::activated,button,&QPushButton::click);
        }
        button->setFixedHeight(36);
        button->setStyleSheet("QPushButton{background:#202020;color:transparent;border:0;border-radius:11px;padding:0} QPushButton:hover,QPushButton:focus{background:#303030}");
        auto* content=new QHBoxLayout(button);content->setContentsMargins(0,0,0,0);content->setSpacing(5);
        auto* icon=new QLabel;icon->setAttribute(Qt::WA_TransparentForMouseEvents);icon->setStyleSheet("background:transparent");
        icon->setPixmap(Ui::tintedIcon("copy",QColor("#ededed")).pixmap(13,13));icon->setFixedSize(13,13);
        label=new QLabel;label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setStyleSheet("background:transparent;color:#ededed;font-size:10.5px");
        auto font=label->font();font.setPointSizeF(10.5*72/button->logicalDpiY());font.setStretch(96);label->setFont(font);
        content->addStretch();content->addWidget(icon);content->addWidget(label);content->addStretch();
        refreshLanguage();
        layout->addWidget(button);
        field=new QLineEdit;field->setAccessibleName(UiText::ManualServerLink());field->setReadOnly(true);
        layout->addWidget(field);field->hide();timer.setSingleShot(true);
        connect(&timer,&QTimer::timeout,this,[this]{refreshLanguage();});
        connect(button,&QPushButton::clicked,this,[this]{
            if(server.isEmpty())return;
            const auto url=ServerLinks::returnLink(server);
            auto* clipboard=QApplication::clipboard();clipboard->setText(url);
            if(clipboard->text()==url){
                field->hide();timer.start(2000);refreshLanguage();
            }else{
                reset();field->setText(url);field->show();field->setFocus();field->selectAll();
            }
        });
    }
    void refreshLanguage(){
        const auto text=timer.isActive()?UiText::Copied():UiText::ServerLink();
        button->setText(text);label->setText(text);
    }
    void setServer(const QUrl& origin){if(server!=origin){server=origin;reset();}}
protected:
    void hideEvent(QHideEvent* event) override{reset();QWidget::hideEvent(event);}
private:
    void reset(){timer.stop();refreshLanguage();field->clear();field->hide();}
    QPushButton* button;
    QLabel* label;
    QLineEdit* field;
    QUrl server;
    QTimer timer;
};
