#pragma once
#include "interface_text.h"
#include "api.h"
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
    explicit DesktopUpdates(QObject* parent, QString profileDirectory)
        : QObject(parent), profileDir(QFileInfo(profileDirectory).absoluteFilePath()) {
        connect(&timer,&QTimer::timeout,this,[this]{check();});
        timer.start(6*60*60*1000);
        QTimer::singleShot(1500,this,[this]{check();});
    }
    std::function<void()> availabilityChanged;
    std::function<QJsonObject()> restartView;
    std::function<void()> restartApplication;
    QString offeredVersion()const{return available()?version:QString();}
    bool available()const{return !asset.isEmpty();}
    bool active()const{return working;}
    bool installing()const{return working && downloading;}
    qint64 downloadedBytes()const{return received;}
    qint64 totalBytes()const{return expected;}
    QString progressText()const{
        if(message!=UiText::Id::DownloadingVersion)return QString::fromUtf8(UiText::value(message));
        return QString::fromUtf8(UiText::UpdateDownloadProgress()).arg(UiText::locale().toString(received/1048576.,'f',1),UiText::locale().toString(expected/1048576.,'f',1))
            +QStringLiteral(" · %1%").arg(expected?received*100/expected:0);
    }
    QString status()const{
        const auto installed=UiText::WindowsApp()+QCoreApplication::applicationVersion();
        if(message==UiText::Id::YouHaveTheLatestVersion)return installed;
        return installed+" · "+QString::fromUtf8(UiText::value(message))+
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
    bool working=false,downloading=false;
    qint64 received=0;
    int attempts=0;
    QNetworkAccessManager network;
    QTimer timer;
    QPointer<QObject> observer;
    std::function<void()> changed;
    void refresh(){if(availabilityChanged)availabilityChanged();if(observer && changed)changed();}
    void finish(UiText::Id text){
        const bool proceed=installAfterCheck && text==UiText::Id::VersionAvailable;
        installAfterCheck=false;working=false;downloading=false;message=text;refresh();
        if(proceed)install();
    }
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
    // Keep the original server and session while Settings closes or the user changes rooms.
    bool coordinating() const { return updateServer!=nullptr; }
    void clearServerIssue(const QUrl& server){if(issueServer==server)updateIssue.clear();}
    bool checkingServer(const QUrl& server)const{return updateServer && updateServer->server==server;}
    QString serverIssue(const QUrl& server) const { return server==issueServer?updateIssue:QString(); }
    void updateBoth(const ServerApi* ownerApi) {
        if(coordinating() || installing())return;
        if(!ownerApi){installLatestClient();return;}
        issueServer=ownerApi->server;
        updateIssue.clear();
        updateServer=new ServerApi(this);
        updateServer->server=ownerApi->server;
        updateServer->token=ownerApi->token;
        serverDeadline.start();
        refresh();
        serverStep("GET","/api/server-update",{},false);
    }
private:
    ServerApi* updateServer=nullptr;
    QElapsedTimer serverDeadline;
    QUrl issueServer;
    QString updateIssue;
    bool installAfterCheck=false;
    void installLatestClient(){
        if(available() && !working){install();return;}
        if(!installing()){installAfterCheck=true;check();}
    }
    void serverDone(QString issue){
        updateIssue=std::move(issue);
        updateServer->deleteLater();
        updateServer=nullptr;
        refresh();
        installLatestClient();
    }
    void serverStep(QByteArray method,QString path,QJsonObject body,bool checked){
        updateServer->request(method,path,body,[this,checked,path,method](QJsonObject data,QString issue){
            if(!issue.isEmpty()){serverDone(issue);return;}
            const auto phase=data["phase"].toString();
            if(phase=="queued" || phase=="installing" || phase=="waiting" || phase=="unmanaged"){
                serverDone({});return;
            }
            if(phase=="checking"){
                if(serverDeadline.elapsed()>20000){serverDone(UiText::CouldNotCompleteTheOperation());return;}
                QTimer::singleShot(350,updateServer,[this]{serverStep("GET","/api/server-update",{},true);});
                return;
            }
            if(!checked){
                serverStep("POST","/api/server-update/check",{},true);
                return;
            }
            if(phase=="error"){serverDone(UiText::serverMessage(data["detail"].toString()));return;}
            const auto tag=data["available"].toString();
            if(data["canInstall"].toBool() && !tag.isEmpty() && !(method=="POST" && path=="/api/server-update")){
                serverStep("POST","/api/server-update",{{"tag",tag}},true);
                return;
            }
            serverDone({});
        });
    }
public:
    QString helper()const{return QCoreApplication::applicationDirPath()+"/update-desktop.ps1";}
    void install() {
        if(working||asset.isEmpty())return;
        attempts=0;download();
    }
private:
    void download() {
        auto temp=std::make_shared<QTemporaryDir>();
        if(!temp->isValid()){finish(UiText::Id::CouldNotCreateTheUpdateDirectory);return;}
        auto archive=std::make_shared<QFile>(temp->filePath("client.zip"));
        if(!archive->open(QIODevice::WriteOnly)){finish(UiText::Id::CouldNotCreateTheUpdateFile);return;}
        working=true;downloading=true;received=0;++attempts;message=UiText::Id::DownloadingVersion;refresh();
        QNetworkRequest request(asset);request.setTransferTimeout(60000);
        auto* reply=network.get(request);
        reply->setReadBufferSize(256*1024);
        auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
        auto writeFailed=std::make_shared<bool>(false);
        auto collect=[this,archive,hash,reply,writeFailed]{
            const auto data=reply->readAll();
            if(archive->pos()+data.size()>expected || archive->write(data)!=data.size()){*writeFailed=true;reply->abort();return;}
            hash->addData(data);
            received=archive->pos();
        };
        connect(reply,&QIODevice::readyRead,this,collect);
        connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64,qint64){refresh();});
        auto* deadline=new QTimer(reply);deadline->setSingleShot(true);deadline->start(5*60*1000);
        connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
        connect(reply,&QNetworkReply::finished,this,[this,temp,archive,hash,reply,collect,writeFailed]{
            reply->deleteLater();
            if(reply->error()!=QNetworkReply::NoError){
                archive->close();
                const auto error=reply->error();
                const bool transient=error==QNetworkReply::TimeoutError || error==QNetworkReply::RemoteHostClosedError || error==QNetworkReply::TemporaryNetworkFailureError;
                if(!*writeFailed && transient && attempts<3){message=UiText::Id::UpdateRetrying;refresh();QTimer::singleShot(1500,this,[this]{download();});return;}
                finish(*writeFailed?UiText::Id::CouldNotCreateTheUpdateFile:UiText::Id::DownloadInterruptedTryAgain);return;
            }
            message=UiText::Id::UpdateVerifying;refresh();
            collect();const bool valid=archive->pos()==expected && archive->flush() && hash->result().toHex()==digest.toLatin1();
            archive->close();
            if(!valid){finish(UiText::Id::TheReleaseChecksumDoesNotMatch);return;}
            const auto script=temp->filePath("update-desktop.ps1");
            if(!QFile::copy(helper(),script)){finish(UiText::Id::CouldNotPrepareTheInstaller);return;}
            const auto shell=qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0/powershell.exe";
            QStringList args={"-NoLogo","-NoProfile","-NonInteractive","-WindowStyle","Hidden","-ExecutionPolicy","RemoteSigned","-File",script,
                "-InstallDir",QCoreApplication::applicationDirPath(),"-ProfileDir",profileDir,
                "-ParentId",QString::number(QCoreApplication::applicationPid()),"-Version",version,"-Digest",digest,"-ReadyFile",temp->filePath("ready")};
            QProcess installer;
            installer.setProgram(shell);
            installer.setArguments(args);
            installer.setWorkingDirectory(temp->path());
            auto environment=QProcessEnvironment::systemEnvironment();
            // A PowerShell 7 parent can export a module path incompatible with Windows PowerShell 5.
            environment.remove("PSModulePath");
            environment.insert("NIMBO_UPDATE_LANGUAGE",UiText::locale().language()==QLocale::Russian?"ru":"en");
            if(restartView)environment.insert("NIMBO_UPDATE_VIEW",QString::fromUtf8(QJsonDocument(restartView()).toJson(QJsonDocument::Compact)));
            installer.setProcessEnvironment(environment);
            if(!installer.startDetached()){finish(UiText::Id::CouldNotStartTheInstaller);return;}
            temp->setAutoRemove(false);
            message=UiText::Id::UpdatePreparingRestart;refresh();
            auto* ready=new QTimer(this);ready->setInterval(100);
            auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
            connect(ready,&QTimer::timeout,this,[this,temp,ready,elapsed]{
                if(QFile::exists(temp->filePath("ready"))){
                    ready->stop();ready->deleteLater();
                    if(restartApplication)restartApplication();else QCoreApplication::quit();
                }else if(QFile::exists(temp->filePath("failed")) || elapsed->elapsed()>90000){
                    ready->stop();ready->deleteLater();finish(UiText::Id::CouldNotStartTheInstaller);
                }
            });ready->start();
        });
    }
};
