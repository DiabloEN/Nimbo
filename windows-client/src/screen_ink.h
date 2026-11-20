#pragma once
#include <algorithm>
#include <cmath>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>
#include <QVarLengthArray>

namespace ScreenInk {
inline constexpr char topic[]="nimbo-screen-ink-v1";
inline const QStringList colors={"#f5c542","#ef7070","#69b5ff","#78dba9","#b99aff","#eeeeee"};
struct Stroke {QString author,id;QColor color;QPainterPath path;int revision=1;bool complete=true;};
using Strokes=QList<Stroke>;
inline QPainterPath smoothPath(const QList<QPointF>& points){
    QPainterPath path;
    if(points.isEmpty())return path;
    path.moveTo(points.first());
    if(points.size()<3){path.lineTo(points.last());return path;}
    // Mouse events cluster around slow, small movements. Integrate over the
    // drawn distance so a cluster of tiny steps cannot dominate a bend.
    auto pixels=[](const QPointF& delta){return std::hypot(delta.x()*1920.,delta.y()*1080.);};
    QVarLengthArray<qreal,350> lengths(points.size()-1);
    for(qsizetype i=0;i<lengths.size();++i)lengths[i]=pixels(points[i+1]-points[i]);
    constexpr qreal radius=6.;
    auto meanOffset=[&](qsizetype center,int direction){
        QPointF sum;
        qreal travelled=0.;
        for(qsizetype i=center;i+direction>=0 && i+direction<points.size();i+=direction){
            const qsizetype next=i+direction;
            const qreal length=lengths[qMin(i,next)];
            if(length<=0.)continue;
            if(i==center && length>=radius)return (points[next]-points[i])*.5;
            const qreal take=qMin(length,radius-travelled);
            // Average on this linear segment is its midpoint, weighted by length.
            sum+=take*(points[i]-points[center]+(points[next]-points[i])*(.5*take/length));
            travelled+=take;
            if(take<length || travelled>=radius)break;
        }
        return travelled>0.?sum/travelled:QPointF{};
    };
    auto control=[&](qsizetype i){
        auto offset=(meanOffset(i,-1)+meanOffset(i,1))*.5;
        const double distance=pixels(offset);
        // Fine lettering and arrowheads need a tighter displacement limit.
        const qreal limit=qMin(lengths[i-1],lengths[i])<radius ? 1.6 : 2.;
        if(distance>limit)offset*=limit/distance;
        return points[i]+offset;
    };
    auto current=control(1);
    for(qsizetype i=2;i+1<points.size();++i){
        const auto next=control(i);
        path.quadTo(current,(current+next)*.5);
        current=next;
    }
    path.quadTo(current,points.last());
    return path;
}
inline void paint(QPainter& p,const QRectF& box,const Strokes& strokes){
    p.save();p.setClipRect(box,Qt::IntersectClip);p.setRenderHint(QPainter::Antialiasing);
    p.translate(box.topLeft());p.scale(box.width(),box.height());p.setBrush(Qt::NoBrush);
    for(const auto& s:strokes){
        QPen pen(s.color,qMax(2.,box.height()/300.),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
        pen.setCosmetic(true);p.setPen(pen);p.drawPath(s.path);
    }
    p.restore();
}
// Only the presenter owns the board. The author always comes from the call transport.
struct Board {
    QString session;
    Strokes strokes;
    bool apply(const QString& author,bool presenter,const QJsonObject& command){
        if(author.isEmpty() || session.isEmpty() || command["session"]!=session)return false;
        const auto type=command["type"].toString();
        if(type=="undo"){
            for(qsizetype i=strokes.size();i>0;--i){
                if(strokes[i-1].author!=author)continue;
                strokes.removeAt(i-1);
                if(strokes.isEmpty())strokes.squeeze();
                return true;
            }
            return false;
        }
        if(type=="clear"){
            const auto removed=strokes.removeIf([&](const Stroke& s){return presenter || s.author==author;});
            if(strokes.isEmpty())strokes.squeeze();
            return removed>0;
        }
        if(type!="stroke")return false;
        const auto id=command["id"].toString(),color=command["color"].toString();
        const auto points=command["points"].toArray();
        if(id.isEmpty() || id.size()>80 || !colors.contains(color) || points.size()<2 || points.size()>350)return false;
        const int revision=command["revision"].toInt(1);
        const bool complete=command["complete"].toBool(true);
        if(revision<1)return false;
        auto old=std::find_if(strokes.begin(),strokes.end(),[&](const Stroke& s){return s.author==author && s.id==id;});
        if(old==strokes.end()){
            if(revision!=1 || strokes.size()>=120)return false;
        }else if(old->complete || revision<=old->revision)return false;
        QList<QPointF> vertices;vertices.reserve(points.size());
        for(qsizetype i=0;i<points.size();++i){
            const auto pair=points[i].toArray();if(pair.size()!=2)return false;
            double x=pair[0].toDouble(-1),y=pair[1].toDouble(-1);
            if(!(x>=0 && x<=1 && y>=0 && y<=1))return false;
            vertices.append(QPointF(x,y));
        }
        Stroke stroke{author,id,QColor(color),smoothPath(vertices),revision,complete};
        if(old==strokes.end())strokes.append(std::move(stroke));else *old=std::move(stroke);
        return true;
    }
};
}
Q_DECLARE_METATYPE(ScreenInk::Strokes)
