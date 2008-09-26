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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Cambridge, MA 02110-1301, USA.

*/
// Self
#include "fileoperations.h"

// KDE
#include <kdebug.h>
#include <kfiledialog.h>
#include <kfileitem.h>
#include <kinputdialog.h>
#include <kio/copyjob.h>
#include <kio/deletejob.h>
#include <kio/job.h>
#include <kio/jobuidelegate.h>
#include <klocale.h>

namespace Gwenview {

namespace FileOperations {


// Taken from KonqOperations::askDeleteConfirmation
// Forked because KonqOperations is in kdebase/apps/lib/konq, and kdegraphics
// can't depend on kdebase.
enum Operation { TRASH, DEL, COPY, MOVE, LINK, EMPTYTRASH, STAT, MKDIR, RESTORE, UNKNOWN };
enum ConfirmationType { DEFAULT_CONFIRMATION, SKIP_CONFIRMATION, FORCE_CONFIRMATION };
static bool askDeleteConfirmation( const KUrl::List & selectedUrls, Operation operation, ConfirmationType confirmation, QWidget* widget )
{
	KIO::JobUiDelegate::DeletionType deletionType = operation == DEL ? KIO::JobUiDelegate::Delete : KIO::JobUiDelegate::Trash;
	KIO::JobUiDelegate::ConfirmationType confirmationType = confirmation == FORCE_CONFIRMATION ? KIO::JobUiDelegate::ForceConfirmation : KIO::JobUiDelegate::DefaultConfirmation;
	KIO::JobUiDelegate uiDelegate;
	uiDelegate.setWindow(widget);
	return uiDelegate.askDeleteConfirmation(selectedUrls, deletionType, confirmationType);
}


static void copyMoveOrLink(Operation operation, const KUrl::List& urlList, QWidget* parent) {
	Q_ASSERT(urlList.count() > 0);

	KFileDialog dialog(
		KUrl("kfiledialog:///<copyMoveOrLink>"),
		QString() /* filter */,
		parent);
	switch (operation) {
	case COPY:
		dialog.setCaption(i18n("Copy To"));
		break;
	case MOVE:
		dialog.setCaption(i18n("Move To"));
		break;
	case LINK:
		dialog.setCaption(i18n("Link To"));
		break;
	default:
		Q_ASSERT(0);
	}
	dialog.setOperationMode(KFileDialog::Saving);
	if (urlList.count() == 1) {
		dialog.setMode(KFile::File);
		dialog.setSelection(urlList[0].fileName());
	} else {
		dialog.setMode(KFile::ExistingOnly | KFile::Directory);
	}
	if (!dialog.exec()) {
		return;
	}

	KUrl destUrl = dialog.selectedUrl();
	switch (operation) {
	case COPY:
		KIO::copy(urlList, destUrl);
		break;

	case MOVE:
		KIO::move(urlList, destUrl);
		break;

	case LINK:
		KIO::link(urlList, destUrl);
		break;

	default:
		Q_ASSERT(0);
	}
}


static void delOrTrash(Operation operation, const KUrl::List& urlList, QWidget* parent) {
	Q_ASSERT(urlList.count() > 0);

	if (!askDeleteConfirmation(urlList, operation, DEFAULT_CONFIRMATION, parent)) {
		return;
	}

	switch (operation) {
	case TRASH:
		KIO::trash(urlList);
		break;

	case DEL:
		KIO::del(urlList);
		break;

	default:
		kWarning() << "Unknown operation " << operation ;
		break;
	}
}

void copyTo(const KUrl::List& urlList, QWidget* parent) {
	copyMoveOrLink(COPY, urlList, parent);
}


void moveTo(const KUrl::List& urlList, QWidget* parent) {
	copyMoveOrLink(MOVE, urlList, parent);
}


void linkTo(const KUrl::List& urlList, QWidget* parent) {
	copyMoveOrLink(LINK, urlList, parent);
}


void trash(const KUrl::List& urlList, QWidget* parent) {
	delOrTrash(TRASH, urlList, parent);
}


void del(const KUrl::List& urlList, QWidget* parent) {
	delOrTrash(DEL, urlList, parent);
}


void createFolder(const KUrl& parentUrl, QWidget* parent) {
	bool ok;
	QString name = KInputDialog::getText(
		i18n("Create Folder"),
		i18n("Enter the name of the folder to create:"),
		QString(),
		&ok,
		parent);

	if (!ok) {
		return;
	}

	KUrl url = parentUrl;
	url.addPath(name);

	KIO::mkdir(url);
}


} // namespace

} // namespace
