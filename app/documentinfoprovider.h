// vim: set tabstop=4 shiftwidth=4 expandtab:
/*
Gwenview: an image viewer
Copyright 2010 Aurélien Gâteau <agateau@kde.org>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Cambridge, MA 02110-1301, USA.

*/
#ifndef DOCUMENTINFOPROVIDER_H
#define DOCUMENTINFOPROVIDER_H

// Qt

// KDE

// Local
#include <lib/thumbnailview/abstractdocumentinfoprovider.h>

namespace Gwenview
{

class SortedDirModel;

class DocumentInfoProvider : public AbstractDocumentInfoProvider
{
    Q_OBJECT
public:
    DocumentInfoProvider(SortedDirModel* model);

    virtual bool isBusy(const QUrl &url) Q_DECL_OVERRIDE;

    virtual bool isModified(const QUrl &url) Q_DECL_OVERRIDE;

    virtual void thumbnailForDocument(const QUrl &url, ThumbnailGroup::Enum group, QPixmap* outPix, QSize* outFullSize) const Q_DECL_OVERRIDE;

private Q_SLOTS:
    void emitBusyStateChanged(const QUrl&, bool);
    void emitDocumentChanged(const QUrl&);

private:
    SortedDirModel* mDirModel;
};

} // namespace

#endif /* DOCUMENTINFOPROVIDER_H */
