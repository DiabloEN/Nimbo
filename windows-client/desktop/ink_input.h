#pragma once
#include "screen_ink.h"
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <functional>

class InkInput : public QWidget {
public:
    explicit InkInput(QWidget* parent=nullptr):QWidget(parent){
        flush.setSingleShot(true);flush.setInterval(90);
        connect(&flush,&QTimer::timeout,this,[this]{publish(false);});
    }
    QString inkColor=ScreenInk::colors.first();
    bool drawing=false;
    std::function<void(QJsonObject)> submit;
    void finishStroke(){if(!points.isEmpty())publish(true);reset();}
    void cancelStroke(){if(revision)publish(true);reset();}
protected:
    virtual QRect inkArea()const{return rect();}
    void paintDraft(QPainter& painter)const{
        if(points.isEmpty())return;
        const auto path=ScreenInk::smoothPath(points);
        ScreenInk::paint(painter,inkArea(),{{{}, {},QColor(inkColor),path}});
    }
    void mousePressEvent(QMouseEvent* e)override{
        if(!drawing || e->button()!=Qt::LeftButton || !inkArea().contains(e->position().toPoint()))return;
        cancelStroke();strokeId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        append(e->position());e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e)override{
        if(!points.isEmpty() && e->buttons().testFlag(Qt::LeftButton))append(e->position());
    }
    void mouseReleaseEvent(QMouseEvent* e)override{
        if(e->button()!=Qt::LeftButton || points.isEmpty())return;
        append(e->position());
        publish(true);reset();
    }
private:
    QTimer flush;
    QString strokeId;
    int revision=0;
    void reset(){
        flush.stop();revision=0;strokeId.clear();points.clear();points.squeeze();update();
    }
    void publish(bool complete){
        if(points.isEmpty() || !submit)return;
        QJsonArray pairs;
        for(auto p:points)pairs.append(QJsonArray{p.x(),p.y()});
        if(points.size()==1){
            const auto p=points.first();pairs.append(QJsonArray{p.x()+(p.x()<.5?.0002:-.0002),p.y()});
        }
        submit({{"type","stroke"},{"id",strokeId},{"revision",++revision},{"complete",complete},
                {"color",inkColor},{"points",pairs}});
    }
    QList<QPointF> points;
    void append(QPointF pos){
        const auto area=inkArea();if(area.isEmpty())return;
        auto coordinate=[](double x){return qRound(qBound(0.,x,1.)*10000)/10000.;};
        QPointF next(coordinate((pos.x()-area.x())/area.width()),coordinate((pos.y()-area.y())/area.height()));
        if(!points.isEmpty() && points.last()==next)return;
        if(points.size()>=350){
            for(int i=1;i<175;++i)points[i]=points[2*i];
            points.resize(175);
        }
        points.append(next);if(!flush.isActive())flush.start();update();
    }
};
