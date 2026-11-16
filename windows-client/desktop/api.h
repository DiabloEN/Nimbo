#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QCache>
#include <functional>

class ServerApi final : public QObject {
public:
    using Callback = std::function<void(QJsonObject,QString)>;
    std::function<QString()> activeProfileKey;
    QUrl server;
    QString token;
    explicit ServerApi(QObject* parent = nullptr) : QObject(parent) {}
    void request(QByteArray method, QString path, QJsonObject body, Callback callback);
    QJsonObject cachedPreview(const QString& path);
    void rememberPreview(const QString& path,const QJsonObject& result);
    void bytes(QString path, int limit, std::function<void(QByteArray,QString)> callback);
    void upload(QString path, QStringList files, QJsonObject fields, Callback callback,std::function<void(int)> progress={});
    void cancelUpload(const QString& id);
    void download(QString path, QString destination, std::function<void(QString)> callback,qint64 limit=-1,QObject* owner=nullptr);
    // Account-scoped cache. A successful reconciliation unlocks reads after each reconnect.
    void syncMedia(const QString& root,const QString& member);
    void trimMedia();
    QString durationPath(const QString& route) const;
    QString posterPath(const QString& route) const;
    std::function<QStringList()> visibleMedia;
    std::function<void(QStringList)> mediaInvalidated;
    void reset();
private:
    QNetworkRequest headers(const QString& path) const;
    using Retry=std::function<void()>;
    void finishJson(QNetworkReply* reply, Callback callback,Retry retry={});
    bool renew(QNetworkReply* reply,Retry retry,Retry failure);
    void requestOnce(QByteArray method,QString path,QJsonObject body,Callback callback,bool retry);
    void uploadOnce(QString path,QStringList files,QJsonObject fields,Callback callback,std::function<void(int)> progress,bool retry);
    void bytesOnce(QString path,int limit,std::function<void(QByteArray,QString)> callback,bool retry);
    void downloadOnce(QString path,QString destination,std::function<void(QString)> callback,bool retry,qint64 limit,QPointer<QObject> owner);
    QList<std::function<void(bool)>> renewalWaiters;
    struct UploadState{bool cancelled=false;QPointer<QNetworkReply> reply;};
    QHash<QString,std::shared_ptr<UploadState>> uploads;
    QNetworkAccessManager network;
    QCache<QString,QTemporaryFile> previews{4096}; // KiB of session-local temporary files
    void waitForMediaChanges();
    void reconcileMedia(const QString& revision);
    QString mediaRoot,mediaDir,mediaCursor;
    bool cacheReady=false,checkingDisk=false;
    QList<std::function<void()>> deferredDownloads;
    void resumeDownloads();
    quint64 cacheEpoch=0;
    QMultiHash<QString,bool> pendingMedia;
    unsigned generation = 0;
};
