// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UserView.h"

#include "Channel.h"
#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "ServerHandler.h"
#include "UserModel.h"
#include "Global.h"

#include <QtGui/QDesktopServices>
#include <QtGui/QHelpEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QWhatsThis>

UserDelegate::UserDelegate(QObject *p) : QStyledItemDelegate(p) {
}

void UserDelegate::adjustIcons(int iconTotalDimension, int iconIconPadding, int iconIconDimension) {
	m_iconTotalDimension = iconTotalDimension;
	m_iconIconPadding    = iconIconPadding;
	m_iconIconDimension  = iconIconDimension;
}

UserStatusGeometry UserDelegate::computeGeometry(const QStyleOptionViewItem &option, const QList< QVariant > &statusIcons,
												 int commentIconIndex, int iconTotalDimension, int iconIconPadding,
												 int iconIconDimension) {
	UserStatusGeometry geometry;

	const int iconCount = static_cast< int >(statusIcons.count());
	if (iconCount <= 0) {
		geometry.contentRect = option.rect;
		return geometry;
	}

	const int stripWidth = iconCount * iconTotalDimension;
	geometry.stripRect   = QRect(option.rect.right() - stripWidth, option.rect.y(), stripWidth, option.rect.height());
	geometry.contentRect = QRect(option.rect.x(), option.rect.y(), option.rect.width() - stripWidth,
								 option.rect.height());

	const int iconPosY = (option.rect.height() / 2) - (iconIconDimension / 2);
	geometry.iconRects.reserve(iconCount);
	for (int i = 0; i < iconCount; ++i) {
		QRect iconRect = geometry.stripRect;
		iconRect.setSize(QSize(iconIconDimension, iconIconDimension));
		iconRect.translate(i * iconTotalDimension + iconIconPadding, iconPosY);
		geometry.iconRects.append(iconRect);
	}

	if (commentIconIndex >= 0 && commentIconIndex < iconCount) {
		geometry.commentIconRect = geometry.iconRects.at(commentIconIndex);
	}

	return geometry;
}

void UserDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	const QAbstractItemModel *m = index.model();
	const QModelIndex idxc1     = index.sibling(index.row(), 1);
	QVariant data               = m->data(idxc1);
	QList< QVariant > ql        = data.toList();

	// Allow a UserView's BackgroundRole to override the current theme's default color.
	QVariant bg = index.data(Qt::BackgroundRole);
	if (bg.isValid()) {
		painter->fillRect(option.rect, bg.value< QBrush >());
	}

	painter->save();

	QStyleOptionViewItem o = option;
	initStyleOption(&o, index);

	QStyle *style        = o.widget->style();
	QIcon::Mode iconMode = QIcon::Normal;

	QPalette::ColorRole colorRole = ((o.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
#if defined(Q_OS_WIN)
	// Qt's Vista Style has the wrong highlight color for treeview items
	// We can't check for QStyleSheetStyle so we have to search the children list search for a QWindowsVistaStyle
	QList< QObject * > hierarchy = style->findChildren< QObject * >();
	hierarchy.insert(0, style);
	for (QObject *obj : hierarchy) {
		if (QString::fromUtf8(obj->metaObject()->className()) == QString::fromUtf8("QWindowsVistaStyle")) {
			colorRole = QPalette::Text;
			break;
		}
	}
#endif

	// draw background
	style->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, painter, o.widget);

	// the shared geometry decides where the status icons sit and what
	// remains of the row for decoration and text
	const UserStatusGeometry geometry = computeGeometry(option, ql, -1, m_iconTotalDimension, m_iconIconPadding,
														m_iconIconDimension);

	// draw icon
	o.rect            = geometry.contentRect;
	QRect decorationRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &o, o.widget);
	o.icon.paint(painter, decorationRect, o.decorationAlignment, iconMode, QIcon::On);

	// draw text
	QRect textRect   = style->subElementRect(QStyle::SE_ItemViewItemText, &o, o.widget);
	QString itemText = o.fontMetrics.elidedText(o.text, o.textElideMode, textRect.width());
	painter->setFont(o.font);
	style->drawItemText(painter, textRect, static_cast< int >(o.displayAlignment), o.palette, true, itemText,
						colorRole);

	// draw the status icons at the shared geometry's positions
	for (int i = 0; i < ql.size(); ++i) {
		qvariant_cast< QIcon >(ql[i]).paint(painter, geometry.iconRects.at(i), option.decorationAlignment, iconMode,
											QIcon::On);
	}

	painter->restore();
}

bool UserDelegate::helpEvent(QHelpEvent *evt, QAbstractItemView *view, const QStyleOptionViewItem &option,
							 const QModelIndex &index) {
	if (index.isValid()) {
		const QAbstractItemModel *m      = index.model();
		const QModelIndex firstColumnIdx = index.sibling(index.row(), 1);
		QVariant data                    = m->data(firstColumnIdx);
		QList< QVariant > iconList       = data.toList();

		// the shared geometry decides which positions belong to the status
		// icons: help events there answer the column-1 flags tooltip
		const UserStatusGeometry geometry = computeGeometry(option, iconList, -1, m_iconTotalDimension,
															m_iconIconPadding, m_iconIconDimension);

		if (geometry.stripRect.contains(evt->pos())) {
			return QStyledItemDelegate::helpEvent(evt, view, option, firstColumnIdx);
		}
	}
	return QStyledItemDelegate::helpEvent(evt, view, option, index);
}

UserView::UserView(QWidget *p) : QTreeView(p), m_userDelegate(make_qt_unique< UserDelegate >(this)) {
	adjustIcons();
	setItemDelegate(m_userDelegate.get());

	// Because in Qt fonts take some time to initialize properly, we have to delay the call
	// to adjustIcons a bit in order to give the fonts the necessary time (so we can read out
	// the actual font details).
	QTimer::singleShot(0, this, [this]() { adjustIcons(); });

	connect(this, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(nodeActivated(const QModelIndex &)));
}

void UserView::adjustIcons() {
	// Calculate the icon size for status icons based on font size
	// This should automaticially adjust size when the user has
	// display scaling enabled
	m_iconTotalDimension = QFontMetrics(font()).height();
	m_iconIconPadding    = 1;
	m_iconIconDimension  = m_iconTotalDimension - (2 * m_iconIconPadding);
	m_userDelegate->adjustIcons(m_iconTotalDimension, m_iconIconPadding, m_iconIconDimension);
	viewport()->update();
}

/**
 * This implementation contains a special handler to display
 * custom what's this entries for items. All other events are
 * passed on.
 */
bool UserView::event(QEvent *evt) {
	if (evt->type() == QEvent::WhatsThisClicked) {
		QWhatsThisClickedEvent *qwtce = static_cast< QWhatsThisClickedEvent * >(evt);
		QDesktopServices::openUrl(qwtce->href());
		evt->accept();
		return true;
	} else if (evt->type() == QEvent::FontChange || evt->type() == QEvent::StyleChange) {
		// The status-icon dimensions (and with them the hit-test geometry)
		// derive from the view font's metrics: re-derive them whenever the
		// font or the style changes at runtime (e.g. a theme or font-size
		// change after startup). Deferred like the constructor's initial
		// call so the new font details are settled, and bound to this view
		// so a pending refresh dies with it.
		QTimer::singleShot(0, this, [this]() { adjustIcons(); });
	}
	return QTreeView::event(evt);
}

QStyleOptionViewItem UserView::itemOptionFor(const QModelIndex &idx) const {
	QStyleOptionViewItem option;
	initViewItemOption(&option);
	option.rect = visualRect(idx);
	return option;
}

/**
 * This function is used to create custom behaviour when clicking
 * on user/channel icons (e.Global::get(). showing the comment)
 */
void UserView::mouseReleaseEvent(QMouseEvent *evt) {
	QPoint clickPosition = evt->pos();

	QModelIndex idx = indexAt(clickPosition);
	if ((evt->button() == Qt::LeftButton) && idx.isValid()) {
		// Hit-test against the very geometry the delegate paints: the
		// model-provided status-icon list (column 1) decides where the icons
		// are, and CommentIconRole whether the leading one is the comment
		// indicator. (Reconstructing icon presence from ClientUser/Channel
		// flags here used to drift from the painted icons, e.g. for
		// push-to-mute and listener proxy rows.)
		const QList< QVariant > iconList = idx.sibling(idx.row(), 1).data(Qt::DisplayRole).toList();
		const bool hasCommentIcon        = idx.data(UserModel::CommentIconRole).toBool();

		if (hasCommentIcon) {
			QStyleOptionViewItem option = itemOptionFor(idx);
			const UserStatusGeometry geometry = UserDelegate::computeGeometry(
				option, iconList, 0, m_iconTotalDimension, m_iconIconPadding, m_iconIconDimension);

			if (geometry.commentIconRect.contains(clickPosition)) {
				// Clicked the comment icon. Consuming the release here also
				// suppresses the base-class handling of this click (selection,
				// clicked() and - on single-click-activation platforms -
				// activated()); nothing relies on those for comment icons.
				QString str = idx.data(Qt::ToolTipRole).toString();
				if (auto *userModel = qobject_cast< UserModel * >(model())) {
					if (str.isEmpty()) {
						userModel->bClicked = true;
					} else {
						QWhatsThis::showText(viewport()->mapToGlobal(option.rect.bottomRight()), str, this);
						userModel->seenComment(idx);
					}
				}
				return;
			}
		}
	}
	QTreeView::mouseReleaseEvent(evt);
}

void UserView::keyPressEvent(QKeyEvent *ev) {
	if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter)
		UserView::nodeActivated(currentIndex());
	QTreeView::keyPressEvent(ev);
}

void UserView::nodeActivated(const QModelIndex &idx) {
	UserModel *um = static_cast< UserModel * >(model());
	ClientUser *p = um->getUser(idx);
	if (p) {
		Global::get().mw->openTextMessageDialog(p);
		return;
	}

	Channel *c = um->getChannel(idx);
	if (c) {
		// if a channel is activated join it
		Global::get().sh->joinChannel(Global::get().uiSession, c->iId);
	}
}

void UserView::keyboardSearch(const QString &) {
	// Disable keyboard search for the UserView in order to prevent jumping wildly through the
	// UI just because the user has accidentally typed something on their keyboard.
	return;
}

void UserView::updateChannel(const QModelIndex &idx) {
	UserModel *um = static_cast< UserModel * >(model());

	if (!idx.isValid()) {
		return;
	}

	Channel *c = um->getChannel(idx);

	for (int i = 0; idx.model()->index(i, 0, idx).isValid(); ++i) {
		updateChannel(idx.model()->index(i, 0, idx));
	}

	if (c && idx.parent().isValid()) {
		setRowHidden(idx.row(), idx.parent(), c->isFiltered());
	}
}

void UserView::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector< int > &) {
	UserModel *um = static_cast< UserModel * >(model());
	int nRowCount = um->rowCount();
	int i;
	for (i = 0; i < nRowCount; i++)
		updateChannel(um->index(i, 0));

	QTreeView::dataChanged(topLeft, bottomRight);
}
