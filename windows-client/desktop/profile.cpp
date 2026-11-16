#include "profile.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <stdexcept>

namespace DesktopProfile {
namespace {
void fail() { throw std::runtime_error("invalid profile or password"); }
void check(NTSTATUS status) { if (status < 0) fail(); }
PUCHAR bytes(QByteArray& value) { return reinterpret_cast<PUCHAR>(value.data()); }
QByteArray randomBytes(int size) {
    QByteArray data(size, Qt::Uninitialized);
    check(BCryptGenRandom(nullptr, bytes(data), size, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    return data;
}
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    Algorithm(LPCWSTR name, ULONG flags = 0) { check(BCryptOpenAlgorithmProvider(&handle, name, nullptr, flags)); }
    ~Algorithm() { BCryptCloseAlgorithmProvider(handle, 0); }
};
struct AesKey {
    BCRYPT_KEY_HANDLE handle{};
    ~AesKey() { if (handle) BCryptDestroyKey(handle); }
};
QByteArray crypt(bool encrypt, QByteArray input, QByteArray salt, QByteArray iv, const QString& password) {
    if (password.size() < 6 || password.size() > 256 || salt.size() != 16 || iv.size() != 12 ||
        input.size() > 200016 || (!encrypt && input.size() <= 16)) fail();
    Algorithm sha(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG), aes(BCRYPT_AES_ALGORITHM);
    auto utf8 = password.toUtf8();
    QByteArray secret(32, Qt::Uninitialized);
    auto derived = BCryptDeriveKeyPBKDF2(sha.handle, bytes(utf8), utf8.size(), bytes(salt), 16, 600000, bytes(secret), 32, 0);
    SecureZeroMemory(utf8.data(), utf8.size());
    check(derived);
    check(BCryptSetProperty(aes.handle, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
    AesKey key;
    auto generated = BCryptGenerateSymmetricKey(aes.handle, &key.handle, nullptr, 0, bytes(secret), 32, 0);
    SecureZeroMemory(secret.data(), secret.size());
    check(generated);
    QByteArray tag = encrypt ? QByteArray(16, 0) : input.right(16);
    if (!encrypt) input.chop(16);
    QByteArray aad("nimbo-encrypted-profile:v1:PBKDF2-SHA256:600000:AES-256-GCM");
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = bytes(iv); info.cbNonce = iv.size();
    info.pbAuthData = bytes(aad); info.cbAuthData = aad.size();
    info.pbTag = bytes(tag); info.cbTag = tag.size();
    QByteArray result(input.size(), Qt::Uninitialized);
    ULONG written{};
    auto operation = encrypt ? BCryptEncrypt : BCryptDecrypt;
    check(operation(key.handle, bytes(input), input.size(), &info, nullptr, 0, bytes(result), result.size(), &written, 0));
    result.resize(written);
    if (encrypt) result.append(tag);
    return result;
}
}
QByteArray randomKey() { return randomBytes(32).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals); }
QUrl origin(QString address) {
    address = address.trimmed();
    if (!address.contains("://")) address.prepend("https://");
    QUrl u(address, QUrl::StrictMode);
    const bool local = u.host() == "localhost" || u.host() == "127.0.0.1" || u.host() == "::1";
    if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() || u.hasQuery() || u.hasFragment() ||
        (!u.path().isEmpty() && u.path() != "/") || (u.scheme() != "https" && !(local && u.scheme() == "http"))) return {};
    u.setPath({});
    if ((u.scheme() == "https" && u.port() == 443) || (u.scheme() == "http" && u.port() == 80)) u.setPort(-1);
    return u;
}
QJsonObject validate(const QByteArray& json) {
    const auto p = QJsonDocument::fromJson(json).object();
    if (json.size() > 200000 || p["format"] != "nimbo-profile" || p["version"].toInt() != 1 ||
        p["name"].toString().trimmed().isEmpty() || !p["servers"].isArray() || p["servers"].toArray().size() > 64) fail();
    QSet<QUrl> seen;
    static const QRegularExpression key("^[A-Za-z0-9_-]{43}$");
    for (auto item : p["servers"].toArray()) {
        auto server = item.toObject();
        const auto address = origin(server["url"].toString());
        if (address.isEmpty() || seen.contains(address) || !key.match(server["profileKey"].toString()).hasMatch()) fail();
        seen.insert(address);
    }
    return p;
}
struct EncryptedFields { QByteArray data, salt, iv; };
static EncryptedFields encryptedFields(const QByteArray& file) {
    if (file.size() > 300000) fail();
    const auto p = QJsonDocument::fromJson(file).object();
    if (p["format"] != "nimbo-encrypted-profile" || p["version"].toInt() != 1 || p["iterations"].toInt() != 600000 ||
        p["cipher"] != "AES-256-GCM" || p["kdf"] != "PBKDF2-SHA256") fail();
    auto decode = [&](const char* field) { return QByteArray::fromBase64(p[field].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors); };
    EncryptedFields fields{decode("data"), decode("salt"), decode("iv")};
    if(fields.salt.size()!=16 || fields.iv.size()!=12 || fields.data.size()<17 || fields.data.size()>200016) fail();
    return fields;
}
void inspectEncrypted(const QByteArray& file) { encryptedFields(file); }
QJsonObject open(const QByteArray& file, const QString& password) {
    if(QJsonDocument::fromJson(file).object()["format"]=="nimbo-profile") return validate(file);
    const auto fields=encryptedFields(file);
    return validate(crypt(false, fields.data, fields.salt, fields.iv, password));
}
QByteArray seal(const QJsonObject& profile, const QString& password) {
    const auto plain = QJsonDocument(profile).toJson(QJsonDocument::Compact);
    validate(plain);
    auto salt = randomBytes(16), iv = randomBytes(12);
    QJsonObject p{{"format","nimbo-encrypted-profile"},{"version",1},{"iterations",600000},{"cipher","AES-256-GCM"},
                  {"kdf","PBKDF2-SHA256"},{"salt",QString::fromLatin1(salt.toBase64())},{"iv",QString::fromLatin1(iv.toBase64())},
                  {"data",QString::fromLatin1(crypt(true, plain, salt, iv, password).toBase64())}};
    return QJsonDocument(p).toJson(QJsonDocument::Compact);
}
QByteArray localCrypt(QByteArray data, bool encrypt) {
    DATA_BLOB in{DWORD(data.size()),reinterpret_cast<BYTE*>(data.data())}, out{};
    BOOL ok = encrypt ? CryptProtectData(&in, L"Nimbo", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                      : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) fail();
    QByteArray result(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData);
    return result;
}
QByteArray protect(const QByteArray& plain) { return localCrypt(plain,true); }
QByteArray unprotect(const QByteArray& cipher) { return localCrypt(cipher,false); }
}
