// vim: set tabstop=4 shiftwidth=4 noexpandtab:
/*
Gwenview: an image viewer
Copyright 2007 Aurélien Gâteau <aurelien.gateau@free.fr>

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/
#ifndef LOADINGDOCUMENTIMPL_H
#define LOADINGDOCUMENTIMPL_H

// Qt

// KDE

// Local
#include <lib/document/abstractdocumentimpl.h>

class KJob;
namespace KIO { class Job; }

namespace Gwenview {


class LoadingDocumentImplPrivate;
class LoadingDocumentImpl : public AbstractDocumentImpl {
	Q_OBJECT
public:
	LoadingDocumentImpl(Document*, Document::LoadType);
	~LoadingDocumentImpl();

	virtual void init();
	virtual bool isMetaDataLoaded() const;
	virtual Document::LoadingState loadingState() const;
	virtual Document::SaveResult save(const KUrl&, const QByteArray& format);
	virtual void setImage(const QImage&);

	void finishLoading();

private Q_SLOTS:
	void slotMetaDataLoaded();
	void slotImageLoaded();
	void slotDataReceived(KIO::Job*, const QByteArray&);
	void slotTransferFinished(KJob*);

private:
	LoadingDocumentImplPrivate* const d;
	friend class LoadingDocumentImplPrivate;
};


} // namespace

#endif /* LOADINGDOCUMENTIMPL_H */
