/*
Gwenview: an image viewer
Copyright 2007 Aurélien Gâteau <agateau@kde.org>

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#include "document.moc"

// Qt
#include <QApplication>
#include <QImage>
#include <QQueue>
#include <QUndoStack>

// KDE
#include <kdebug.h>
#include <kfileitem.h>
#include <kurl.h>

// Local
#include "documentjob.h"
#include "emptydocumentimpl.h"
#include "imagemetainfomodel.h"
#include "loadingdocumentimpl.h"
#include "loadingjob.h"
#include "savejob.h"

namespace Gwenview {

struct DocumentPrivate {
	AbstractDocumentImpl* mImpl;
	KUrl mUrl;
	bool mKeepRawData;
	QQueue<DocumentJob*> mJobQueue;

	/**
	 * @defgroup imagedata should be reset in reload()
	 * @{
	 */
	QSize mSize;
	QImage mImage;
	QMap<int, QImage> mDownSampledImageMap;
	Exiv2::Image::AutoPtr mExiv2Image;
	MimeTypeUtils::Kind mKind;
	QByteArray mFormat;
	ImageMetaInfoModel mImageMetaInfoModel;
	QUndoStack mUndoStack;
	QString mErrorString;
	/** @} */

	void scheduleImageLoading(int invertedZoom) {
		LoadingDocumentImpl* impl = qobject_cast<LoadingDocumentImpl*>(mImpl);
		Q_ASSERT(impl);
		impl->loadImage(invertedZoom);
	}
};


qreal Document::maxDownSampledZoom() {
	return 0.5;
}


Document::Document(const KUrl& url)
: QObject()
, d(new DocumentPrivate) {
	d->mImpl = 0;
	d->mUrl = url;
	d->mKeepRawData = false;
	connect(&d->mUndoStack, SIGNAL(indexChanged(int)), SLOT(slotUndoIndexChanged()) );

	reload();
}


Document::~Document() {
	// We do not want undo stack to emit signals, forcing us to emit signals
	// ourself while we are being destroyed.
	disconnect(&d->mUndoStack, 0, this, 0);

	delete d->mImpl;
	delete d;
}


void Document::reload() {
	d->mSize = QSize();
	d->mImage = QImage();
	d->mDownSampledImageMap.clear();
	d->mExiv2Image.reset();
	d->mKind = MimeTypeUtils::KIND_UNKNOWN;
	d->mFormat = QByteArray();
	KFileItem fileItem(KFileItem::Unknown, KFileItem::Unknown, d->mUrl);
	d->mImageMetaInfoModel.setFileItem(fileItem);
	d->mUndoStack.clear();
	d->mErrorString.clear();

	switchToImpl(new LoadingDocumentImpl(this));
}


const QImage& Document::image() const {
	return d->mImage;
}


/**
 * invertedZoom is the biggest power of 2 for which zoom < 1/invertedZoom.
 * Example:
 * zoom = 0.4 == 1/2.5 => invertedZoom = 2 (1/2.5 < 1/2)
 * zoom = 0.2 == 1/5   => invertedZoom = 4 (1/5   < 1/4)
 */
inline int invertedZoomForZoom(qreal zoom) {
	int invertedZoom;
	for (invertedZoom = 1; zoom < 1./(invertedZoom*2); invertedZoom*=2) {}
	return invertedZoom;
}


const QImage& Document::downSampledImageForZoom(qreal zoom) const {
	static const QImage sNullImage;

	int invertedZoom = invertedZoomForZoom(zoom);
	if (invertedZoom == 1) {
		return d->mImage;
	}

	if (!d->mDownSampledImageMap.contains(invertedZoom)) {
		if (!d->mImage.isNull()) {
			// Special case: if we have the full image and the down sampled
			// image would be too small, return the original image.
			const QSize downSampledSize = d->mImage.size() / invertedZoom;
			if (downSampledSize.isEmpty()) {
				return d->mImage;
			}
		}
		return sNullImage;
	}

	return d->mDownSampledImageMap[invertedZoom];
}


Document::LoadingState Document::loadingState() const {
	return d->mImpl->loadingState();
}


void Document::switchToImpl(AbstractDocumentImpl* impl) {
	Q_ASSERT(impl);
	if (d->mImpl) {
		d->mImpl->deleteLater();
	}
	d->mImpl=impl;

	connect(d->mImpl, SIGNAL(metaInfoLoaded()),
		this, SLOT(emitMetaInfoLoaded()) );
	connect(d->mImpl, SIGNAL(loaded()),
		this, SLOT(emitLoaded()) );
	connect(d->mImpl, SIGNAL(loadingFailed()),
		this, SLOT(emitLoadingFailed()) );
	connect(d->mImpl, SIGNAL(imageRectUpdated(const QRect&)),
		this, SIGNAL(imageRectUpdated(const QRect&)) );
	connect(d->mImpl, SIGNAL(isAnimatedUpdated()),
		this, SIGNAL(isAnimatedUpdated()) );
	d->mImpl->init();
}


void Document::setImageInternal(const QImage& image) {
	d->mImage = image;
	d->mDownSampledImageMap.clear();

	// If we didn't get the image size before decoding the full image, set it
	// now
	setSize(d->mImage.size());
}


KUrl Document::url() const {
	return d->mUrl;
}


QByteArray Document::rawData() const {
	return d->mImpl->rawData();
}


bool Document::keepRawData() const {
	return d->mKeepRawData;
}


void Document::setKeepRawData(bool value) {
	d->mKeepRawData = value;
}


void Document::waitUntilLoaded() {
	startLoadingFullImage();
	while (true) {
		LoadingState state = loadingState();
		if (state == Loaded || state == LoadingFailed) {
			return;
		}
		qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
	}
}


DocumentJob* Document::save(const KUrl& url, const QByteArray& format) {
	waitUntilLoaded();
	DocumentJob* job = d->mImpl->save(url, format);
	job->setProperty("oldUrl", d->mUrl);
	job->setProperty("newUrl", url);
	if (!job) {
		kWarning() << "Implementation does not support saving!";
		return false;
	}
	connect(job, SIGNAL(result(KJob*)), SLOT(slotSaveResult(KJob*)));
	enqueueJob(job);
	return job;
}


void Document::slotSaveResult(KJob* job) {
	if (job->error()) {
		setErrorString(job->errorString());
	} else {
		d->mUndoStack.setClean();
		SaveJob* saveJob = static_cast<SaveJob*>(job);
		d->mUrl = saveJob->newUrl();
		saved(saveJob->oldUrl(), d->mUrl);
	}
}


QByteArray Document::format() const {
	return d->mFormat;
}


void Document::setFormat(const QByteArray& format) {
	d->mFormat = format;
	emit metaInfoUpdated();
}


MimeTypeUtils::Kind Document::kind() const {
	return d->mKind;
}


void Document::setKind(MimeTypeUtils::Kind kind) {
	d->mKind = kind;
	emit kindDetermined(d->mUrl);
}


QSize Document::size() const {
	return d->mSize;
}


bool Document::hasAlphaChannel() const {
	if (d->mImage.isNull()) {
		return false;
	} else {
		return d->mImage.hasAlphaChannel();
	}
}


int Document::memoryUsage() const {
	// FIXME: Take undo stack into account
	int usage = d->mImage.numBytes();
	usage += rawData().length();
	return usage;
}


void Document::setSize(const QSize& size) {
	if (size == d->mSize) {
		return;
	}
	d->mSize = size;
	d->mImageMetaInfoModel.setImageSize(size);
	emit metaInfoUpdated();
}


bool Document::isModified() const {
	return !d->mUndoStack.isClean();
}


AbstractDocumentEditor* Document::editor() {
	return d->mImpl->editor();
}


void Document::setExiv2Image(Exiv2::Image::AutoPtr image) {
	d->mExiv2Image = image;
	d->mImageMetaInfoModel.setExiv2Image(d->mExiv2Image.get());
	emit metaInfoUpdated();
}


void Document::setDownSampledImage(const QImage& image, int invertedZoom) {
	Q_ASSERT(!d->mDownSampledImageMap.contains(invertedZoom));
	d->mDownSampledImageMap[invertedZoom] = image;
	emit downSampledImageReady();
}


QString Document::errorString() const {
	return d->mErrorString;
}


void Document::setErrorString(const QString& string) {
	d->mErrorString = string;
}


ImageMetaInfoModel* Document::metaInfo() const {
	return &d->mImageMetaInfoModel;
}


void Document::startLoadingFullImage() {
	LoadingState state = loadingState();
	if (state <= MetaInfoLoaded) {
		// Schedule full image loading
		enqueueJob(new LoadingJob);
		d->scheduleImageLoading(1);
	} else if (state == Loaded) {
		return;
	} else if (state == LoadingFailed) {
		kWarning() << "Can't load full image: loading has already failed";
	}
}


bool Document::prepareDownSampledImageForZoom(qreal zoom) {
	if (zoom >= maxDownSampledZoom()) {
		kWarning() << "No need to call prepareDownSampledImageForZoom if zoom >= " << maxDownSampledZoom();
		return true;
	}

	int invertedZoom = invertedZoomForZoom(zoom);
	if (d->mDownSampledImageMap.contains(invertedZoom)) {
		return true;
	}

	if (loadingState() == Loaded) {
		// Resample image from the full one
		d->mDownSampledImageMap[invertedZoom] = d->mImage.scaled(d->mImage.size() / invertedZoom, Qt::KeepAspectRatio, Qt::FastTransformation);
		if (d->mDownSampledImageMap[invertedZoom].size().isEmpty()) {
			d->mDownSampledImageMap[invertedZoom] = d->mImage;
			return true;
		}

		return true;
	}

	// Schedule down sampled image loading
	d->scheduleImageLoading(invertedZoom);

	return false;
}


void Document::emitMetaInfoLoaded() {
	emit metaInfoLoaded(d->mUrl);
}


void Document::emitLoaded() {
	emit loaded(d->mUrl);
}


void Document::emitLoadingFailed() {
	emit loadingFailed(d->mUrl);
}


QUndoStack* Document::undoStack() const {
	return &d->mUndoStack;
}


void Document::slotUndoIndexChanged() {
	if (d->mUndoStack.isClean()) {
		// If user just undid all his changes this does not really correspond
		// to a save, but it's similar enough as far as Document users are
		// concerned
		saved(d->mUrl, d->mUrl);
	} else {
		modified(d->mUrl);
	}
}


bool Document::isEditable() const {
	return d->mImpl->isEditable();
}


bool Document::isAnimated() const {
	return d->mImpl->isAnimated();
}


void Document::startAnimation() {
	return d->mImpl->startAnimation();
}


void Document::stopAnimation() {
	return d->mImpl->stopAnimation();
}

void Document::enqueueJob(DocumentJob* task) {
	d->mJobQueue.enqueue(task);
	task->setDocument(Ptr(this));
	connect(task, SIGNAL(destroyed(QObject*)),
		SLOT(slotJobDestroyed(QObject*)));
	if (d->mJobQueue.size() == 1) {
		task->start();
		busyChanged(d->mUrl, true);
	}
}

void Document::slotJobDestroyed(QObject* job) {
	Q_ASSERT(!d->mJobQueue.isEmpty());
	if (d->mJobQueue.head() != job) {
		// Job was killed before it even got started, just remove it from the
		// queue and move along
		d->mJobQueue.removeAll(static_cast<DocumentJob*>(job));
		return;
	}
	d->mJobQueue.dequeue();
	if (d->mJobQueue.isEmpty()) {
		busyChanged(d->mUrl, false);
		allTasksDone();
	} else {
		d->mJobQueue.head()->start();
	}
}

bool Document::isBusy() const {
	return !d->mJobQueue.isEmpty();
}

} // namespace
