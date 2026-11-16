#pragma once
#include "interface_text.h"
#include "ui.h"

class EffectCard final : public QPushButton {
public:
    EffectCard(const QPixmap& photo,bool blurred,int strength){
        setObjectName("effectCard");
        setAccessibleName(blurred?UiText::BlurredBackground():UiText::NoEffects());
        setToolTip(accessibleName());
        setCheckable(true);setAutoExclusive(true);setCursor(Qt::PointingHandCursor);
        setMinimumWidth(0);setMaximumWidth(220);
        QSizePolicy policy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        policy.setHeightForWidth(true);setSizePolicy(policy);
        if(!photo.isNull())half=photo.copy(blurred?photo.width()/2:0,0,photo.width()/2,photo.height());
        if(!blurred)return;
        strengthBox=new QWidget(this);
        strengthBox->setObjectName("strengthBox");
        strengthBox->setStyleSheet("QWidget#strengthBox{background:rgba(20,20,20,232);border-radius:9px}");
        auto* line=new QHBoxLayout(strengthBox);
        line->setContentsMargins(9,7,9,7);line->setSpacing(8);
        slider=new QSlider(Qt::Horizontal);
        slider->setAccessibleName(UiText::BlurStrength());slider->setRange(1,60);slider->setValue(strength);
        slider->setStyleSheet("QSlider{background:transparent} QSlider::groove:horizontal{height:4px;background:#555;border-radius:2px} QSlider::sub-page:horizontal{background:#66aaf0} QSlider::handle:horizontal{background:#66aaf0;width:14px;margin:-5px 0;border-radius:7px}");
        line->addWidget(slider,1);
        auto* value=Ui::text(QString::number(strength),"font-size:10px;color:white");
        value->setMinimumWidth(14);value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);line->addWidget(value);
        connect(slider,&QSlider::valueChanged,value,qOverload<int>(&QLabel::setNum));
        connect(this,&QPushButton::toggled,strengthBox,&QWidget::setVisible);
        strengthBox->hide();
    }
    QSize sizeHint() const override{return {220,266};}
    QSize minimumSizeHint() const override{return {0,46};}
    int heightForWidth(int w) const override{return qMin(220,w)+46;}
    QSlider* slider=nullptr;
protected:
    void resizeEvent(QResizeEvent* e) override{
        QPushButton::resizeEvent(e);
        if(strengthBox)strengthBox->setGeometry(10,width()-42,qMax(0,width()-20),32);
    }
    void paintEvent(QPaintEvent*) override{
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
        const QRectF box(0,0,width(),heightForWidth(width()));
        QPainterPath clip;clip.addRoundedRect(box,14,14);p.setClipPath(clip);
        p.fillRect(box,isChecked()?QColor("#29302e"):QColor("#1a1a1a"));
        p.drawPixmap(QRect(0,0,width(),width()),half);
        QFont caption=font();caption.setPixelSize(12);caption.setWeight(QFont::Medium);p.setFont(caption);
        p.setPen(QColor("#ddd"));p.drawText(QRect(12,width(),width()-44,46),Qt::AlignLeft|Qt::AlignVCenter,accessibleName());
        if(isChecked())Ui::icon("check").paint(&p,QRect(width()-29,width()+14,17,17));
        if(hasFocus()&&testAttribute(Qt::WA_KeyboardFocusChange)){
            p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#bbb"),2));p.drawRoundedRect(box.adjusted(1,1,-1,-1),13,13);
        }
    }
private:
    QPixmap half;
    QWidget* strengthBox=nullptr;
};
