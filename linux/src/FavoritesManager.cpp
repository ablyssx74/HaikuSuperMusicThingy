/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "FavoritesManager.h"

#include <QSettings>
#include <cstdlib>

FavoritesManager::FavoritesManager()
{
    load();
}

void FavoritesManager::load()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "SuperMusicThingy", "favorites");
    m_ids = s.value("ids").toStringList();
}

void FavoritesManager::save()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "SuperMusicThingy", "favorites");
    s.setValue("ids", m_ids);
    s.sync();
}

bool FavoritesManager::isFavorite(const QString& channelId) const
{
    return m_ids.contains(channelId);
}

void FavoritesManager::addFavorite(const QString& channelId)
{
    if (channelId.isEmpty() || m_ids.contains(channelId))
        return;
    m_ids.append(channelId);
    save();
}

void FavoritesManager::removeFavorite(const QString& channelId)
{
    if (m_ids.removeAll(channelId) > 0)
        save();
}

QString FavoritesManager::randomFavoriteId() const
{
    if (m_ids.isEmpty())
        return QString();
    return m_ids.at(std::rand() % m_ids.size());
}
