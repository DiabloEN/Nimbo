#include "interface_text.h"
#include "window.h"
#include "ui.h"
#include "audio_attachment.h"
#include "video_attachment.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>

namespace {
class AttachmentPage final : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void()> dismiss;
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape){dismiss();event->accept();return;}
        QWidget::keyPressEvent(event);
    }
};
class GalleryImage final : public QLabel {
protected:
    void paintEvent(QPaintEvent* event) override {
        const auto image=pixmap();
        if(image.isNull()){QLabel::paintEvent(event);return;}
        QRect target(QPoint(),image.size().scaled(size(),Qt::KeepAspectRatio));
        target.moveCenter(rect().center());
        QPainter painter(this);painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(target,image);
    }
};
class PreviewButton final : public QPushButton {
public:
    std::function<void()> loadOnPaint;QPixmap preview;QPointer<QPushButton> tool;
    void positionTool(){if(tool)tool->move(width()-tool->width()-8,8);}
    void resizeEvent(QResizeEvent* e) override{QPushButton::resizeEvent(e);positionTool();}
    void enterEvent(QEnterEvent* e) override{QPushButton::enterEvent(e);if(tool)tool->show();}
    void leaveEvent(QEvent* e) override{QPushButton::leaveEvent(e);if(tool&&!hasFocus()&&!tool->hasFocus())tool->hide();}
    void focusInEvent(QFocusEvent* e) override{QPushButton::focusInEvent(e);if(tool)tool->show();}
    void focusOutEvent(QFocusEvent* e) override{QPushButton::focusOutEvent(e);if(tool&&!underMouse()&&!tool->hasFocus())tool->hide();}
protected:
    void paintEvent(QPaintEvent* event)override{
        if(preview.isNull())QPushButton::paintEvent(event);
        else{QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);QPainterPath clip;clip.addRoundedRect(rect(),12,12);p.setClipPath(clip);p.drawPixmap(rect(),preview);
             if(hasFocus()){p.setPen(QPen(QColor("#8fc4f5"),2));p.setBrush(Qt::NoBrush);p.drawRoundedRect(rect().adjusted(1,1,-1,-1),11,11);}}

        if(loadOnPaint){auto load=std::move(loadOnPaint);loadOnPaint={};QTimer::singleShot(0,this,std::move(load));}
    }
};
bool supported(QString ext){return QStringList{"png","jpg","jpeg","gif","webp","mp3","wav","ogg","oga","flac","m4a","mp4","webm","ogv"}.contains(ext);}
}
void DesktopWindow::configureMediaSync(){
    api.visibleMedia=[this]{
        QStringList ids;
        for(const auto& item:lastMessages){
            const auto row=item.toObject();auto files=row["attachments"].toArray();
            if(row["attachment"].isObject())files.append(row["attachment"]);
            for(const auto& file:files)ids.append(file.toObject()["id"].toString());
        }
        return ids;
    };
    api.mediaInvalidated=[this](const QStringList& ids){
        for(const auto& id:ids){
            const auto key=api.server.toString()+"/"+selfId+"/api/files/"+id;
            thumbnails.remove(key);imageFiles.remove(key);imageRequests.remove(key);
            if(attachmentViewer && attachmentViewer->property("fileIds").toStringList().contains(id))
                static_cast<AttachmentPage*>(attachmentViewer.data())->dismiss();
        }
        ++messageRevision;loadMessages();
    };
}
void DesktopWindow::loadImage(const QString& route,ImageReady ready){
    const auto key=api.server.toString()+"/"+selfId+route;
    if(auto* existing=imageFiles.object(key)){
        auto file=*existing;
        if(QFileInfo::exists(file->fileName())){if(ready)ready(file->fileName());return;}
        imageFiles.remove(key);
    }
    const bool loading=imageRequests.contains(key);
    auto& waiters=imageRequests[key];if(ready)waiters.append(std::move(ready));
    if(loading)return;
    std::shared_ptr<CachedImageFile> file;
    {
        QTemporaryFile temp;
        if(!temp.open()){
            const auto pending=imageRequests.take(key);
            for(const auto& done:pending)done({});
            return;
        }
        file=std::make_shared<CachedImageFile>(temp.fileName());
        temp.setAutoRemove(false);
    }
    api.download(route,file->fileName(),[this,key,file,gen=channelGeneration](QString error){
        if(!imageRequests.contains(key))return;
        auto pending=imageRequests.take(key);
        const auto path=error.isEmpty()?file->fileName():QString();
        if(!path.isEmpty() && gen==channelGeneration)
            imageFiles.insert(key,new std::shared_ptr<CachedImageFile>(file),qMax<qint64>(64,(QFileInfo(path).size()+1023)/1024));
        for(auto& done:pending)done(path);
    },20LL*1048576);
}

void DesktopWindow::downloadVideo(const QString& route,const QString& name){
    QString leaf=QFileInfo(QDir::fromNativeSeparators(name)).fileName();
    leaf.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])"),"_");
    leaf.remove(QRegularExpression("[. ]+$"));
    if(leaf.isEmpty())leaf="video";
    static const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$))",QRegularExpression::CaseInsensitiveOption);
    if(leaf.contains(reserved))leaf.prepend('_');
    QDir dir(downloadDirectory);
    if(downloadDirectory.isEmpty() || !dir.mkpath(".")){note(UiText::CouldNotOpenTheDownloadsFolder());return;}
    auto temp=std::make_shared<QTemporaryDir>(dir.filePath(".nimbo-XXXXXX"));
    if(!temp->isValid()){note(UiText::CouldNotCreateAFileInDownloads());return;}
    const QString source=temp->filePath("video");
    api.download(route,source,[this,temp,dir,leaf,source](QString error){
        if(!error.isEmpty()){note(error);return;}
        const QFileInfo file(leaf);
        const QString suffix=file.suffix().isEmpty()?QString():"."+file.suffix();
        for(int number=0;number<10000;++number){
            const QString candidate=number?file.completeBaseName()+QString(" (%1)").arg(number)+suffix:leaf;
            const QString destination=dir.filePath(candidate);
            if(QFile::exists(destination))continue;
            if(QFile::rename(source,destination)){note(UiText::SavedToDownloads()+candidate);return;}
            if(!QFile::exists(destination))break;
        }
        note(UiText::CouldNotSaveTheVideoToDownloads());
    });
}

void DesktopWindow::attachmentActions(QVBoxLayout* list,const QJsonObject& file,const QJsonArray& gallery){
    const bool compact=gallery.size()>1;
    const auto name=file.value("name").toString(),ext=QFileInfo(name).suffix().toLower(),route="/api/files/"+QString::fromLatin1(QUrl::toPercentEncoding(file.value("id").toString()));
    auto* line=new QHBoxLayout;list->addLayout(line);
    auto save=[this,name,route]{auto dest=QFileDialog::getSaveFileName(this,UiText::DownloadAttachment(),QFileInfo(name).fileName());if(!dest.isEmpty())api.download(route,dest,[this](QString e){note(e.isEmpty()?UiText::FileSaved():e);});};
    if(QStringList{"mp3","wav","ogg","oga","flac","m4a"}.contains(ext)){
        const QString key=api.server.toString()+"/"+selfId+route;
        AudioAttachment* audio=nullptr;
        for(auto* existing:messages->findChildren<QWidget*>("audioAttachment",Qt::FindDirectChildrenOnly)){
            auto* candidate=static_cast<AudioAttachment*>(existing);
            if(candidate->key==key){audio=candidate;break;}
        }
        if(!audio){audio=new AudioAttachment(api,name,route,save);audio->key=key;audio->largeOriginal=file["size"].toInteger()>20LL*1048576;}
        line->setAlignment(Qt::AlignLeft|Qt::AlignTop);line->addWidget(audio);audio->show();return;
    }
    int index=0;while(index<gallery.size()&&gallery[index].toObject()["id"]!=file["id"])++index;
    auto enlarge=[this,gallery,index]{viewAttachments(gallery,index);};
    if(QStringList{"mp4","webm","ogv"}.contains(ext)){
        const QString key=api.server.toString()+"/"+selfId+route;
        VideoAttachment* video=nullptr;
        for(auto* child:messages->findChildren<QWidget*>("videoAttachment",Qt::FindDirectChildrenOnly)){
            auto* candidate=static_cast<VideoAttachment*>(child);
            if(candidate->key==key){video=candidate;break;}
        }
        if(!video){video=new VideoAttachment(api,route,name,compact);video->key=key;video->largeOriginal=file["size"].toInteger()>20LL*1048576;}
        video->download=[this,route,name]{downloadVideo(route,name);};video->enlarge=enlarge;
        video->geometryChanged=[this]{relayoutChat();};
        line->setAlignment(Qt::AlignTop|Qt::AlignLeft);line->addWidget(video);video->show();return;
    }
    auto* open=new PreviewButton;open->setAccessibleName(UiText::Open()+name);open->setText(name+"    "+QLocale().formattedDataSize(file.value("size").toInteger()));
    open->setIcon(Ui::icon("paperclip"));open->setCursor(Qt::PointingHandCursor);open->setMinimumHeight(42);open->setMaximumWidth(480);
    open->setStyleSheet("QPushButton{background:#191919;border:0;border-radius:10px;text-align:left;padding:8px 12px;font-size:12px} QPushButton:hover{background:#242424}");
    line->setAlignment(Qt::AlignLeft|Qt::AlignTop);line->addWidget(open);
    if(QStringList{"jpg","jpeg","webp","png","gif"}.contains(ext)){
        const QSize limit(qMin(compact?240:480,qMax(100,messages->viewport()->width()-58)),compact?120:280);
        open->setFixedSize(limit.width(),qRound(limit.width()*9./16));
        const auto key=api.server.toString()+"/"+selfId+route;
        auto show=[this,open,limit](QPixmap pix){
            const auto old=open->size();
            auto size=pix.size().scaled(limit,Qt::KeepAspectRatio);open->setFixedSize(size);open->setIconSize(size);
            open->setText({});open->preview=pix;open->update();open->setStyleSheet("QPushButton{border:0;border-radius:10px;padding:0;background:#151515}");
            if(old!=size && open->isVisible())relayoutChat();
        };
        if(const auto* cached=thumbnails.object(key))show(*cached);
        else open->loadOnPaint=[this,key,open,show,route,large=file["size"].toInteger()>20LL*1048576]{
            if(const auto* cached=thumbnails.object(key)){show(*cached);return;}
            const auto thumbPath=api.posterPath(route);
            if(!thumbPath.isEmpty()){
                QImageReader saved(thumbPath,"PNG");auto size=saved.size();
                if(size.isValid() && size.width()<=1440 && size.height()<=840){
                    const auto pix=QPixmap::fromImage(saved.read());
                    if(!pix.isNull()){thumbnails.insert(key,new QPixmap(pix),qMax(1,pix.width()*pix.height()*4/1024));show(pix);return;}
                }
            }
            if(large)return;
            loadImage(route,[this,key,guard=QPointer<QPushButton>(open),show,route,gen=channelGeneration](const QString& path){
                if(!guard || path.isEmpty() || gen!=channelGeneration)return;
                QImageReader reader(path);reader.setAutoTransform(true);auto size=reader.size();
                if(!size.isValid()||qint64(size.width())*size.height()>24000000)return;
                const QSize pixels=QSize(480,280)*qBound(1.,guard->devicePixelRatioF(),3.);
                if(size.width()>pixels.width() || size.height()>pixels.height())reader.setScaledSize(size.scaled(pixels,Qt::KeepAspectRatio));
                auto image=reader.read();if(image.isNull())return;
                const auto target=api.posterPath(route);
                if(!target.isEmpty()){
                    QSaveFile out(target);
                    if(out.open(QIODevice::WriteOnly) && image.save(&out,"PNG") && out.commit())api.trimMedia();
                }
                auto pix=QPixmap::fromImage(std::move(image));
                thumbnails.insert(key,new QPixmap(pix),qMax(1,pix.width()*pix.height()*4/1024));show(pix);
            });
        };
    }
    if(!supported(ext)){
        open->setText({});open->setIcon({});open->setAccessibleName(UiText::Download()+name);
        open->setFixedSize(qBound(152,messages->viewport()->width()-58,480),58);
        open->setStyleSheet("QPushButton{background:#242424;border:0;border-radius:12px;padding:0} QPushButton:hover{background:#303030}");
        auto* content=new QHBoxLayout(open);content->setContentsMargins(12,8,12,8);content->setSpacing(12);
        auto* mark=Ui::text("");mark->setPixmap(Ui::icon("download").pixmap(18,18));content->addWidget(mark);
        auto* meta=new QVBoxLayout;meta->setSpacing(3);meta->setAlignment(Qt::AlignVCenter);content->addLayout(meta,1);
        meta->addWidget(Ui::text(name,"font-weight:500;font-size:13px"));meta->addWidget(Ui::text(QString::number(file["size"].toDouble()/1024.,'f',1)+UiText::Kb2()+(file["scanned"].toBool()?UiText::ScannedForViruses():UiText::NotScannedForViruses()),"font-size:10px;color:#999"));
        content->addWidget(Ui::text(UiText::Download2(),"font-size:10px;color:#8fc4f5"));connect(open,&QPushButton::clicked,open,save);return;
    }
    auto* dl=Ui::action("download",UiText::DownloadAttachment());
    if(QStringList{"jpg","jpeg","png","webp","gif"}.contains(ext)){dl->setParent(open);open->tool=dl;open->positionTool();dl->setStyleSheet("background:#171717;border:0;border-radius:9px;padding:0");dl->hide();}else line->addWidget(dl);connect(dl,&QPushButton::clicked,dl,save);
    connect(open,&QPushButton::clicked,open,enlarge);
}

void DesktopWindow::viewAttachments(QJsonArray files,int index){
    if(index<0||index>=files.size())return;
    if(attachmentViewer)return;
    auto* viewer=new AttachmentPage(pages);
    attachmentViewer=viewer;
    QStringList ids;for(auto f:files)ids.append(f.toObject()["id"].toString());viewer->setProperty("fileIds",ids);
    viewer->setObjectName("attachmentViewer");
    auto* origin=pages->currentWidget();
    const QPointer<QWidget> focus=QApplication::focusWidget();
    pages->addWidget(viewer);
    pages->setCurrentWidget(viewer);
    auto dismiss=[this,viewer,origin,focus]{
        if(attachmentViewer!=viewer)return;
        attachmentViewer=nullptr;
        pages->setCurrentWidget(origin);
        pages->removeWidget(viewer);
        viewer->deleteLater();
        if(focus && focus->isVisible())focus->setFocus();
    };
    auto selection=std::make_shared<int>(index);
    viewer->dismiss=dismiss;
    viewer->setStyleSheet("QWidget#attachmentViewer{background:#080808} QPushButton{background:#202020;border:0;border-radius:10px}");
    auto* layout=new QVBoxLayout(viewer);layout->setContentsMargins(12,12,12,12);
    auto* header=new QHBoxLayout;auto* name=Ui::text("");auto* count=Ui::text("","color:#aaa");
    header->addWidget(name);header->addWidget(count);header->addStretch();
    auto* close=Ui::action("close",UiText::CloseViewer());header->addWidget(close);layout->addLayout(header);
    connect(close,&QPushButton::clicked,viewer,dismiss);
    auto* content=new QHBoxLayout;layout->addLayout(content,1);
    auto* previous=Ui::action("back",UiText::PreviousAttachment());content->addWidget(previous);
    auto* center=new QVBoxLayout;content->addLayout(center,1);
    auto* next=Ui::action("arrow",UiText::NextAttachment());content->addWidget(next);
    auto* download=Ui::action("download",UiText::DownloadFile());layout->addWidget(download,0,Qt::AlignRight);
    auto save=[this,files,selection]{
        const auto file=files[*selection].toObject();
        auto destination=QFileDialog::getSaveFileName(this,UiText::DownloadAttachment(),QFileInfo(file["name"].toString()).fileName());
        if(!destination.isEmpty())api.download("/api/files/"+QString::fromLatin1(QUrl::toPercentEncoding(file["id"].toString())),destination,[this](QString err){note(err);});
    };
    connect(download,&QPushButton::clicked,viewer,save);
    auto render=[=,this](bool playSelected=false){
        const int index=*selection;
        while(auto* item=center->takeAt(0)){delete item->widget();delete item;}
        auto file=files[index].toObject();
        const QString filename=file["name"].toString(),ext=QFileInfo(filename).suffix().toLower();
        const QString path="/api/files/"+QString::fromLatin1(QUrl::toPercentEncoding(file["id"].toString()));
        const bool video=QStringList{"webm","mp4","ogv"}.contains(ext);
        name->setText(filename);count->setText(files.size()>1?QString("%1 / %2").arg(index+1).arg(files.size()):QString());
        previous->setVisible(index>0);next->setVisible(index+1<files.size());download->setVisible(!video);
        if(video){
            auto* card=new VideoAttachment(api,path,filename,false,true);card->largeOriginal=file["size"].toInteger()>20LL*1048576;card->download=[this,path,filename]{downloadVideo(path,filename);};center->addWidget(card,1);
            if(playSelected)card->requestPlay();
            return;
        }
        auto* preview=new GalleryImage;preview->setText(UiText::Loading());preview->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);preview->setAlignment(Qt::AlignCenter);center->addWidget(preview,1);
        const QSize displayLimit=viewer->size()-QSize(110,110);
        if(const auto* thumbnail=thumbnails.object(api.server.toString()+"/"+selfId+path))preview->setPixmap(*thumbnail);
        auto display=[guard=QPointer<QLabel>(preview),displayLimit](const QString& localPath){
            if(!guard)return;
            if(localPath.isEmpty()){guard->setText(UiText::CouldNotOpenTheImage());return;}
            QImageReader reader(localPath);
            reader.setAutoTransform(true);auto size=reader.size();
            if(!size.isValid()||qint64(size.width())*size.height()>24000000){guard->setText(UiText::CouldNotOpenTheImage());return;}
            reader.setScaledSize(size.scaled(displayLimit,Qt::KeepAspectRatio));
            auto image=reader.read();if(image.isNull()){guard->setText(UiText::CouldNotOpenTheImage());return;}
            guard->setPixmap(QPixmap::fromImage(image));
        };
        if(file["size"].toInteger()>20LL*1048576){
            auto temp=std::make_shared<QTemporaryDir>();
            if(temp->isValid()){const auto original=temp->filePath("original");api.download(path,original,[display,temp,original](QString err){display(err.isEmpty()?original:QString());},1536LL*1048576,preview);}
            else display({});
        }else loadImage(path,display);
        for(int neighbor:{index-1,index+1}){
            if(neighbor<0 || neighbor>=files.size())continue;
            const auto adjacent=files[neighbor].toObject();
            if(adjacent["size"].toInteger()<=20LL*1048576 && QStringList{"jpg","jpeg","png","webp","gif"}.contains(QFileInfo(adjacent["name"].toString()).suffix().toLower()))
                loadImage("/api/files/"+QString::fromLatin1(QUrl::toPercentEncoding(adjacent["id"].toString())));
        }
    };
    auto step=[files,selection,render](int delta){const int to=*selection+delta;if(to>=0&&to<files.size()){*selection=to;render();}};
    connect(previous,&QPushButton::clicked,viewer,[step]{step(-1);});
    connect(next,&QPushButton::clicked,viewer,[step]{step(1);});
    for(int delta:{-1,1}){
        auto* shortcut=new QShortcut(delta<0?Qt::Key_Left:Qt::Key_Right,viewer);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,viewer,[step,delta]{step(delta);});
    }
    render(files.size()>1);close->setFocus();
}
