// vim: set tabstop=4 shiftwidth=4 noexpandtab:
/*
Gwenview: an image viewer
Copyright 2008 Aurélien Gâteau <aurelien.gateau@free.fr>

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
#include "gvcore.h"

// Qt
#include <QApplication>
#include <QFuture>
#include <QFutureWatcher>
#include <QList>
#include <QProgressDialog>
#include <QtConcurrentMap>
#include <QWidget>

// KDE
#include <kfiledialog.h>
#include <kimageio.h>
#include <kio/netaccess.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kurl.h>

// Local
#include <lib/document/documentfactory.h>
#include <lib/gwenviewconfig.h>
#include <lib/mimetypeutils.h>
#include <lib/transformimageoperation.h>

static const int MAX_RECENT_FOLDER = 20;

namespace Gwenview {


struct GvCorePrivate {
	QWidget* mParent;
};


GvCore::GvCore(QWidget* parent)
: QObject(parent)
, d(new GvCorePrivate) {
	d->mParent = parent;
}


GvCore::~GvCore() {
	delete d;
}


void GvCore::addUrlToRecentFolders(const KUrl& _url) {
	KUrl url(_url);
	url.cleanPath();
	url.adjustPath(KUrl::RemoveTrailingSlash);
	QString urlString = url.url();

	QStringList list = GwenviewConfig::recentFolders();
	int index = list.indexOf(urlString);

	if (index == 0) {
		// Nothing to do, it's already the first recent folder.
		return;
	} else if (index != -1) {
		// Remove it from the list. This way it will get inserted at the
		// beginning.
		list.removeAt(index);
	}

	list.insert(0, urlString);
	while (list.size() > MAX_RECENT_FOLDER) {
		list.removeLast();
	}

	GwenviewConfig::setRecentFolders(list);
}


struct SaveAllHelper {
	typedef QPair<KUrl, QString> ErrorListItem;
	typedef QList<ErrorListItem> ErrorList;

	SaveAllHelper(ErrorList* errorList)
	: mErrorList(errorList) {}

	void operator()(const KUrl& url) {
		Document::Ptr doc = DocumentFactory::instance()->load(url);
		if (!doc->save(url, doc->format())) {
			*mErrorList << qMakePair(url, doc->errorString());
		}
	}

	ErrorList* mErrorList;
};

void GvCore::saveAll() {
	SaveAllHelper::ErrorList errorList;
	SaveAllHelper helper(&errorList);

	QList<KUrl> lst = DocumentFactory::instance()->modifiedDocumentList();

	QProgressDialog progress(d->mParent);
	progress.setLabelText(i18nc("@info:progress saving all image changes", "Saving..."));
	progress.setCancelButtonText(i18n("&Stop"));
	progress.setMinimum(0);
	progress.setMaximum(lst.size());
	progress.setWindowModality(Qt::WindowModal);

	QFuture<void> future = QtConcurrent::map(lst, helper);

	QFutureWatcher<void> watcher;
	watcher.setFuture(future);
	connect(&watcher, SIGNAL(progressValueChanged(int)),
		&progress, SLOT(setValue(int)) );

	connect(&watcher, SIGNAL(finished()),
		&progress, SLOT(close()) );

	connect(&progress, SIGNAL(canceled()),
		&watcher, SLOT(cancel()) );

	progress.exec();
	watcher.waitForFinished();

	if (errorList.count() > 0) {
		QString msg = i18ncp("@info", "One document could not be saved:", "%1 documents could not be saved:", errorList.count());
		msg += "<ul>";
		Q_FOREACH(const SaveAllHelper::ErrorListItem& item, errorList) {
			const KUrl& url = item.first;
			QString name = url.fileName().isEmpty() ? url.pathOrUrl() : url.fileName();
			msg += "<li>"
				+ i18nc("@info %1 is the name of the document which failed to save, %2 is the reason for the failure",
					"<filename>%1</filename>: %2", name, item.second)
				+ "</li>";
		}
		msg += "</ul>";
		KMessageBox::sorry(d->mParent, msg);
	}
}


void GvCore::save(const KUrl& url) {
	Document::Ptr doc = DocumentFactory::instance()->load(url);
	doc->loadFullImage();
	doc->waitUntilLoaded();
	QByteArray format = doc->format();
	QStringList availableTypes = KImageIO::types(KImageIO::Writing);
	if (availableTypes.contains(QString(format))) {
		if (!doc->save(url, format)) {
			QString name = url.fileName().isEmpty() ? url.pathOrUrl() : url.fileName();
			QString msg = i18nc("@info", "<b>Saving <filename>%1</filename> failed:</b><br>%2",
				name, doc->errorString());
			KMessageBox::sorry(d->mParent, msg);
		}
		return;
	}

	// We don't know how to save in 'format', ask the user for a format we can
	// write to.
	KGuiItem saveUsingAnotherFormat = KStandardGuiItem::saveAs();
	saveUsingAnotherFormat.setText(i18n("Save using another format"));
	int result = KMessageBox::warningContinueCancel(
		d->mParent,
		i18n("Gwenview cannot save images in '%1' format.", QString(format)),
		QString() /* caption */,
		saveUsingAnotherFormat
		);
	if (result == KMessageBox::Continue) {
		saveAs(url);
	}
}


void GvCore::saveAs(const KUrl& url) {
	KFileDialog dialog(url, QString(), d->mParent);
	dialog.setOperationMode(KFileDialog::Saving);

	// Init mime filter
	QString mimeType = MimeTypeUtils::urlMimeType(url);
	QStringList availableMimeTypes = KImageIO::mimeTypes(KImageIO::Writing);
	dialog.setMimeFilter(availableMimeTypes, mimeType);

	// Show dialog
	if (!dialog.exec()) {
		return;
	}

	KUrl saveAsUrl = dialog.selectedUrl();

	// Check for overwrite
	if (KIO::NetAccess::exists(saveAsUrl, KIO::NetAccess::DestinationSide, d->mParent)) {
		int answer = KMessageBox::warningContinueCancel(
			d->mParent,
			i18nc("@info",
				"A file named <filename>%1</filename> already exists.\n"
				"Are you sure you want to overwrite it?",
				saveAsUrl.fileName()),
			QString(),
			KStandardGuiItem::overwrite());
		if (answer == KMessageBox::Cancel) {
			return;
		}
	}

	// Start save
	mimeType = dialog.currentMimeFilter();
	QStringList typeList = KImageIO::typeForMime(mimeType);
	Q_ASSERT(typeList.count() > 0);
	Document::Ptr doc = DocumentFactory::instance()->load(url);
	doc->save(saveAsUrl, typeList[0].toAscii());
}


static void applyTransform(const KUrl& url, Orientation orientation) {
	if (!GvCore::ensureDocumentIsEditable(url)) {
		return;
	}
	TransformImageOperation* op = new TransformImageOperation(orientation);
	Document::Ptr doc = DocumentFactory::instance()->load(url);
	doc->loadFullImage();
	doc->waitUntilLoaded();
	op->setDocument(doc);
	doc->undoStack()->push(op);
}


void GvCore::rotateLeft(const KUrl& url) {
	applyTransform(url, ROT_270);
}


void GvCore::rotateRight(const KUrl& url) {
	applyTransform(url, ROT_90);
}


bool GvCore::ensureDocumentIsEditable(const KUrl& url) {
	Document::Ptr doc = DocumentFactory::instance()->load(url);
	if (doc->isEditable()) {
		return true;
	}

	KMessageBox::sorry(
		QApplication::activeWindow(),
		i18nc("@info", "Gwenview cannot edit this kind of image.")
		);
	return false;
}


} // namespace
