/*
Gwenview - A simple image viewer for KDE
Copyright (C) 2000-2002 Aur�lien G�teau

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef GVFILEVIEW_H
#define GVFILEVIEW_H

#include <kfileview.h>

class GVFileView : public KFileView {
public:
	GVFileView() : mShownFileItem(0L) {}
	
	KFileItem* shownFileItem() const { return mShownFileItem; }
	virtual void setShownFileItem(KFileItem* fileItem) { mShownFileItem=fileItem; }

protected:
	KFileItem* mShownFileItem;
};

#endif
