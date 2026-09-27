/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QString>
#include <QStringList>

// Persists favorite station IDs via QSettings, replacing the Haiku build's
// plain-text ~/config/settings/SuperMusicThingy/favorites.txt file.
class FavoritesManager {
public:
    FavoritesManager();

    const QStringList& favoriteIds() const { return m_ids; }

    bool isFavorite(const QString& channelId) const;
    void addFavorite(const QString& channelId);
    void removeFavorite(const QString& channelId);

    // Returns an empty string if there are no favorites.
    QString randomFavoriteId() const;

private:
    void load();
    void save();

    QStringList m_ids;
};
