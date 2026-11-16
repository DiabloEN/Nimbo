#pragma once
#include "interface_text.h"
#include "ui.h"

class AvatarUpload final : public QPushButton {
public:
    AvatarUpload(){
        setFixedSize(80,80);setIconSize({80,80});setCursor(Qt::PointingHandCursor);setProperty("theme","avatar");
        animation.setDuration(120);connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant& v){opacity=v.toReal();update();});
    }
    void updateImage(const QString& fallback,const QString& data){
        populated=!data.isEmpty();setIcon(QIcon(Ui::avatar(fallback,Ui::imageData(data),80)));setAccessibleName(populated?UiText::EditAvatar():UiText::UploadAvatar());
    }
protected:
    void enterEvent(QEnterEvent* event)override{transition(1);QPushButton::enterEvent(event);}
    void leaveEvent(QEvent* event)override{transition(hasFocus()?1:0);QPushButton::leaveEvent(event);}
    void focusInEvent(QFocusEvent* event)override{transition(1);QPushButton::focusInEvent(event);}
    void focusOutEvent(QFocusEvent* event)override{transition(underMouse()?1:0);QPushButton::focusOutEvent(event);}
    void paintEvent(QPaintEvent* event)override{
        QPushButton::paintEvent(event);if(opacity<=0)return;QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setOpacity(opacity);
        p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,153));p.drawEllipse(rect());Ui::icon("camera").paint(&p,QRect(29,20,22,22));
        auto f=font();f.setPixelSize(10);p.setFont(f);p.setPen(Qt::white);p.drawText(QRect(0,45,80,16),Qt::AlignCenter,populated?UiText::Edit():UiText::Upload());
    }
private:
    void transition(qreal to){animation.stop();animation.setStartValue(opacity);animation.setEndValue(to);animation.start();}
    bool populated=false;qreal opacity=0;QVariantAnimation animation;
};

class AvatarCrop final : public QWidget {
public:
    explicit AvatarCrop(QImage image,bool square):source(std::move(image)),square(square){
        setFixedSize(280,280);setFocusPolicy(Qt::StrongFocus);setCursor(Qt::OpenHandCursor);setAccessibleName(UiText::CropAreaUseTheArrowKeysOr());
    }
    void setZoom(int percent){zoom=percent/100.;clamp();update();}
    QImage result()const{
        QImage output(192,192,QImage::Format_RGB32);output.fill(Qt::black);QPainter painter(&output);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(output.rect(),source,sourceRect());return output;
    }
protected:
    void paintEvent(QPaintEvent*) override{
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);QPainterPath clip;
        if(square)clip.addRoundedRect(rect(),16,16);else clip.addEllipse(rect());p.setClipPath(clip);p.drawImage(rect(),source,sourceRect());
    }
    void mousePressEvent(QMouseEvent* e) override{if(e->button()==Qt::LeftButton){last=e->position();drag=true;setFocus();setCursor(Qt::ClosedHandCursor);}}
    void mouseReleaseEvent(QMouseEvent*) override{drag=false;setCursor(Qt::OpenHandCursor);}
    void mouseMoveEvent(QMouseEvent* e) override{if(drag){offset+=e->position()-last;last=e->position();clamp();update();}}
    void keyPressEvent(QKeyEvent* e) override{
        const int step=e->modifiers().testFlag(Qt::ShiftModifier)?10:2;
        switch(e->key()){case Qt::Key_Left:offset.rx()-=step;break;case Qt::Key_Right:offset.rx()+=step;break;case Qt::Key_Up:offset.ry()-=step;break;case Qt::Key_Down:offset.ry()+=step;break;default:QWidget::keyPressEvent(e);return;}clamp();update();
    }
private:
    double scale()const{return 280./qMin(source.width(),source.height())*zoom;}
    QRectF sourceRect()const{double factor=scale(),dim=280/factor;return {(source.width()-dim)/2-offset.x()/factor,(source.height()-dim)/2-offset.y()/factor,dim,dim};}
    void clamp(){double x=qMax(0.,(source.width()*scale()-280)/2),y=qMax(0.,(source.height()*scale()-280)/2);offset.setX(qBound(-x,offset.x(),x));offset.setY(qBound(-y,offset.y(),y));}
    QImage source;bool square,drag=false;double zoom=1;QPointF offset,last;
};

