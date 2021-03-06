/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "mirall/owncloudinfo.h"
#include "mirall/mirallconfigfile.h"
#include "mirall/theme.h"
#include "mirall/utility.h"
#include "mirall/logger.h"

#include <QtCore>
#include <QtGui>
#include <QAuthenticator>

#if QT46_IMPL
#include <QHttp>
#endif

#define DEFAULT_CONNECTION QLatin1String("default");
static const char WEBDAV_PATH[] = "remote.php/webdav/";

namespace Mirall
{

class oCCookieJar : public QNetworkCookieJar
{
public:
    QList<QNetworkCookie> cookiesForUrl ( const QUrl & url ) const {
        QList<QNetworkCookie> list;
        return list;
    }

};


ownCloudInfo *ownCloudInfo::_instance = 0;

ownCloudInfo* ownCloudInfo::instance()
{
  static QMutex mutex;
  if (!_instance)
  {
    mutex.lock();

    if (!_instance) {
      _instance = new ownCloudInfo;
    }
    mutex.unlock();
  }

  return _instance;
}

ownCloudInfo::ownCloudInfo() :
    QObject(0),
    _manager(0)
{
    _connection = Theme::instance()->appName();
    connect(this, SIGNAL(guiLog(QString,QString)),
            Logger::instance(), SIGNAL(guiLog(QString,QString)));
    setNetworkAccessManager( new QNetworkAccessManager( this ) );

}

void ownCloudInfo::setNetworkAccessManager( QNetworkAccessManager* qnam )
{
    delete _manager;
    qnam->setParent( this );
    _manager = qnam;

    MirallConfigFile cfg( _configHandle );
    QSslSocket::addDefaultCaCertificates(QSslCertificate::fromData(cfg.caCerts()));

    connect( _manager, SIGNAL( sslErrors(QNetworkReply*, QList<QSslError>)),
             this, SIGNAL(sslFailed(QNetworkReply*, QList<QSslError>)) );

    // The authenticationRequired signal is not handled because the creds are set
    // in the request header.
#if 0
    connect( _manager, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)),
             this, SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)));
#endif

    // no cookie jar so far.
    _manager->setCookieJar(new oCCookieJar);

    _certsUntrusted = false;

}

ownCloudInfo::~ownCloudInfo()
{
}

void ownCloudInfo::setCustomConfigHandle( const QString& handle )
{
    _configHandle = handle;
    _authAttempts = 0; // allow a couple of tries again.
    resetSSLUntrust();
}

bool ownCloudInfo::isConfigured()
{
    MirallConfigFile cfgFile( _configHandle );
    return cfgFile.connectionExists( _connection );
}

QNetworkReply *ownCloudInfo::checkInstallation()
{
    _redirectCount = 0;
    MirallConfigFile cfgFile(  _configHandle );
    QUrl url ( cfgFile.ownCloudUrl( _connection ) +  QLatin1String("status.php") );
    /* No authentication required for this. */
    return getRequest(url);
}

QNetworkReply* ownCloudInfo::getWebDAVPath( const QString& path )
{
    _redirectCount = 0;
    QUrl url ( webdavUrl( _connection ) +  path );
    QNetworkReply *reply = getRequest(url);
    _directories[reply] = path;
    return reply;
}

QNetworkReply* ownCloudInfo::getRequest( const QUrl& url )
{
    qDebug() << "Get Request to " << url;

    QNetworkRequest request;
    request.setUrl( url );
    setupHeaders( request, 0 );

    QNetworkReply *reply = _manager->get( request );
    connect( reply, SIGNAL(finished()), SLOT(slotReplyFinished()));

    if( !_configHandle.isEmpty() ) {
        qDebug() << "Setting config handle " << _configHandle;
        _configHandleMap[reply] = _configHandle;
    }

    connect( reply, SIGNAL( error(QNetworkReply::NetworkError )),
             this, SLOT(slotError( QNetworkReply::NetworkError )));
    return reply;
}

#if QT46_IMPL
QNetworkReply* ownCloudInfo::mkdirRequest( const QString& dir )
{
    qDebug() << "OCInfo Making dir " << dir;

    MirallConfigFile cfgFile( _configHandle );
    QUrl url = QUrl( webdavUrl(_connection) + dir );
    QHttp::ConnectionMode conMode = QHttp::ConnectionModeHttp;
    if (url.scheme() == "https")
        conMode = QHttp::ConnectionModeHttps;

    QHttp* qhttp = new QHttp(QString(url.encodedHost()), conMode, 0, this);

    connect(qhttp, SIGNAL(requestStarted(int)), this,SLOT(qhttpRequestStarted(int)));
    connect(qhttp, SIGNAL(requestFinished(int, bool)), this,SLOT(qhttpRequestFinished(int,bool)));
    connect(qhttp, SIGNAL(responseHeaderReceived(QHttpResponseHeader)), this, SLOT(qhttpResponseHeaderReceived(QHttpResponseHeader)));
    //connect(qhttp, SIGNAL(authenticationRequired(QString,quint16,QAuthenticator*)), this, SLOT(qhttpAuthenticationRequired(QString,quint16,QAuthenticator*)));
    QHttpRequestHeader header("MKCOL", QString(url.encodedPath()), 1,1);   /* header */
    header.setValue("Host", QString(url.encodedHost()));
    header.setValue("User-Agent", Utility::userAgentString());
    header.setValue("Accept-Charset", "ISO-8859-1,utf-8;q=0.7,*;q=0.7");
    header.setValue("Accept-Language", "it,de-de;q=0.8,it-it;q=0.6,en-us;q=0.4,en;q=0.2");
    header.setValue("Connection", "keep-alive");
    header.setContentType("application/x-www-form-urlencoded"); //important
    header.setContentLength(0);

    QString con = _configHandle;
    if( con.isEmpty() ) con = DEFAULT_CONNECTION;
    if( _credentials.contains(con)) {
        oCICredentials creds = _credentials.value(con);
        QString concatenated = creds.user + QLatin1Char(':') + creds.passwd;
        const QString b(QLatin1String("Basic "));
        QByteArray data = b.toLocal8Bit() + concatenated.toLocal8Bit().toBase64();
        header.setValue("Authorization", data);

        qhttp->setUser( creds.user, creds.passwd );
    }

    int david = qhttp->request(header,0,0);
    //////////////// connect(davinfo, SIGNAL(dataSendProgress(int,int)), this, SLOT(SendStatus(int, int)));
    /////////////////connect(davinfo, SIGNAL(done(bool)), this,SLOT(DavWake(bool)));
    //connect(_http, SIGNAL(requestFinished(int, bool)), this,SLOT(qhttpRequestFinished(int,bool)));
    ///////////connect(davinfo, SIGNAL(responseHeaderReceived(constQHttpResponseHeader &)), this, SLOT(RegisterBackHeader(constQHttpResponseHeader &)));

    return NULL;
}

void ownCloudInfo::qhttpResponseHeaderReceived(const QHttpResponseHeader& header)
{
    qDebug() << "Resp:" << header.toString();
    if (header.statusCode() == 201)
        emit webdavColCreated( QNetworkReply::NoError );
    else
        qDebug() << "http request failed" << header.toString();
}

void ownCloudInfo::qhttpRequestStarted(int id)
{
    qDebug() << "QHttp based request started " << id;
}

void ownCloudInfo::qhttpRequestFinished(int id, bool success )
{
     qDebug() << "HIT!";
     QHttp* qhttp = qobject_cast<QHttp*>(sender());

     if( success ) {
         qDebug() << "QHttp based request successful";
     } else {
         qDebug() << "QHttp based request failed: " << qhttp->errorString();
     }
}
#else
QNetworkReply* ownCloudInfo::mkdirRequest( const QString& dir )
{
    qDebug() << "OCInfo Making dir " << dir;
    _authAttempts = 0;
    QNetworkRequest req;
    req.setUrl( QUrl( webdavUrl(_connection) + dir ) );
    QNetworkReply *reply = davRequest(QLatin1String("MKCOL"), req, 0);

    // remember the confighandle used for this request
    if( ! _configHandle.isEmpty() )
        qDebug() << "Setting config handle " << _configHandle;
        _configHandleMap[reply] = _configHandle;

    if( reply->error() != QNetworkReply::NoError ) {
        qDebug() << "mkdir request network error: " << reply->errorString();
    }

    connect( reply, SIGNAL(finished()), SLOT(slotMkdirFinished()) );
    connect( reply, SIGNAL( error(QNetworkReply::NetworkError )),
             this, SLOT(slotError(QNetworkReply::NetworkError )));
    return reply;
}

void ownCloudInfo::slotMkdirFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());

    if( ! reply ) {
        qDebug() << "ownCloudInfo: Reply empty!";
        return;
    }

    emit webdavColCreated( reply->error() );
    qDebug() << "mkdir slot hit with status: " << reply->error();
    if( _configHandleMap.contains( reply ) ) {
        _configHandleMap.remove( reply );
    }

    reply->deleteLater();
}
#endif

// FIXME: remove this later, once the new connection dialog has settled.
#if 0
void ownCloudInfo::slotAuthentication( QNetworkReply *reply, QAuthenticator *auth )
{
    if( !(auth && reply) ) return;
    QString configHandle;

    // an empty config handle is ok for the default config.
    if( _configHandleMap.contains(reply) ) {
        configHandle = _configHandleMap[reply];
        qDebug() << "Auth: Have a custom config handle: " << configHandle;
    }

    qDebug() << "Auth request to me and I am " << this;
    _authAttempts++;
    MirallConfigFile cfgFile( configHandle );
    qDebug() << "Authenticating request for " << reply->url();
    if( reply->url().toString().startsWith( cfgFile.ownCloudUrl( _connection, true )) ) {

        QString con = configHandle;
        if( con.isEmpty() ) con = DEFAULT_CONNECTION;
        if( _credentials.contains(con)) {
            oCICredentials creds = _credentials.value(con);

            auth->setUser( creds.user );
            auth->setPassword( creds.passwd );
        } else {
            qDebug() << "Unable to get Credentials, not set!";
            reply->close();
        }
    } else {
        qDebug() << "WRN: attempt to authenticate to different url - attempt " <<_authAttempts;
    }
    if( _authAttempts > 1) {
        qDebug() << "Too many attempts to authenticate. Stop request.";
        reply->close();
    }
}
#endif

QString ownCloudInfo::configHandle(QNetworkReply *reply)
{
    QString configHandle;
    if( _configHandleMap.contains(reply) ) {
        configHandle = _configHandleMap[reply];
    }
    return configHandle;
}

QList<QSslCertificate> ownCloudInfo::certificateChain() const
{
    QMutexLocker lock(const_cast<QMutex*>(&_certChainMutex));
    return _certificateChain;
}

//
// There have been problems with the finish-signal coming from the networkmanager.
// To avoid that, the reply-signals were connected and the data is taken from the
// sender() method.
//
void ownCloudInfo::slotReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    QSslConfiguration sslConfig = reply->sslConfiguration();
    if (!sslConfig.isNull()) {
        QMutexLocker lock(&_certChainMutex);
        _certificateChain = sslConfig.peerCertificateChain();
    }

    if( ! reply ) {
        qDebug() << "ownCloudInfo: Reply empty!";
        return;
    }

    // Detect redirect url
    QUrl possibleRedirUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    /* We'll deduct if the redirection is valid in the redirectUrl function */


    if (!possibleRedirUrl.isEmpty() && _redirectCount++  > 10) {
        // Are we in a redirect loop
        qDebug() << "Redirect loop while redirecting to" << possibleRedirUrl;
        possibleRedirUrl.clear();
    }

    if(!possibleRedirUrl.isEmpty()) {
        QString configHandle;

        qDebug() << "Redirected to " << possibleRedirUrl;

        // We'll do another request to the redirection url.
        // an empty config handle is ok for the default config.
        if( _configHandleMap.contains(reply) ) {
            configHandle = _configHandleMap[reply];
            qDebug() << "Redirect: Have a custom config handle: " << configHandle;
        }

        QString path = _directories[reply];
        if (path.isEmpty()) {
            path = QLatin1String("status.php");
        } else {
            path.prepend( QLatin1String(WEBDAV_PATH) );
        }
        qDebug() << "This path was redirected: " << path;

        QString newUrl = possibleRedirUrl.toString();
        if( !path.isEmpty() && newUrl.endsWith( path )) {
            // cut off the trailing path
            newUrl.chop( path.length() );
            _urlRedirectedTo = newUrl;
            qDebug() << "Updated url to" << newUrl;
            getRequest( possibleRedirUrl );
        } else {
            qDebug() << "WRN: Path is not part of the redirect URL. NO redirect.";
        }
        reply->deleteLater();
        _directories.remove(reply);
        _configHandleMap.remove(reply);
        return;
    }

    // TODO: check if this is always the correct encoding
    const QString version = QString::fromUtf8( reply->readAll() );
    const QString url = reply->url().toString();
    QString plainUrl(url);
    plainUrl.remove( QLatin1String("/status.php"));

    QString info( version );

    if( url.endsWith( QLatin1String("status.php")) ) {
        // it was a call to status.php
        if( reply->error() == QNetworkReply::NoError && info.isEmpty() ) {
            // This seems to be a bit strange behaviour of QNetworkAccessManager.
            // It calls the finised slot multiple times but only the first read wins.
            // That happend when the code connected the finished signal of the manager.
            // It did not happen when the code connected to the reply finish signal.
            qDebug() << "WRN: NetworkReply with not content but also no error! " << reply;
            reply->deleteLater();
            return;
        }
        qDebug() << "status.php returns: " << info << " " << reply->error() << " Reply: " << reply;
        if( info.contains(QLatin1String("installed"))
                && info.contains(QLatin1String("version"))
                && info.contains(QLatin1String("versionstring")) ) {
            info.remove(0,1); // remove first char which is a "{"
            info.remove(-1,1); // remove the last char which is a "}"
            QStringList li = info.split( QLatin1Char(',') );

            QString versionStr;
            QString version;
            QString edition;

            foreach ( const QString& infoString, li ) {
                QStringList touple = infoString.split( QLatin1Char(':'));
                QString key = touple[0];
                key.remove(QLatin1Char('"'));
                QString val = touple[1];
                val.remove(QLatin1Char('"'));

                if( key == QLatin1String("versionstring") ) {
                    // get the versionstring out.
                    versionStr = val;
                } else if( key == QLatin1String( "version") ) {
                    // get version out
                    version = val;
                } else if( key == QLatin1String( "edition") ) {
                    // get version out
                    edition = val;
                } else if(key == QLatin1String("installed")) {
		    // Silently ignoring "installed = true" information
		} else {
                    qDebug() << "Unknown info from ownCloud status.php: "<< key << "=" << val;
                }
            }
            emit ownCloudInfoFound( plainUrl, versionStr, version, edition );
        } else {
            qDebug() << "No proper answer on " << url;

            emit noOwncloudFound( reply );
        }
    } else {
        // it was a general GET request.
        QString dir(QLatin1String("unknown"));
        if( _directories.contains(reply) ) {
            dir = _directories[reply];
        }

        emit ownCloudDirExists( dir, reply );
    }
    reply->deleteLater();
    _directories.remove(reply);
    _configHandleMap.remove(reply);
}

void ownCloudInfo::resetSSLUntrust()
{
    _certsUntrusted = false;
}

void ownCloudInfo::setCertsUntrusted(bool donttrust)
{
    _certsUntrusted = donttrust;
}

bool ownCloudInfo::certsUntrusted()
{
    return _certsUntrusted;
}

void ownCloudInfo::slotError( QNetworkReply::NetworkError err)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());

    qDebug() << "ownCloudInfo Network Error"
             << err << ":" << reply->errorString();

    switch (err) {
    case QNetworkReply::ProxyConnectionRefusedError:
        emit guiLog(tr("Proxy Refused Connection "),
                    tr("The configured proxy has refused the connection. "
                       "Please check the proxy settings."));
        break;
    case QNetworkReply::ProxyConnectionClosedError:
        emit guiLog(tr("Proxy Closed Connection"),
                    tr("The configured proxy has closed the connection. "
                       "Please check the proxy settings."));
        break;
    case QNetworkReply::ProxyNotFoundError:
        emit guiLog(tr("Proxy Not Found"),
                    tr("The configured proxy could not be found. "
                       "Please check the proxy settings."));
        break;
    case QNetworkReply::ProxyAuthenticationRequiredError:
        emit guiLog(tr("Proxy Authentication Error"),
                    tr("The configured proxy requires login but the proxy credentials "
                       "are invalid. Please check the proxy settings."));
        break;
    case QNetworkReply::ProxyTimeoutError:
        emit guiLog(tr("Proxy Connection Timed Out"),
                    tr("The connection to the configured proxy has timed out."));
        break;
    default:
        break;
    }
}

void ownCloudInfo::setCredentials( const QString& user, const QString& passwd,
                                   const QString& configHandle )
{
    QString con( configHandle );
    if( configHandle.isEmpty() )
        con = DEFAULT_CONNECTION;

    if( _credentials.contains(con) ) {
        qDebug() << "Overwriting credentials for connection " << con;
    }

    oCICredentials creds;
    creds.user = user;
    creds.passwd = passwd;
    creds.connection = con;
    _credentials[con] = creds;
}

// ============================================================================
void ownCloudInfo::setupHeaders( QNetworkRequest & req, quint64 size )
{
    QUrl url( req.url() );
    qDebug() << "Setting up host header: " << url.host();
    req.setRawHeader( QByteArray("Host"), url.host().toUtf8() );
    req.setRawHeader( QByteArray("User-Agent"), Utility::userAgentString());

    QString con = _configHandle;
    if( con.isEmpty() ) con = DEFAULT_CONNECTION;
    if( _credentials.contains(con)) {
        oCICredentials creds = _credentials.value(con);
        QString concatenated = creds.user + QLatin1Char(':') + creds.passwd;
        const QString b(QLatin1String("Basic "));
        QByteArray data = b.toUtf8() + concatenated.toUtf8().toBase64();
        req.setRawHeader( QByteArray("Authorization"), data );
    }

    if (size) {
        req.setHeader( QNetworkRequest::ContentLengthHeader, size);
        req.setHeader( QNetworkRequest::ContentTypeHeader, QLatin1String("text/xml; charset=utf-8"));
    }
}

#if QT46_IMPL
#else
QNetworkReply* ownCloudInfo::davRequest(const QString& reqVerb,  QNetworkRequest& req, QByteArray *data)
{
    setupHeaders(req, quint64(data ? data->size() : 0));
    if( data ) {
        QBuffer iobuf( data );
        return _manager->sendCustomRequest(req, reqVerb.toUtf8(), &iobuf );
    } else {
        return _manager->sendCustomRequest(req, reqVerb.toUtf8(), 0 );
    }
}
#endif

QString ownCloudInfo::webdavUrl(const QString &connection)
{
    QString url;

    if (!_urlRedirectedTo.isEmpty()) {
        url = _urlRedirectedTo.toString();
    } else {
        MirallConfigFile cfgFile(_configHandle );
        url = cfgFile.ownCloudUrl( connection );
    }
    url.append( QLatin1String( WEBDAV_PATH ) );
    return url;
}

}
