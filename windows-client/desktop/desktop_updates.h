#pragma once
#include "interface_text.h"
#include <QtWidgets>
#include <QtNetwork>
#include <functional>

struct DesktopRelease {
    QString version,digest;
    qint64 size=0;
    static constexpr qint64 maxBytes=512LL*1024*1024;
    bool read(const QJsonObject& data){
        version=data["version"].toString();digest=data["sha256"].toString();size=data["size"].toInteger();
        static const QRegularExpression semver(R"(^(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})$)");
        static const QRegularExpression sha256("^[a-f0-9]{64}$");
        return data["schema"].toInt()==1 && data["platform"]=="windows-x64" &&
               semver.match(version).hasMatch() && sha256.match(digest).hasMatch() && size>0 && size<=maxBytes;
    }
    QUrl archive() const {
        return QUrl(QStringLiteral("https://github.com/DiabloEN/Nimbo/releases/download/desktop-v%1/Nimbo-Desktop-%1-portable.zip").arg(version));
    }
};

// A desktop release belongs to the client publisher, independently of the selected Nimbo server.
class DesktopUpdates final : public QObject {
    friend class NativeDesktopTests;
public:
    explicit DesktopUpdates(QObject* parent, QString profileDirectory, std::function<bool()> callBusy)
        : QObject(parent), profileDir(QFileInfo(profileDirectory).absoluteFilePath()), busyCall(std::move(callBusy)) {
        connect(&timer,&QTimer::timeout,this,[this]{check();});
        timer.start(6*60*60*1000);
        QTimer::singleShot(1500,this,[this]{check();});
    }
    std::function<void()> availabilityChanged;
    QString offeredVersion()const{return available()?version:QString();}
    bool available()const{return !asset.isEmpty();}
    bool active()const{return working;}
    QString status()const{return UiText::WindowsApp()+QCoreApplication::applicationVersion()+" · "+QString::fromUtf8(UiText::value(message))+
        (message==UiText::Id::VersionAvailable?version+".":message==UiText::Id::DownloadingVersion?version+"…":QString());}
    void observe(QObject* owner,std::function<void()> callback){
        observer=owner;changed=std::move(callback);
        connect(owner,&QObject::destroyed,this,[this]{if(!observer)changed={};});
    }
private:
    QString profileDir,version,digest;
    UiText::Id message=UiText::Id::CheckPending;
    QUrl asset;
    qint64 expected=0;
    bool working=false;
    QNetworkAccessManager network;
    QTimer timer;
    std::function<bool()> busyCall;
    QPointer<QObject> observer;
    std::function<void()> changed;
    void refresh(){if(availabilityChanged)availabilityChanged();if(observer && changed)changed();}
    void finish(UiText::Id text){working=false;message=text;refresh();}
public:
    void check() {
        if(working)return;
        if(!QFile::exists(helper())){finish(UiText::Id::TheUpdateComponentIsMissingFromThis);return;}
        working=true;message=UiText::Id::CheckingForWindowsUpdates;refresh();
        QNetworkRequest request(QUrl("https://raw.githubusercontent.com/DiabloEN/Nimbo/main/windows-client/updates/windows-x64.json"));
        request.setTransferTimeout(15000);
        request.setRawHeader("User-Agent","Nimbo-Desktop-Updater");
        auto* reply=network.get(request);
        connect(reply,&QIODevice::readyRead,reply,[reply]{if(reply->bytesAvailable()>8192)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply]{
            reply->deleteLater();
            if(reply->error()!=QNetworkReply::NoError){
                const int http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                finish(http==404?UiText::Id::TheWindowsUpdateChannelIsUnavailableHttp:UiText::Id::CouldNotCheckForUpdatesTryAgain);return;
            }
            DesktopRelease release;
            if(!release.read(QJsonDocument::fromJson(reply->readAll()).object())){finish(UiText::Id::InvalidWindowsUpdateInformation);return;}
            if(QVersionNumber::fromString(release.version)<=QVersionNumber::fromString(QCoreApplication::applicationVersion())){
                asset.clear();version.clear();
                finish(UiText::Id::YouHaveTheLatestVersion);return;
            }
            version=release.version;digest=release.digest;expected=release.size;asset=release.archive();
            finish(UiText::Id::VersionAvailable);
        });
    }
    QString helper()const{return QCoreApplication::applicationDirPath()+"/update-desktop.ps1";}
    void install() {
        if(working||asset.isEmpty())return;
        if(busyCall()){finish(UiText::Id::EndYourCallBeforeUpdating);return;}
        auto temp=std::make_shared<QTemporaryDir>();
        if(!temp->isValid()){finish(UiText::Id::CouldNotCreateTheUpdateDirectory);return;}
        auto archive=std::make_shared<QFile>(temp->filePath("client.zip"));
        if(!archive->open(QIODevice::WriteOnly)){finish(UiText::Id::CouldNotCreateTheUpdateFile);return;}
        working=true;message=UiText::Id::DownloadingVersion;refresh();
        QNetworkRequest request(asset);request.setTransferTimeout(60000);
        auto* reply=network.get(request);
        reply->setReadBufferSize(256*1024);
        auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
        auto collect=[this,archive,hash,reply]{
            const auto data=reply->readAll();
            if(archive->pos()+data.size()>expected || archive->write(data)!=data.size()){reply->abort();return;}
            hash->addData(data);
        };
        connect(reply,&QIODevice::readyRead,this,collect);
        connect(reply,&QNetworkReply::finished,this,[this,temp,archive,hash,reply,collect]{
            reply->deleteLater();
            if(reply->error()!=QNetworkReply::NoError){finish(UiText::Id::DownloadInterruptedTryAgain);return;}
            collect();const bool valid=archive->pos()==expected && archive->flush() && hash->result().toHex()==digest.toLatin1();
            archive->close();
            if(!valid){finish(UiText::Id::TheReleaseChecksumDoesNotMatch);return;}
            if(busyCall()){finish(UiText::Id::InstallationWillWaitUntilTheCallEnds);return;}
            const auto script=temp->filePath("update-desktop.ps1");
            if(!QFile::copy(helper(),script)){finish(UiText::Id::CouldNotPrepareTheInstaller);return;}
            const auto shell=qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0/powershell.exe";
            QStringList args={"-NoLogo","-NoProfile","-NonInteractive","-WindowStyle","Hidden","-ExecutionPolicy","RemoteSigned","-File",script,
                "-InstallDir",QCoreApplication::applicationDirPath(),"-ProfileDir",profileDir,
                "-ParentId",QString::number(QCoreApplication::applicationPid()),"-Version",version,"-Digest",digest};
            if(!QProcess::startDetached(shell,args,temp->path())){finish(UiText::Id::CouldNotStartTheInstaller);return;}
            temp->setAutoRemove(false);QCoreApplication::quit();
        });
    }
};
