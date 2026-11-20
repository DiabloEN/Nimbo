#pragma once
#include "interface_text.h"
#include "screen_ink.h"
#include "ui.h"

class InkToolbar final : public QWidget {
public:
    explicit InkToolbar(QWidget* parent):QWidget(parent){
        setObjectName("inkToolbar");
        setStyleSheet("QWidget#inkToolbar,QWidget#inkPalette{background:transparent;border:0}");
        setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        auto* row=new QHBoxLayout(this);
        row->setAlignment(Qt::AlignRight|Qt::AlignVCenter);row->setContentsMargins(0,0,0,0);row->setSpacing(5);
        body=new QWidget(this);body->setObjectName("inkPalette");
        auto* palette=new QHBoxLayout(body);
        palette->setContentsMargins(3,0,3,0);palette->setSpacing(4);
        auto* group=new QButtonGroup(body);
        for(const auto& color:ScreenInk::colors){
            auto* swatch=new QPushButton(body);swatch->setCheckable(true);swatch->setFixedSize(20,20);
            swatch->setAccessibleName(color);swatch->setCursor(Qt::PointingHandCursor);
            swatch->setStyleSheet("QPushButton{background:"+color+";color:#181818;border:0;outline:0;border-radius:10px;padding:0}");
            group->addButton(swatch);swatches.insert(color,swatch);palette->addWidget(swatch);
            connect(swatch,&QPushButton::toggled,this,[this,swatch,color](bool checked){
                swatch->setText(checked?QStringLiteral("✓"):QString());
                if(checked && onColor)onColor(color);
            });
        }
        selectColor(ScreenInk::colors.first());
        erase=Ui::action("eraser",UiText::InkClearMine(),30);
        palette->addWidget(erase);
        row->addWidget(body);body->hide();
        pencil=Ui::action("pencil",UiText::InkPen(),30);pencil->setCheckable(true);
        row->addWidget(pencil);
        connect(erase,&QPushButton::clicked,this,[this]{if(onClear)onClear();});
        connect(pencil,&QPushButton::toggled,this,[this](bool expanded){
            body->setVisible(expanded);if(onExpand)onExpand(expanded);
        });
    }
    std::function<void(bool)> onExpand;
    std::function<void(QString)> onColor;
    std::function<void()> onClear;
    void setExpanded(bool on){pencil->setChecked(on && pencil->isEnabled());}
    bool expanded() const{return pencil->isChecked();}
    void selectColor(const QString& color){if(auto* swatch=swatches.value(color))swatch->setChecked(true);}
    void configure(bool host,bool connected,bool allowed,bool transport){
        const bool writable=connected && (host || allowed);
        erase->setEnabled(writable);
        pencil->setToolTip(writable?UiText::InkPen():!transport || !connected?UiText::InkUnavailable():UiText::InkNeedsPermission());
        const auto label=host?UiText::InkClearAll():UiText::InkClearMine();
        erase->setToolTip(label);erase->setAccessibleName(label);
    }
private:
    QWidget* body;
    QPushButton *pencil,*erase;
    QHash<QString,QPushButton*> swatches;
};
