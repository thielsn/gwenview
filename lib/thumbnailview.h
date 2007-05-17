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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#ifndef THUMBNAILVIEW_H
#define THUMBNAILVIEW_H

#include <QListView>
#include "gwenviewlib_export.h"

class ThumbnailViewPrivate;
class GWENVIEWLIB_EXPORT ThumbnailView : public QListView {
	Q_OBJECT
public:
	ThumbnailView(QWidget* parent);
	~ThumbnailView();

	/**
	 * Sets the thumbnail size, in pixels.
	 */
	void setThumbnailSize(int pixel);

	/**
	 * Returns the thumbnail size.
	 */
	int thumbnailSize() const;

	/**
	 * Returns the width of an item. This width is proportional to the
	 * thumbnail size.
	 */
	int itemWidth() const;

	/**
	 * Returns the height of an item. This width is proportional to the
	 * thumbnail size.
	 */
	int itemHeight() const;	


private:
	ThumbnailViewPrivate * const d;
};

#endif /* THUMBNAILVIEW_H */
