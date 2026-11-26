#include "interface_text.h"
#include "api.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QDateTime>
#include <memory>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QTimer>
#include <QSet>

QJsonObject ServerApi::cachedPreview(const QString& path){
    auto* file=previews.object(path);
    if(!file)return {};
    if(QFileInfo(*file).lastModified().secsTo(QDateTime::currentDateTime())>600){
        previews.remove(path);return {};
    }
    if(!file->open())return {};
    const auto result=QJsonDocument::fromJson(file->readAll()).object();
    file->close();
    return result;
}
void ServerApi::rememberPreview(const QString& path,const QJsonObject& result){
    const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact);
    if(bytes.size()>300000 || result["title"].toString().isEmpty())return;
    auto file=std::make_unique<QTemporaryFile>();
    if(!file->open() || file->write(bytes)!=bytes.size() || !file->flush())return;
    file->close();
    previews.insert(path,file.release(),qMax(1,int((bytes.size()+1023)/1024)));
}

void ServerApi::reset() {
    stopMemberPresence();presenceUnsupported=false;
    uploads.clear();++generation;++cacheEpoch;checkingDisk=false;deferredDownloads.clear();mediaDir.clear();mediaCursor.clear();cacheReady=false;pendingMedia.clear();renewalWaiters.clear();previews.clear();
    server = QUrl(); token.clear();
    for (auto* reply : network.findChildren<QNetworkReply*>()) if (reply->isRunning()) reply->abort();
}
QString ServerApi::activeWorkspacePath() const {
    return presenceReply || presenceUnsupported ? QStringLiteral("/api/workspace") :
        QStringLiteral("/api/workspace?webTab=open");
}
void ServerApi::stopMemberPresence(){
    const auto reply=presenceReply;
    presenceReply.clear();
    if(reply)reply->abort();
}
void ServerApi::syncMemberPresence(const QJsonObject& workspace){
    if(!workspace.value("onlineMembers").isArray()){
        presenceUnsupported=true;
        stopMemberPresence();
        return;
    }
    startMemberPresence();
}
void ServerApi::startMemberPresence(){
    if(presenceUnsupported || presenceReply || token.isEmpty())return;
    auto request=headers("/api/web-presence");
    request.setTransferTimeout(20000);
    auto* reply=network.get(request);
    presenceReply=reply;
    reply->setReadBufferSize(4096);
    connect(reply,&QNetworkReply::metaDataChanged,this,[reply]{
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(status>=200 && status!=204 && reply->isRunning())reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        reply->deleteLater();
        if(presenceReply!=reply)return;
        presenceReply.clear();
        const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(reply->error()!=QNetworkReply::NoError || status!=204){
            // Old servers may return their HTML entry page on an unknown route.
            presenceUnsupported=status==404 || status==405 || status==200;
            qWarning("WINDOWS_MEMBER_PRESENCE_LOST: HTTP %d, network %d",status,int(reply->error()));
            if(memberPresenceLost)memberPresenceLost();
            return; // A successful workspace poll retries transient failures.
        }
        startMemberPresence();
    });
}
QNetworkRequest ServerApi::headers(const QString& path) const {
    QNetworkRequest request(QUrl(server.toString() + path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    request.setRawHeader("Origin","https://desktop.nimbo.invalid");
    request.setRawHeader("X-Nimbo-Client","1");
    if (!token.isEmpty()) request.setRawHeader("Authorization","Bearer " + token.toLatin1());
    return request;
}
void ServerApi::request(QByteArray method, QString path, QJsonObject body, Callback callback) {
    requestOnce(method,path,body,callback,true);
}
QNetworkReply* ServerApi::sendJson(const QByteArray& method,const QString& path,const QJsonObject& body){
    auto request = headers(path);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return network.sendCustomRequest(request,method,body.isEmpty()?QByteArray{}:QJsonDocument(body).toJson(QJsonDocument::Compact));
}
void ServerApi::profileSession(const QString& key,std::function<void(QJsonObject,QString,int)> callback){
    auto* reply=sendJson("POST","/api/profile-session",{{"profileKey",key}});
    finishJson(reply,[reply,callback=std::move(callback)](QJsonObject result,QString error){
        callback(result,error,reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    });
}
void ServerApi::requestOnce(QByteArray method,QString path,QJsonObject body,Callback callback,bool retry){
    auto* reply=sendJson(method,path,body);
    Retry again;
    if(retry && path!="/api/profile-session" && path!="/api/auth" && path!="/api/access-session")
        again=[=,this]{requestOnce(method,path,body,callback,false);};
    finishJson(reply,callback,std::move(again));
}
bool ServerApi::renew(QNetworkReply* reply,Retry retry,Retry failure){
    if(!retry || token.isEmpty() || !activeProfileKey || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=401)return false;
    if(reply->request().rawHeader("Authorization")!="Bearer "+token.toLatin1()){retry();return true;}
    const auto key=activeProfileKey();if(key.isEmpty())return false;
    renewalWaiters.append([retry,failure](bool ok){if(ok)retry();else failure();});
    if(renewalWaiters.size()>1)return true;
    requestOnce("POST","/api/profile-session",{{"profileKey",key}},[this](QJsonObject data,QString error){
        const auto renewed=data["session"].toString();
        const bool ok=error.isEmpty()&&!renewed.isEmpty();
        if(ok)token=renewed;
        const auto waiters=std::exchange(renewalWaiters,{});
        const auto current=generation;
        for(const auto& callback:waiters)if(current==generation)callback(ok);
    },false);
    return true;
}
void ServerApi::finishJson(QNetworkReply* reply, Callback callback,Retry retry) {
    const auto run = generation;
    connect(reply, &QNetworkReply::readyRead, reply, [reply] { if (reply->bytesAvailable() > 8*1024*1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this,reply,run,callback,retry] {
        reply->deleteLater();
        if (run != generation) return;
        if(renew(reply,retry,[callback]{callback({},UiText::SignInToTheServerAgain());}))return;
        auto document = QJsonDocument::fromJson(reply->readAll());
        auto data = document.object();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300 || !document.isObject()){
            error=UiText::serverMessage(data["error"].toString());
            if(error.isEmpty()){
                if(status==401)error=UiText::SignInToTheServerAgain();
                else if(status>=400)error=QString(UiText::TheServerReturnedHttpError1()).arg(status);
                else if(reply->error()==QNetworkReply::TimeoutError)error=UiText::TheServerDidNotRespondInTime();
                else if(reply->error()==QNetworkReply::SslHandshakeFailedError)error=UiText::CouldNotEstablishASecureConnectionTo();
                else if(status>=200 && status<300)error=UiText::TheServerReturnedAnUnexpectedResponseFormat();
                else error=UiText::NoResponseReceivedCheckTheAddressAnd();
            }
        }
        callback(data, error);
    });
}
void ServerApi::bytes(QString path, int limit, std::function<void(QByteArray,QString)> callback) {
    bytesOnce(path,limit,callback,true);
}
void ServerApi::bytesOnce(QString path,int limit,std::function<void(QByteArray,QString)> callback,bool retry){
    auto* reply = network.get(headers(path));
    const auto run = generation;
    connect(reply,&QNetworkReply::readyRead,reply,[reply,limit]{if(reply->bytesAvailable()>limit)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,run,limit,callback,retry,path]{
        reply->deleteLater();
        if(run!=generation)return;
        if(retry && renew(reply,[=,this]{bytesOnce(path,limit,callback,false);},[callback]{callback({},UiText::SignInToTheServerAgain());}))return;
        if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200 || reply->bytesAvailable()>limit){
            callback({},UiText::FileUnavailable());return;
        }
        callback(reply->readAll(),{});
    });
}
void ServerApi::upload(QString path, QStringList files, QJsonObject fields, Callback callback,std::function<void(int)> progress) {
    const auto id=fields["clientId"].toString();
    if(id.isEmpty()){uploadOnce(path,files,fields,callback,progress,true);return;}
    auto state=std::make_shared<UploadState>();uploads.insert(id,state);
    uploadOnce(path,files,fields,[this,id,callback,state](QJsonObject data,QString err){
        uploads.remove(id);callback(data,state->cancelled && !err.isEmpty()?UiText::DownloadCanceled():err);
    },progress,true);
}
void ServerApi::cancelUpload(const QString& id){
    auto state=uploads.value(id);if(!state)return;state->cancelled=true;
    if(state->reply && state->reply->isRunning())state->reply->abort();
}
void ServerApi::uploadOnce(QString path,QStringList files,QJsonObject fields,Callback callback,std::function<void(int)> progress,bool retry){
    auto state=uploads.value(fields["clientId"].toString());
    if(state && state->cancelled){callback({},UiText::DownloadCanceled());return;}
    auto* form=new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for(auto it=fields.begin();it!=fields.end();++it){
        QHttpPart part;
        part.setRawHeader("Content-Disposition","form-data; name=\""+it.key().toUtf8()+"\"");
        part.setBody(it.value().isArray()?QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact):it.value().toString().toUtf8());
        form->append(part);
    }
    for(const auto& name:files){
        auto* file=new QFile(name,form);
        if(!file->open(QIODevice::ReadOnly)){delete form;callback({},UiText::CouldNotOpenTheAttachment());return;}
        QByteArray filename=QFileInfo(name).fileName().toUtf8().replace('"','_').replace('\r','_').replace('\n','_');
        QHttpPart part;
        part.setRawHeader("Content-Disposition","form-data; name=\"file\"; filename=\""+filename+"\"");
        part.setHeader(QNetworkRequest::ContentTypeHeader,"application/octet-stream");
        part.setBodyDevice(file);form->append(part);
    }
    auto request=headers(path);request.setTransferTimeout(240000);
    auto* reply=network.post(request,form);form->setParent(reply);if(state)state->reply=reply;
    if(progress)connect(reply,&QNetworkReply::uploadProgress,reply,[progress](qint64 sent,qint64 total){
        if(total>0)progress(int(qBound(qint64(0),sent*100/total,qint64(100))));
    });
    Retry again;
    if(retry)again=[=,this]{uploadOnce(path,files,fields,callback,progress,false);};
    finishJson(reply,callback,std::move(again));
}
namespace {
QString mediaKey(QString route) {
    if(!route.startsWith("/api/files/"))return {};
    const auto id=route.mid(11).toLatin1();
    const auto raw=QByteArray::fromBase64(id,QByteArray::Base64UrlEncoding);
    if(raw.size()!=32 || raw.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals)!=id)return {};
    return QString::fromLatin1(raw.toHex());
}
QString attachmentId(const QString& key){
    return QString::fromLatin1(QByteArray::fromHex(key.toLatin1()).toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
}
bool copyAtomic(const QString& source,const QString& destination,qint64 limit){
    QFile in(source);QSaveFile out(destination);
    if(!in.open(QIODevice::ReadOnly)||(limit>=0 && in.size()>limit)||!out.open(QIODevice::WriteOnly))return false;
    char block[65536];
    while(true){
        const auto count=in.read(block,sizeof block);
        if(count<0 || (count && out.write(block,count)!=count))return false;
        if(!count)return out.commit();
    }
}
void pruneMedia(const QString& root){
    QFileInfoList entries;qint64 total=0;
    QDirIterator files(root,{"*.bin","*.png","*.duration"},QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(files.hasNext()){files.next();entries.append(files.fileInfo());total+=entries.last().size();}
    std::sort(entries.begin(),entries.end(),[](const QFileInfo& a,const QFileInfo& b){return a.lastModified()<b.lastModified();});
    int count=entries.size();
    for(const auto& f:entries){
        if(total<=128*1024*1024 && count<=256)break;
        if(QFile::remove(f.filePath())){total-=f.size();--count;}
    }
}
}
void ServerApi::trimMedia(){if(!mediaRoot.isEmpty())pruneMedia(mediaRoot);}
QString ServerApi::durationPath(const QString& route) const{
    auto path=posterPath(route);
    if(path.isEmpty())return {};
    path.chop(3);return path+"duration";
}
QString ServerApi::posterPath(const QString& route) const{
    auto key=mediaKey(route);
    if(!cacheReady || key.isEmpty() || !QFileInfo::exists(mediaDir+"/"+key+".bin"))return {};
    return mediaDir+"/"+key+".png";
}
void ServerApi::syncMedia(const QString& root,const QString& member){
    if(member.isEmpty() || server.isEmpty() || token.isEmpty())return;
    const auto scope=QCryptographicHash::hash((server.toString()+"\n"+member).toUtf8(),QCryptographicHash::Sha256).toHex();
    auto dir=root+"/"+scope;
    if(mediaDir==dir)return;
    mediaRoot=root;mediaDir=dir;cacheReady=false;checkingDisk=true;mediaCursor.clear();++cacheEpoch;
    waitForMediaChanges();
}
void ServerApi::resumeDownloads(){
    checkingDisk=false;
    const auto queued=std::exchange(deferredDownloads,{});
    for(const auto& start:queued)start();
}
void ServerApi::waitForMediaChanges(){
    const auto scope=mediaDir;
    if(scope.isEmpty())return;
    request("POST","/api/media-cache/sync",{{"cursor",mediaCursor}},[this,scope](QJsonObject data,QString error){
        if(scope!=mediaDir)return;
        const auto revision=data["cursor"].toString();
        if(!error.isEmpty() || revision.isEmpty()){
            cacheReady=false;mediaCursor.clear();resumeDownloads();
            QTimer::singleShot(30000,this,[this,scope,run=generation]{if(run==generation && mediaDir==scope)waitForMediaChanges();});
        }else if(revision!=mediaCursor){reconcileMedia(revision);}
        else waitForMediaChanges();
    });
}
void ServerApi::reconcileMedia(const QString& revision){
    const auto scope=mediaDir;
    cacheReady=false;checkingDisk=true;++cacheEpoch;
    QSet<QString> ids;
    for(const auto& f:QDir(scope).entryInfoList({"*.bin","*.png","*.duration"},QDir::Files))ids.insert(attachmentId(f.completeBaseName()));
    for(const auto& id:pendingMedia.uniqueKeys())ids.insert(id);
    if(visibleMedia)for(const auto& id:visibleMedia())ids.insert(id);
    auto remaining=std::make_shared<QStringList>(ids.values());
    auto missing=std::make_shared<QStringList>();
    auto page=std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak=page;
    *page=[this,scope,revision,remaining,missing,weak]{
        QJsonArray batch;
        while(!remaining->isEmpty() && batch.size()<256)batch.append(remaining->takeLast());
        request("POST","/api/media-cache/sync",{{"cursor",revision},{"ids",batch}},[this,scope,revision,remaining,missing,next=weak.lock()](QJsonObject data,QString error){
            if(scope!=mediaDir)return;
            if(!error.isEmpty() || data["cursor"].toString()!=revision){
                mediaCursor.clear();resumeDownloads();QTimer::singleShot(error.isEmpty()?0:3000,this,[this,scope,run=generation]{if(run==generation && scope==mediaDir)waitForMediaChanges();});return;
            }
            for(auto id:data["missing"].toArray())missing->append(id.toString());
            if(!remaining->isEmpty()){(*next)();return;}
            for(const auto& id:*missing){
                const auto key=mediaKey("/api/files/"+id);if(key.isEmpty())continue;
                QFile::remove(scope+"/"+key+".bin");QFile::remove(scope+"/"+key+".png");QFile::remove(scope+"/"+key+".duration");
            }
            mediaCursor=revision;cacheReady=QDir().mkpath(scope);
            if(mediaInvalidated)mediaInvalidated(*missing);
            resumeDownloads();
            waitForMediaChanges();
        });
    };
    (*page)();
}
void ServerApi::download(QString path, QString destination, std::function<void(QString)> callback,qint64 limit,QObject* owner) {
    const auto key=mediaKey(path);
    if(checkingDisk && !key.isEmpty()){
        deferredDownloads.append([this,path,destination,callback,limit,owned=bool(owner),scope=QPointer<QObject>(owner)]{if(owned&&!scope){callback(UiText::DownloadCanceled());return;}download(path,destination,callback,limit,scope);});
        return;
    }
    const auto cached=mediaDir+"/"+key+".bin";
    if(cacheReady && !key.isEmpty() && copyAtomic(cached,destination,limit)){
        QFile used(cached);if(used.open(QIODevice::ReadOnly))used.setFileTime(QDateTime::currentDateTimeUtc(),QFileDevice::FileModificationTime);
        callback({});return;
    }
    const auto id=key.isEmpty()?QString():attachmentId(key);
    if(!id.isEmpty())pendingMedia.insert(id,true);
    downloadOnce(path,destination,[this,id,cached,destination,callback,epoch=cacheEpoch,scope=mediaDir](QString error){
        if(!id.isEmpty()){
            auto it=pendingMedia.find(id);if(it!=pendingMedia.end())pendingMedia.erase(it);
            if(error.isEmpty() && cacheReady && epoch==cacheEpoch && scope==mediaDir &&
               !QFileInfo::exists(cached) && copyAtomic(destination,cached,20*1024*1024))pruneMedia(mediaRoot);
        }
        callback(error);
    },true,limit,owner);
}
void ServerApi::downloadOnce(QString path,QString destination,std::function<void(QString)> callback,bool retry,qint64 limit,QPointer<QObject> owner){
    auto file=std::make_shared<QSaveFile>(destination);
    if(!file->open(QIODevice::WriteOnly)){callback(UiText::CouldNotOpenTheDestinationFile());return;}
    const auto run=generation;
    auto request=headers(path);request.setTransferTimeout(120000);
    auto* reply=network.get(request);reply->setReadBufferSize(256*1024);
    if(owner)connect(owner,&QObject::destroyed,reply,[reply]{if(reply->isRunning())reply->abort();});
    connect(reply,&QNetworkReply::readyRead,reply,[reply,file,limit]{
        auto data=reply->readAll();if((limit>=0 && file->pos()+data.size()>limit) || file->write(data)!=data.size())reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,file,run,callback,path,destination,retry,limit,owner,owned=bool(owner)]{
        reply->deleteLater();if(run!=generation)return;
        if(retry && renew(reply,[=,this]{if(owned&&!owner){callback(UiText::DownloadCanceled());return;}downloadOnce(path,destination,callback,false,limit,owner);},[callback]{callback(UiText::SignInToTheServerAgain());})){
            file->cancelWriting();return;
        }
        auto tail=reply->readAll();
        if(reply->error()!=QNetworkReply::NoError||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200||
           (limit>=0 && file->pos()+tail.size()>limit)||file->write(tail)!=tail.size()||!file->commit()){callback(UiText::CouldNotDownloadTheFile());return;}
        callback({});
    });
}
