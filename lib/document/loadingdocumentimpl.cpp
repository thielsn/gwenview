// vim: set tabstop=4 shiftwidth=4 noexpandtab:
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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/
// Self
#include "loadingdocumentimpl.moc"

// STL
#include <memory>

// Qt
#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QFuture>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QtConcurrentRun>

// KDE
#include <kdebug.h>
#include <kio/job.h>
#include <kio/jobclasses.h>
#include <klocale.h>
#include <kmimetype.h>
#include <kprotocolinfo.h>
#include <kurl.h>

// Local
#include "animateddocumentloadedimpl.h"
#include "document.h"
#include "documentloadedimpl.h"
#include "emptydocumentimpl.h"
#include "exiv2imageloader.h"
#include "imageutils.h"
#include "jpegcontent.h"
#include "jpegdocumentloadedimpl.h"
#include "orientation.h"
#include "svgdocumentloadedimpl.h"
#include "urlutils.h"
#include "videodocumentloadedimpl.h"

namespace Gwenview {

#undef ENABLE_LOG
#undef LOG
//#define ENABLE_LOG
#ifdef ENABLE_LOG
#define LOG(x) kDebug() << x
#else
#define LOG(x) ;
#endif

const int HEADER_SIZE = 256;

struct LoadingDocumentImplPrivate {
	LoadingDocumentImpl* mImpl;
	QPointer<KIO::TransferJob> mTransferJob;
	QFuture<bool> mMetaInfoFuture;
	QFutureWatcher<bool> mMetaInfoFutureWatcher;
	QFuture<void> mImageDataFuture;
	QFutureWatcher<void> mImageDataFutureWatcher;

	// If != 0, this means we need to load an image at zoom =
	// 1/mImageDataInvertedZoom
	int mImageDataInvertedZoom;

	bool mMetaInfoLoaded;
	bool mAnimated;
	bool mDownSampledImageLoaded;
	QByteArray mData;
	QByteArray mFormat;
	QSize mImageSize;
	Exiv2::Image::AutoPtr mExiv2Image;
	std::auto_ptr<JpegContent> mJpegContent;
	QImage mImage;


	/**
	 * Determine kind of document and switch to an implementation if it is not
	 * necessary to download more data.
	 * @return true if switched to another implementation.
	 */
	bool determineKind() {
		QString mimeType;
		const KUrl& url = mImpl->document()->url();
		if (KProtocolInfo::determineMimetypeFromExtension(url.protocol())) {
			mimeType = KMimeType::findByNameAndContent(url.fileName(), mData)->name();
		} else {
			mimeType = KMimeType::findByContent(mData)->name();
		}
		MimeTypeUtils::Kind kind = MimeTypeUtils::mimeTypeKind(mimeType);
		LOG("mimeType:" << mimeType);
		LOG("kind:" << kind);
		mImpl->setDocumentKind(kind);

		switch (kind) {
		case MimeTypeUtils::KIND_RASTER_IMAGE:
		case MimeTypeUtils::KIND_SVG_IMAGE:
			return false;

		case MimeTypeUtils::KIND_VIDEO:
			mImpl->switchToImpl(new VideoDocumentLoadedImpl(mImpl->document()));
			return true;

		default:
			mImpl->setDocumentErrorString(
				i18nc("@info", "Gwenview cannot display documents of type %1.", mimeType)
				);
			emit mImpl->loadingFailed();
			mImpl->switchToImpl(new EmptyDocumentImpl(mImpl->document()));
			return true;
		}
	}


	void startLoading() {
		Q_ASSERT(!mMetaInfoLoaded);

		switch (mImpl->document()->kind()) {
		case MimeTypeUtils::KIND_RASTER_IMAGE:
			mMetaInfoFuture = QtConcurrent::run(this, &LoadingDocumentImplPrivate::loadMetaInfo);
			mMetaInfoFutureWatcher.setFuture(mMetaInfoFuture);
			break;

		case MimeTypeUtils::KIND_SVG_IMAGE:
			mImpl->switchToImpl(new SvgDocumentLoadedImpl(mImpl->document(), mData));
			break;

		case MimeTypeUtils::KIND_VIDEO:
			break;

		default:
			kWarning() << "We should not reach this point!";
			break;
		}
	}

	void startImageDataLoading() {
		LOG("");
		Q_ASSERT(mMetaInfoLoaded);
		Q_ASSERT(mImageDataInvertedZoom != 0);
		Q_ASSERT(!mImageDataFuture.isRunning());
		mImageDataFuture = QtConcurrent::run(this, &LoadingDocumentImplPrivate::loadImageData);
		mImageDataFutureWatcher.setFuture(mImageDataFuture);
	}

	bool loadMetaInfo() {
		QBuffer buffer;
		buffer.setBuffer(&mData);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer);
		mFormat = reader.format();
		if (mFormat.isEmpty()) {
			return false;
		}

		Exiv2ImageLoader loader;
		if (loader.load(mData)) {
			mExiv2Image = loader.popImage();
		}

		if (mFormat == "jpeg" && mExiv2Image.get()) {
			mJpegContent.reset(new JpegContent());
			if (!mJpegContent->loadFromData(mData, mExiv2Image.get())) {
				return false;
			}

			// Use the size from JpegContent, as its correctly transposed if the
			// image has been rotated
			mImageSize = mJpegContent->size();
		} else {
			mImageSize = reader.size();
		}
		LOG("mImageSize" << mImageSize);

		return true;
	}

	void loadImageData() {
		QBuffer buffer;
		buffer.setBuffer(&mData);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer);

		LOG("mImageDataInvertedZoom=" << mImageDataInvertedZoom);
		if (mImageSize.isValid()
			&& mImageDataInvertedZoom != 1
			&& reader.supportsOption(QImageIOHandler::ScaledSize)
			)
		{
			// Do not use mImageSize here: QImageReader needs a non-transposed
			// image size
			QSize size = reader.size() / mImageDataInvertedZoom;
			if (!size.isEmpty()) {
				LOG("Setting scaled size to" << size);
				reader.setScaledSize(size);
			} else {
				LOG("Not setting scaled size as it is empty" << size);
			}
		}

		bool ok = reader.read(&mImage);
		if (!ok) {
			LOG("QImageReader::read() failed");
			return;
		}

		if (mJpegContent.get()) {
			Gwenview::Orientation orientation = mJpegContent->orientation();
			QMatrix matrix = ImageUtils::transformMatrix(orientation);
			mImage = mImage.transformed(matrix);
		}

		if (reader.supportsAnimation()
			&& reader.nextImageDelay() > 0 // Assume delay == 0 <=> only one frame
		) {
			/*
			 * QImageReader is not really helpful to detect animated gif:
			 * - QImageReader::imageCount() returns 0
			 * - QImageReader::nextImageDelay() may return something > 0 if the
			 *   image consists of only one frame but includes a "Graphic
			 *   Control Extension" (usually only present if we have an
			 *   animation) (Bug #185523)
			 *
			 * Decoding the next frame is the only reliable way I found to
			 * detect an animated gif
			 */
			LOG("May be an animated image. delay:" << reader.nextImageDelay());
			QImage nextImage;
			if (reader.read(&nextImage)) {
				LOG("Really an animated image (more than one frame)");
				mAnimated = true;
			} else {
				kWarning() << mImpl->document()->url() << "is not really an animated image (only one frame)";
			}
		}
	}
};


LoadingDocumentImpl::LoadingDocumentImpl(Document* document)
: AbstractDocumentImpl(document)
, d(new LoadingDocumentImplPrivate) {
	d->mImpl = this;
	d->mMetaInfoLoaded = false;
	d->mAnimated = false;
	d->mDownSampledImageLoaded = false;
	d->mImageDataInvertedZoom = 0;

	connect(&d->mMetaInfoFutureWatcher, SIGNAL(finished()),
		SLOT(slotMetaInfoLoaded()) );

	connect(&d->mImageDataFutureWatcher, SIGNAL(finished()),
		SLOT(slotImageLoaded()) );
}


LoadingDocumentImpl::~LoadingDocumentImpl() {
	LOG("");
	// Disconnect watchers to make sure they do not trigger further work
	d->mMetaInfoFutureWatcher.disconnect();
	d->mImageDataFutureWatcher.disconnect();

	d->mMetaInfoFutureWatcher.waitForFinished();
	d->mImageDataFutureWatcher.waitForFinished();

	if (d->mTransferJob) {
		d->mTransferJob->kill();
	}
	delete d;
}

void LoadingDocumentImpl::init() {
	KUrl url = document()->url();

	if (UrlUtils::urlIsFastLocalFile(url)) {
		// Load file content directly
		QFile file(url.toLocalFile());
		if (!file.open(QIODevice::ReadOnly)) {
			setDocumentErrorString("Could not open file.");
			emit loadingFailed();
			switchToImpl(new EmptyDocumentImpl(document()));
			return;
		}
		d->mData = file.read(HEADER_SIZE);
		if (d->determineKind()) {
			return;
		}
		d->mData += file.readAll();
		d->startLoading();
	} else {
		// Transfer file via KIO
		d->mTransferJob = KIO::get(document()->url());
		connect(d->mTransferJob, SIGNAL(data(KIO::Job*, const QByteArray&)),
			SLOT(slotDataReceived(KIO::Job*, const QByteArray&)) );
		connect(d->mTransferJob, SIGNAL(result(KJob*)),
			SLOT(slotTransferFinished(KJob*)) );
		d->mTransferJob->start();
	}
}


void LoadingDocumentImpl::loadImage(int invertedZoom) {
	if (d->mImageDataInvertedZoom == invertedZoom) {
		LOG("Already loading an image at invertedZoom=" << invertedZoom);
		return;
	}
	d->mImageDataFutureWatcher.waitForFinished();
	d->mImageDataInvertedZoom = invertedZoom;

	if (d->mMetaInfoLoaded) {
		// Do not test on mMetaInfoFuture.isRunning() here: it might not have
		// started if we are downloading the image from a remote url
		d->startImageDataLoading();
	}
}


void LoadingDocumentImpl::slotDataReceived(KIO::Job* job, const QByteArray& chunk) {
	d->mData.append(chunk);
	if (document()->kind() == MimeTypeUtils::KIND_UNKNOWN && d->mData.length() >= HEADER_SIZE) {
		if (d->determineKind()) {
			job->kill();
			return;
		}
	}
}


void LoadingDocumentImpl::slotTransferFinished(KJob* job) {
	if (job->error()) {
		setDocumentErrorString(job->errorString());
		emit loadingFailed();
		switchToImpl(new EmptyDocumentImpl(document()));
		return;
	}
	d->startLoading();
}


bool LoadingDocumentImpl::isEditable() const {
	return d->mDownSampledImageLoaded;
}


Document::LoadingState LoadingDocumentImpl::loadingState() const {
	if (!document()->image().isNull()) {
		return Document::Loaded;
	} else if (d->mMetaInfoLoaded) {
		return Document::MetaInfoLoaded;
	} else if (document()->kind() != MimeTypeUtils::KIND_UNKNOWN) {
		return Document::KindDetermined;
	} else {
		return Document::Loading;
	}
}


void LoadingDocumentImpl::slotMetaInfoLoaded() {
	LOG("");
	Q_ASSERT(!d->mMetaInfoFuture.isRunning());
	if (!d->mMetaInfoFuture.result()) {
		setDocumentErrorString(
			i18nc("@info", "Loading meta information failed.")
			);
		emit loadingFailed();
		switchToImpl(new EmptyDocumentImpl(document()));
		return;
	}

	setDocumentFormat(d->mFormat);
	setDocumentImageSize(d->mImageSize);
	setDocumentExiv2Image(d->mExiv2Image);

	d->mMetaInfoLoaded = true;
	emit metaInfoLoaded();

	// Start image loading if necessary
	// We test if mImageDataFuture is not already running because code connected to
	// metaInfoLoaded() signal could have called loadImage()
	if (!d->mImageDataFuture.isRunning() && d->mImageDataInvertedZoom != 0) {
		d->startImageDataLoading();
	}
}


void LoadingDocumentImpl::slotImageLoaded() {
	LOG("");
	if (d->mImage.isNull()) {
		setDocumentErrorString(
			i18nc("@info", "Loading image failed.")
			);
		emit loadingFailed();
		switchToImpl(new EmptyDocumentImpl(document()));
		return;
	}

	if (d->mAnimated) {
		if (d->mImage.size() == d->mImageSize) {
			// We already decoded the first frame at the right size, let's show
			// it
			setDocumentImage(d->mImage);
		}

		switchToImpl(new AnimatedDocumentLoadedImpl(
			document(),
			d->mData));

		return;
	}

	if (d->mImageDataInvertedZoom != 1 && d->mImage.size() != d->mImageSize) {
		LOG("Loaded a down sampled image");
		d->mDownSampledImageLoaded = true;
		// We loaded a down sampled image
		setDocumentDownSampledImage(d->mImage, d->mImageDataInvertedZoom);
		return;
	}

	LOG("Loaded a full image");
	setDocumentImage(d->mImage);
	DocumentLoadedImpl* impl;
	if (d->mJpegContent.get()) {
		impl = new JpegDocumentLoadedImpl(
			document(),
			d->mJpegContent.release());
	} else {
		impl = new DocumentLoadedImpl(
			document(),
			d->mData);
	}
	switchToImpl(impl);
}


} // namespace
