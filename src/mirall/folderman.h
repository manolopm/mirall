/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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


#ifndef FOLDERMAN_H
#define FOLDERMAN_H

#include <QObject>
#include <QQueue>

#include "mirall/folder.h"
#include "mirall/folderwatcher.h"
#include "mirall/syncfileitem.h"

class QSignalMapper;

namespace Mirall {

class SyncResult;

class FolderMan : public QObject
{
    Q_OBJECT
public:
    /** Helper class to ensure sync is always switched back on */
    class SyncDisabler
    {
    public:
        SyncDisabler(Mirall::FolderMan *man) : _man(man)
        {
            _man->setSyncEnabled(false);
        }
        ~SyncDisabler() { _man->setSyncEnabled(true); }
    private:
        FolderMan *_man;
    };

    explicit FolderMan(QObject *parent = 0);
    ~FolderMan();

    int setupFolders();

    Mirall::Folder::Map map();

    /**
      * Add a folder definition to the config
      * Params:
      * QString backend
      * QString alias
      * QString sourceFolder on local machine
      * QString targetPath on remote
      * bool    onlyThisLAN, currently unused.
      */
    void addFolderDefinition( const QString&, const QString&, const QString&, const QString&, bool );

    /**
      * return the folder by alias or NULL if no folder with the alias exists.
      */
    Folder *folder( const QString& );

    /**
      * return the last sync result by alias
      */
    SyncResult syncResult( const QString& );

    /**
      * creates a folder for a specific configuration, identified by alias.
      */
    Folder* setupFolderFromConfigFile(const QString & );

    /**
     * wipes all folder defintions. No way back!
     */
    void removeAllFolderDefinitions();

    /**
     * Removes csync journals from all folders.
     */
    void wipeAllJournals();

    /**
     * Ensures that a given directory does not contain a .csync_journal.
     *
     * @returns false if the journal could not be removed, false otherwise.
     */
    static bool ensureJournalGone(const QString &path);

    /**
     * Creates a new and empty local directory.
     */
    bool startFromScratch( const QString& );

    /**
     *  called whenever proxy configuration changes
     */
    void setProxy();

signals:
    /**
      * signal to indicate a folder named by alias has changed its sync state.
      * Get the state via the Folder Map or the syncResult and syncState methods.
      */
    void folderSyncStateChange( const QString & );

public slots:
    void slotRemoveFolder( const QString& );
    void slotEnableFolder( const QString&, bool );

    void slotFolderSyncStarted();
    void slotFolderSyncFinished( const SyncResult& );

    void slotReparseConfiguration();

    void terminateSyncProcess( const QString& alias = QString::null );

    /* delete all folder objects */
    int unloadAllFolders();

    // if enabled is set to false, no new folders will start to sync.
    // the current one will finish.
    void setSyncEnabled( bool );

    void slotScheduleAllFolders();

private slots:
    // slot to add a folder to the syncing queue
    void slotScheduleSync( const QString & );

    // slot to take the next folder from queue and start syncing.
    void slotScheduleFolderSync();

private:
    // finds all folder configuration files
    // and create the folders
    int setupKnownFolders();
    void terminateCurrentSync();
    QString getBackupName( const QString& ) const;

    // Escaping of the alias which is used in QSettings AND the file
    // system, thus need to be escaped.
    QString escapeAlias( const QString& ) const;
    QString unescapeAlias( const QString& ) const;

    void removeFolder( const QString& );

    FolderWatcher *_configFolderWatcher;
    Folder::Map    _folderMap;
    QString        _folderConfigPath;
    QSignalMapper *_folderChangeSignalMapper;
    QString        _currentSyncFolder;
    QStringList    _scheduleQueue;
    bool           _syncEnabled;
};

}
#endif // FOLDERMAN_H
