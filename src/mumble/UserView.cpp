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
#include <QtWidgets/QStyle>
#include <QtWidgets/QWhatsThis>

UserDelegate::UserDelegate(QObject *p) : QStyledItemDelegate(p) {
}

void UserDelegate::adjustIcons(const UserStatusMetrics &metrics) {
	m_metrics = metrics;
}

UserStatusGeometry UserDelegate::computeGeometry(const QStyleOptionViewItem &option, const QList< QVariant > &statusIcons,
												 int commentIconIndex, const UserStatusMetrics &metrics) {
	UserStatusGeometry geometry;

	const int iconCount = static_cast< int >(statusIcons.count());
	if (iconCount <= 0) {
		geometry.contentRect = option.rect;
		return geometry;
	}

	// A row can be narrower than the full icon strip (a squeezed tree pane,
	// large fonts). The row's identity - decoration and elided name -
	// outranks the low-priority trailing status icons: keep at least
	// minContentWidth of content area and drop trailing icons first, so Qt
	// style rendering never sees a starved content rectangle. The comment
	// indicator is the leading icon, so it survives every such truncation.
	// Painting bounds, tooltip regions and click hit-testing all consume
	// this one result, so a truncated strip stays consistent across all of
	// them.
	const int availableForStrip =
		option.rect.width() - metrics.stripInset - metrics.contentGap - metrics.minContentWidth;
	const int visibleIcons =
		(metrics.iconTotalDimension > 0) ? qBound(0, availableForStrip / metrics.iconTotalDimension, iconCount) : 0;
	if (visibleIcons <= 0) {
		// Not even one icon slot fits next to the minimum content area:
		// behave like an icon-less row instead of painting icons outside the
		// row or starving the name.
		geometry.contentRect = option.rect;
		return geometry;
	}

	const int stripWidth = visibleIcons * metrics.iconTotalDimension;

	// Logical (left-to-right) layout: the strip occupies the trailing end of
	// the row, inset from the row edge; the content area keeps a small gap
	// to the strip. With a content reserve, visibleIcons above is chosen so
	// the content keeps at least that width; the clamp guards metrics
	// without a reserve so no consumer ever sees a negative rectangle.
	QRect strip(option.rect.x() + option.rect.width() - stripWidth - metrics.stripInset, option.rect.y(), stripWidth,
				option.rect.height());
	QRect content(option.rect.x(), option.rect.y(),
				  qMax(0, option.rect.width() - stripWidth - metrics.stripInset - metrics.contentGap),
				  option.rect.height());

	const int iconPosY = (option.rect.height() / 2) - (metrics.iconIconDimension / 2);
	geometry.iconRects.reserve(visibleIcons);
	for (int i = 0; i < visibleIcons; ++i) {
		QRect iconRect = strip;
		iconRect.setSize(QSize(metrics.iconIconDimension, metrics.iconIconDimension));
		iconRect.translate(i * metrics.iconTotalDimension + metrics.iconIconPadding, iconPosY);
		geometry.iconRects.append(iconRect);
	}

	// In right-to-left layouts both the strip and the content area move to
	// the visual trailing edge, and the icon slots are laid out mirrored
	// within the strip so the leading icon (the comment indicator) stays
	// adjacent to the content area.
	if (option.direction == Qt::RightToLeft) {
		strip   = QStyle::visualRect(option.direction, option.rect, strip);
		content = QStyle::visualRect(option.direction, option.rect, content);
		for (int i = 0; i < visibleIcons; ++i) {
			QRect &iconRect = geometry.iconRects[i];
			iconRect.moveLeft(strip.left() + strip.width() - (i + 1) * metrics.iconTotalDimension
							  + metrics.iconIconPadding);
		}
	}

	geometry.stripRect   = strip;
	geometry.contentRect = content;

	if (commentIconIndex >= 0 && commentIconIndex < visibleIcons) {
		geometry.commentIconRect = geometry.iconRects.at(commentIconIndex);
	}

	return geometry;
}

QFont UserDelegate::refinedFont(const QFont &base, UserModel::RowKind kind) {
	QFont font = base;
	switch (kind) {
		case UserModel::RowKind::Channel:
			// Channels structure the tree: one weight step heavier than
			// users. Compose with the model-provided font instead of
			// replacing it - a bold home channel or an italic linked
			// channel keeps its own emphasis, so never go lighter.
			font.setWeight(qMax(font.weight(), QFont::DemiBold));
			break;
		case UserModel::RowKind::Listener:
			// Listener proxies are always italic, whatever the
			// model-provided font says.
			font.setItalic(true);
			break;
		case UserModel::RowKind::User:
			break;
	}
	return font;
}

QPalette::ColorRole UserDelegate::textColorRole(UserModel::RowKind kind, bool selected) {
	if (kind == UserModel::RowKind::Listener && !selected) {
		// Dim listener proxies that are not selected; the italic text and
		// the ear icon retain the non-color identification.
		return QPalette::PlaceholderText;
	}
	return selected ? QPalette::HighlightedText : QPalette::Text;
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

	// The geometry below derives from the layout direction, so all three of
	// its consumers (paint, help events, click hit-testing) must agree on
	// it: take it from the widget, not from whatever the option carries.
	if (o.widget) {
		o.direction = o.widget->layoutDirection();
	}

	// Classify the row for the typography/color refinements; the model
	// provides the kind, so no icon, string or font heuristics here.
	const UserModel::RowKind kind = static_cast< UserModel::RowKind >(index.data(UserModel::RowKindRole).toInt());

	// Refine the typography per row kind, composing with the model-provided
	// font (FontRole) instead of replacing it.
	o.font        = refinedFont(o.font, kind);
	o.fontMetrics = QFontMetrics(o.font);

	QStyle *style        = o.widget->style();
	QIcon::Mode iconMode = QIcon::Normal;

	QPalette::ColorRole colorRole = textColorRole(kind, o.state & QStyle::State_Selected);
#if defined(Q_OS_WIN)
	// Qt's Vista Style has the wrong highlight color for treeview items
	// We can't check for QStyleSheetStyle so we have to search the children list search for a QWindowsVistaStyle
	QList< QObject * > hierarchy = style->findChildren< QObject * >();
	hierarchy.insert(0, style);
	for (QObject *obj : hierarchy) {
		if (QString::fromUtf8(obj->metaObject()->className()) == QString::fromUtf8("QWindowsVistaStyle")) {
			// Only the selected-row role is wrong on Vista; the dimmed
			// listener color is unaffected.
			if (colorRole == QPalette::HighlightedText) {
				colorRole = QPalette::Text;
			}
			break;
		}
	}
#endif

	// draw background
	style->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, painter, o.widget);

	// the shared geometry decides where the status icons sit and what
	// remains of the row for decoration and text
	const UserStatusGeometry geometry = computeGeometry(o, ql, -1, m_metrics);

	// the focus ring spans the whole row; capture its rect before the
	// option's rect is narrowed to the content area
	const bool hasFocus  = o.state & QStyle::State_HasFocus;
	const QRect focusRect = hasFocus ? style->subElementRect(QStyle::SE_ItemViewItemFocusRect, &o, o.widget) : QRect();

	// Draw the decoration and the text into the content area - but only when
	// it is usable: a degenerate row with no width left for the row's
	// identity must not hand Qt style rendering an empty rectangle.
	if (geometry.contentRect.width() > 0) {
		// draw icon
		o.rect                = geometry.contentRect;
		QRect decorationRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &o, o.widget);
		o.icon.paint(painter, decorationRect, o.decorationAlignment, iconMode, QIcon::On);

		// draw text
		QRect textRect   = style->subElementRect(QStyle::SE_ItemViewItemText, &o, o.widget);
		QString itemText = o.fontMetrics.elidedText(o.text, o.textElideMode, textRect.width());
		painter->setFont(o.font);
		style->drawItemText(painter, textRect, static_cast< int >(o.displayAlignment), o.palette, true, itemText,
							colorRole);
	}

	// draw the status icons at the shared geometry's positions. When the row
	// is too narrow for the whole strip, the geometry keeps only the leading
	// icons - painting follows it, so icons never render outside their row.
	for (int i = 0; i < geometry.iconRects.count(); ++i) {
		qvariant_cast< QIcon >(ql[i]).paint(painter, geometry.iconRects.at(i), o.decorationAlignment, iconMode,
											QIcon::On);
	}

	// draw the keyboard focus ring the default delegate's CE_ItemViewItem
	// rendering would have drawn (painting the parts ourselves dropped it)
	if (hasFocus) {
		QStyleOptionFocusRect focusOption;
		focusOption.QStyleOption::operator=(o);
		focusOption.rect = focusRect;
		focusOption.state |= QStyle::State_KeyboardFocusChange | QStyle::State_Item;
		style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOption, painter, o.widget);
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
		// icons: help events there answer the column-1 flags tooltip. As in
		// paint(), the layout direction comes from the view so the strip is
		// hit-tested where it is painted.
		QStyleOptionViewItem stripOption = option;
		if (view) {
			stripOption.direction = view->layoutDirection();
		}
		const UserStatusGeometry geometry = computeGeometry(stripOption, iconList, -1, m_metrics);

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
	const QFontMetrics fontMetrics(font());

	UserStatusMetrics metrics;
	metrics.iconTotalDimension = fontMetrics.height();
	metrics.iconIconPadding    = 1;
	metrics.iconIconDimension  = metrics.iconTotalDimension - (2 * metrics.iconIconPadding);

	// font-derived breathing room around the status strip, clamped so
	// pathological fonts cannot push it into absurdity
	const int spacing          = qBound(2, fontMetrics.horizontalAdvance(QStringLiteral("n")) / 2, 8);
	metrics.stripInset         = spacing;
	metrics.contentGap         = spacing;

	// Identity outranks status icons: keep room for the decoration (about
	// one icon slot) plus an ellipsis, so an elided name stays readable when
	// the row overflows. Clamped so pathological fonts cannot claim the
	// whole row.
	metrics.minContentWidth =
		qBound(20, metrics.iconTotalDimension + fontMetrics.horizontalAdvance(QStringLiteral("...")), 120);

	m_iconMetrics = metrics;
	m_userDelegate->adjustIcons(metrics);

	// tree indentation follows the same font metrics instead of a fixed
	// pixel default, so it scales with the font (and with display scaling)
	setIndentation(qBound(12, fontMetrics.height() + 4, 40));

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
	option.rect      = visualRect(idx);
	// paint() and helpEvent() derive the strip position from the widget's
	// layout direction; the click hit-test must see the same one.
	option.direction = layoutDirection();
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
			const UserStatusGeometry geometry = UserDelegate::computeGeometry(option, iconList, 0, m_iconMetrics);

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
