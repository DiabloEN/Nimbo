#pragma once
#include "interface_text.h"
#include "api.h"
#include "youtube_surface.h"
#include <QtWidgets>

class LinkCard final : public QPushButton {
public:
    static QPair<QString,QUrl> firstLink(const QString& text){
        static const QRegularExpression pattern(R"rx((?<![\p{L}\p{N}_/@])(?:https://|www\.)[^\s<>"\x00-\x1f]+)rx",QRegularExpression::CaseInsensitiveOption);
        auto matches=pattern.globalMatch(text);
        while(matches.hasNext()){
            auto source=matches.next().captured();
            while(!source.isEmpty() && QString(".,!?;:…").contains(source.back()))source.chop(1);
            for(auto pair:{QString("()"),QString("[]"),QString("{}")})
                while(source.endsWith(pair.back()) && source.count(pair.back())>source.count(pair.front()))source.chop(1);
            QUrl url(source.startsWith("www.",Qt::CaseInsensitive)?"https://"+source:source);
            if(url.isValid()&&url.scheme()=="https"&&!url.host().isEmpty()&&url.userInfo().isEmpty())return {source,url};
        }
        return {};
    }
    static QString videoId(const QUrl& url){
        auto parts=url.path().split('/',Qt::SkipEmptyParts);
        QString id;
        if(url.host()=="youtu.be")id=parts.value(0);
        else if(QStringList{"youtube.com","www.youtube.com","m.youtube.com"}.contains(url.host())){
            if(url.path()=="/watch")id=QUrlQuery(url).queryItemValue("v");
            else if(QStringList{"embed","live","shorts"}.contains(parts.value(0)))id=parts.value(1);
        }
        static const QRegularExpression valid("^[a-zA-Z0-9_-]{11}$");
        return valid.match(id).hasMatch()?id:QString();
    }
    LinkCard(ServerApi& api,QString channel,QString message,const QPair<QString,QUrl>& link,std::function<void()> relayout):
        url(link.second),id(videoId(url)){
        setObjectName("linkPreview");setMaximumWidth(420);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        setFlat(true);setCursor(Qt::PointingHandCursor);setAccessibleName(id.isEmpty()?url.host():UiText::PlayYoutubeVideo());
        setFixedHeight(id.isEmpty()?1:236);
        QUrlQuery query;query.addQueryItem("url",link.first);query.addQueryItem("channel",channel);query.addQueryItem("message",message);
        const auto path="/api/link-preview?"+query.toString(QUrl::FullyEncoded);
        auto display=[self=QPointer<LinkCard>(this),relayout](QJsonObject result,bool resized){
                if(!self)return;
                self->title=result["title"].toString();self->description=result["description"].toString();self->price=result["price"].toString();
                const QString image=result["image"].toString();
                if(image.size()<260000 && (image.startsWith("data:image/jpeg;base64,")||image.startsWith("data:image/png;base64,"))){
                    QByteArray bytes=QByteArray::fromBase64(image.section(',',1).toLatin1());
                    QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);
                    QImageReader reader(&buffer);const auto size=reader.size();
                    if(size.isValid() && size.width()<=4096 && size.height()<=4096){
                        reader.setScaledSize(size.scaled(self->id.isEmpty()?QSize(82,96):QSize(420,420),Qt::KeepAspectRatio));
                        self->picture=QPixmap::fromImage(reader.read());
                    }
                }
                if(self->id.isEmpty()){
                    if(self->title.isEmpty()){self->unavailable=true;self->hide();}else self->setFixedHeight(96);
                    if(resized)relayout();
                }
                self->setAccessibleName(self->id.isEmpty()?self->title:UiText::PlayVideo()+self->title);
                self->update();
        };
        const auto cached=api.cachedPreview(path);
        if(!cached.isEmpty())display(cached,false);
        else load=[this,&api,path,display]{
            api.request("GET",path,{},[guard=QPointer<LinkCard>(this),&api,path,display](QJsonObject result,QString error){
                if(!guard)return;
                if(error.isEmpty())api.rememberPreview(path,result);
                display(result,true);
            });
        };
        connect(this,&QPushButton::clicked,this,[this]{
            if(id.isEmpty()||failed){QDesktopServices::openUrl(url);return;}
            if(player)return;
            player=new YoutubeSurface(this,id,position);
            player->started=[this]{update();};
            player->finished=[this](int time,QString error,QImage still){
                if(!still.isNull())pausedPicture=QPixmap::fromImage(still);
                else if(time==0 && error.isEmpty())pausedPicture={};
                position=time;failed=!error.isEmpty();
                if(failed)setAccessibleName(error);
                stop();update();
            };
            player->setGeometry(rect());
            update();
        });
    }
    void stop(){if(player){delete player;player=nullptr;}}
    void detach(QWidget* parent){moving=true;setParent(parent);}
    void attach(QVBoxLayout* layout){layout->addWidget(this);moving=false;setVisible(!unavailable);if(player)player->trackCard();}
protected:
    void hideEvent(QHideEvent* e) override{if(!moving)stop();QPushButton::hideEvent(e);}
    void resizeEvent(QResizeEvent* e)override{
        QPushButton::resizeEvent(e);
        if(!id.isEmpty())setFixedHeight(qMax(1,qRound(width()*9./16)));
        if(player)player->setGeometry(rect());
    }
    void paintEvent(QPaintEvent*)override{
        if(load){auto request=std::move(load);load={};QTimer::singleShot(0,this,std::move(request));}
        if(id.isEmpty() && title.isEmpty())return;
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
        QPainterPath clip;clip.addRoundedRect(rect(),13,13);p.setClipPath(clip);
        p.fillRect(rect(),QColor("#1d1d1d"));p.setPen(Qt::white);
        if(!id.isEmpty()){
            if(!pausedPicture.isNull()){
                p.drawPixmap(rect(),pausedPicture);
            }else if(!picture.isNull()){
                QSize crop=size().scaled(picture.size(),Qt::KeepAspectRatio);
                QRect source(QPoint(),crop);source.moveCenter(picture.rect().center());
                p.drawPixmap(rect(),picture,source);
            }
            if(pausedPicture.isNull()){
                QFont heading=font();heading.setWeight(QFont::DemiBold);p.setFont(heading);
                QLinearGradient shade(0,0,0,34+p.fontMetrics().height());shade.setColorAt(0,QColor(0,0,0,209));shade.setColorAt(1,Qt::transparent);p.fillRect(rect(),shade);
                p.drawText(rect().adjusted(14,12,-14,-12),Qt::AlignTop,p.fontMetrics().elidedText(title.isEmpty()?UiText::YoutubeVideo():title,Qt::ElideRight,qMax(0,width()-28)));
            }
            QRect play(0,0,54,39);play.moveCenter(rect().center());
            p.setBrush(underMouse()||hasFocus()?QColor("#111"):QColor(0,0,0,194));p.setPen(Qt::NoPen);p.drawRoundedRect(play,12,12);
            if(player && player->isHidden()){
                p.setPen(Qt::white);p.drawText(play,Qt::AlignCenter,"…");
            }else{
                QPolygonF triangle;const QPointF c=play.center();triangle<<c+QPointF(-5,-8)<<c+QPointF(8,0)<<c+QPointF(-5,8);
                p.setBrush(Qt::white);p.drawPolygon(triangle);
            }
            if(hasFocus()){p.setPen(QPen(Qt::white,2));p.setBrush(Qt::NoBrush);p.drawRoundedRect(rect().adjusted(2,2,-2,-2),11,11);}
            p.setPen(Qt::white);
            if(failed)p.drawText(rect().adjusted(8,8,-8,-8),Qt::AlignBottom|Qt::AlignHCenter,UiText::OpenInBrowser());
        }else{
            int x=12;
            if(!picture.isNull()){p.drawPixmap(12,(height()-picture.height())/2,picture);x+=picture.width()+12;}
            const int w=qMax(0,width()-x-12);
            p.setPen(QColor("#aaa"));p.drawText(x,21,url.host());p.setPen(Qt::white);
            p.drawText(x,42,fontMetrics().elidedText(title,Qt::ElideRight,w));p.drawText(x,61,price);
            p.setPen(QColor("#bbb"));p.drawText(x,80,fontMetrics().elidedText(description,Qt::ElideRight,w));
        }
    }
private:
    QUrl url;QString id,title,description,price;QPixmap picture,pausedPicture;YoutubeSurface* player=nullptr;
    std::function<void()> load;int position=0;bool failed=false,moving=false,unavailable=false;
};
