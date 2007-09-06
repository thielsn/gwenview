// vim: set tabstop=4 shiftwidth=4 noexpandtab:
/*
Gwenview: an image viewer
Copyright 2007 Aurélien Gâteau

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
#include "imagemetainfo.h"

// Qt

// KDE
#include <kdebug.h>
#include <kfileitem.h>
#include <kglobal.h>
#include <klocale.h>

// Exiv2
#include <exiv2/exif.hpp>
#include <exiv2/image.hpp>

// Local


namespace Gwenview {


static const int noParentId = -1;


class MetaInfoGroup {
	struct Entry {
		QString mKey;
		QString mLabel;
		QString mValue;
	};
public:
	MetaInfoGroup(const QString& label)
	: mLabel(label) {}


	~MetaInfoGroup() {
		qDeleteAll(mList);
	}


	void clear() {
		qDeleteAll(mList);
		mList.clear();
	}


	void addEntry(const QString& key, const QString& label, const QString& value) {
		Entry* entry = new Entry;
		entry->mKey = key;
		entry->mLabel = label;
		entry->mValue = value;
		mList << entry;
	}


	void getInfoForKey(const QString& key, QString* label, QString* value) const {
		Q_FOREACH(Entry* entry, mList) {
			if (entry->mKey == key) {
				*label = entry->mLabel;
				*value = entry->mValue;
				return;
			}
		}
	}


	QString getLabelForKeyAt(int row) const {
		Q_ASSERT(row < mList.size());
		return mList[row]->mLabel;
	}


	QString getValueForKeyAt(int row) const {
		Q_ASSERT(row < mList.size());
		return mList[row]->mValue;
	}


	int size() const {
		return mList.size();
	}


	QString label() const {
		return mLabel;
	}

private:
	QList<Entry*> mList;
	QString mLabel;
};


struct ImageMetaInfoPrivate {
	QList<MetaInfoGroup*> mMetaInfoGroupList;
	ImageMetaInfo* mModel;


	void clearGroup(MetaInfoGroup* group, const QModelIndex& parent) {
		if (group->size() > 0) {
			group->clear();
			mModel->beginRemoveRows(parent, 0, group->size() - 1);
			mModel->endRemoveRows();
		}
	}


	void notifyGroupFilled(MetaInfoGroup* group, const QModelIndex& parent) {
		mModel->beginInsertRows(parent, 0, group->size() - 1);
		mModel->endInsertRows();
	}
};


ImageMetaInfo::ImageMetaInfo()
: d(new ImageMetaInfoPrivate) {
	d->mModel = this;
	d->mMetaInfoGroupList
		<< new MetaInfoGroup(i18n("File Information"))
		<< new MetaInfoGroup(i18n("Exif Information"))
		<< new MetaInfoGroup(i18n("Iptc Information"))
		;
}


ImageMetaInfo::~ImageMetaInfo() {
	qDeleteAll(d->mMetaInfoGroupList);
	delete d;
}


void ImageMetaInfo::setFileItem(const KFileItem& item) {
	MetaInfoGroup* group = d->mMetaInfoGroupList[0];
	QModelIndex parent = index(0, 0);
	d->clearGroup(group, parent);
	group->addEntry(
		"KFileItem.Name",
		i18n("Name"),
		item.name()
		);

	group->addEntry(
		"KFileItem.Size",
		i18n("File Size"),
		KGlobal::locale()->formatByteSize(item.size())
		);

	group->addEntry(
		"KFileItem.Time",
		i18n("File Time"),
		item.timeString()
		);
	d->notifyGroupFilled(group, parent);
}


template <class iterator>
static void fillExivGroup(MetaInfoGroup* group, iterator begin, iterator end) {
	iterator it = begin;
	for (;it != end; ++it) {
		QString key = QString::fromUtf8(it->key().c_str());
		QString label = QString::fromUtf8(it->tagLabel().c_str());
		std::ostringstream stream;
		stream << *it;
		QString value = QString::fromUtf8(stream.str().c_str());
		group->addEntry(key, label, value);
	}
}


void ImageMetaInfo::setExiv2Image(const Exiv2::Image* image) {
	MetaInfoGroup* exifGroup = d->mMetaInfoGroupList[1];
	QModelIndex parentExifIndex = index(1, 0);
	d->clearGroup(exifGroup, parentExifIndex);

	if (!image) {
		return;
	}

	if (image->supportsMetadata(Exiv2::mdExif)) {
		const Exiv2::ExifData& exifData = image->exifData();

		fillExivGroup(
			exifGroup,
			exifData.begin(),
			exifData.end()
			);
		
		d->notifyGroupFilled(exifGroup, parentExifIndex);
	}
};


void ImageMetaInfo::getInfoForKey(const QString& key, QString* label, QString* value) const {
	MetaInfoGroup* group;
	if (key.startsWith("KFileItem")) {
		group = d->mMetaInfoGroupList[0];
	} else if (key.startsWith("Exif")) {
		group = d->mMetaInfoGroupList[1];
	} else {
		kWarning() << "Unknown metainfo key" << key;
		return;
	}
	group->getInfoForKey(key, label, value);
}


QModelIndex ImageMetaInfo::index(int row, int col, const QModelIndex& parent) const {
	if (!parent.isValid()) {
		// This is a group
		if (col > 0) {
			return QModelIndex();
		}
		if (row >= d->mMetaInfoGroupList.size()) {
			return QModelIndex();
		}
		return createIndex(row, col, noParentId);
	} else {
		// This is an entry
		if (col > 1) {
			return QModelIndex();
		}
		int internalId = parent.row();
		if (row >= d->mMetaInfoGroupList[internalId]->size()) {
			return QModelIndex();
		}
		return createIndex(row, col, internalId);
	}
}


QModelIndex ImageMetaInfo::parent(const QModelIndex& index) const {
	if (!index.isValid()) {
		return QModelIndex();
	}
	if (index.internalId() == noParentId) {
		return QModelIndex();
	} else {
		return createIndex(index.internalId(), 0, noParentId);
	}
}


int ImageMetaInfo::rowCount(const QModelIndex& parent) const {
	if (!parent.isValid()) {
		return d->mMetaInfoGroupList.size();
	} else if (parent.internalId() == noParentId) {
		return d->mMetaInfoGroupList[parent.row()]->size();
	} else {
		return 0;
	}
}


int ImageMetaInfo::columnCount(const QModelIndex& /*parent*/) const {
	return 2;
}


QVariant ImageMetaInfo::data(const QModelIndex& index, int role) const {
	if (role != Qt::DisplayRole) {
		return QVariant();
	}

	if (!index.isValid()) {
		return QVariant();
	}

	if (index.internalId() == noParentId) {
		if (index.column() > 0) {
			return QVariant();
		}
		QString label = d->mMetaInfoGroupList[index.row()]->label();
		return QVariant(label);
	}

	MetaInfoGroup* group = d->mMetaInfoGroupList[index.internalId()];
	if (index.column() == 0) {
		return group->getLabelForKeyAt(index.row());
	} else {
		return group->getValueForKeyAt(index.row());
	}
}


QVariant ImageMetaInfo::headerData(int section, Qt::Orientation orientation, int role) const {
	if (orientation == Qt::Vertical || role != Qt::DisplayRole) {
		return QVariant();
	}

	QString caption;
	if (section == 0) {
		caption = i18n("Property");
	} else if (section == 1) {
		caption = i18n("Value");
	} else {
		kWarning() << "Unknown section" << section;
	}

	return QVariant(caption);
}

} // namespace
