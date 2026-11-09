// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_USERVIEW_H_
#define MUMBLE_MUMBLE_USERVIEW_H_

#include <QtCore/QtGlobal>
#include <QtGui/QFont>
#include <QtGui/QPalette>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTreeView>

#include "QtUtils.h"
#include "Timer.h"
#include "UserModel.h"

/// Geometry of the trailing status-icon strip of one tree row, plus the
/// content area that remains for decoration and text.
///
/// Derived exclusively from the model-provided status-icon list (column 1,
/// Qt::DisplayRole) and the font-metrics-derived icon sizes, so that
/// painting, tooltip handling and click hit-testing all agree on one
/// calculation instead of each reconstructing an icon layout of their own.
///
/// When the row is too narrow to hold every icon, the strip is truncated to
/// the leading icons that fit (see UserDelegate::computeGeometry);
/// iconRects then holds fewer entries than the model's icon list.
struct UserStatusGeometry {
	/// Bounds of the whole icon strip; invalid when the row has no icons (or
	/// none of them fit).
	QRect stripRect;
	/// One rectangle per status icon that fits in the row, in list order
	/// (index 0 is the leading icon - where the comment indicator sits when
	/// present).
	QList< QRect > iconRects;
	/// Bounds of the comment indicator icon; invalid when the row has none
	/// or when its slot did not fit.
	QRect commentIconRect;
	/// The row rectangle reduced by the icon strip: where the decoration and
	/// the text are laid out (and what text elision has to fit into). Never
	/// negative, and whenever status icons are shown at least
	/// UserStatusMetrics::minContentWidth wide (trailing icons are dropped
	/// first); the full row when no icon fits. Only a degenerate zero-width
	/// row can leave it empty.
	QRect contentRect;
};

/// Font-metrics-derived sizes that shape the status-icon strip: icon
/// dimensions plus the breathing room between the strip, the row edge and
/// the content area. Computed by UserView::adjustIcons() so they follow the
/// view font (and with it, display scaling) at construction and on every
/// runtime font or style change.
struct UserStatusMetrics {
	/// Width of one icon slot (the pitch between two adjacent icons).
	int iconTotalDimension = 0;
	/// Empty space kept between two icon slots.
	int iconIconPadding    = 1;
	/// Edge length of one icon.
	int iconIconDimension  = 0;
	/// Gap between the row's trailing edge and the strip.
	int stripInset         = 0;
	/// Gap between the content area and the strip.
	int contentGap         = 0;
	/// Minimum content width reserved for the decoration and an elided
	/// name. Trailing status icons are dropped before the content area
	/// shrinks below this (0 disables the reserve): a row's identity
	/// outranks its low-priority status icons.
	int minContentWidth    = 0;
};

class UserDelegate : public QStyledItemDelegate {
private:
	Q_OBJECT
	Q_DISABLE_COPY(UserDelegate)

	UserStatusMetrics m_metrics;

public:
	UserDelegate(QObject *parent);
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const Q_DECL_OVERRIDE;
	void adjustIcons(const UserStatusMetrics &metrics);

	/// The single shared status-icon geometry calculation for one row.
	///
	/// Pure function of the style option (rect and layout direction), the
	/// model-provided status-icon list, the index of the comment indicator
	/// within that list (-1 for none) and the font-metrics-derived sizes. It
	/// has no model, view or Global access, so painting, tooltip forwarding
	/// and click hit-testing can all use it directly. In right-to-left
	/// layouts the strip and the content area are mirrored to the visual
	/// trailing edge (the icons keep their list order). If the row is too
	/// narrow to hold every icon next to the reserved minimum content area
	/// (see UserStatusMetrics::minContentWidth), trailing icons are dropped
	/// until it is - the comment indicator is the leading icon, so it stays
	/// usable as long as any icon fits - and if not even one icon slot fits
	/// next to the minimum content, the row behaves like an icon-less row
	/// (full-width content, empty strip).
	static UserStatusGeometry computeGeometry(const QStyleOptionViewItem &option, const QList< QVariant > &statusIcons,
											  int commentIconIndex, const UserStatusMetrics &metrics);

	/// Typography refinement per row kind, composing with the model-provided
	/// font (FontRole) instead of replacing it: channels read one weight
	/// step heavier (never lighter than what the model font asks for, so
	/// e.g. the bold home channel stays bold) and listener proxies are
	/// always italic.
	static QFont refinedFont(const QFont &base, UserModel::RowKind kind);

	/// The text color role for a row kind in the given selection state:
	/// listener proxies render dimmed (placeholder color) when not
	/// selected; everything else follows the regular text/highlight roles.
	static QPalette::ColorRole textColorRole(UserModel::RowKind kind, bool selected);

public slots:
	bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
				   const QModelIndex &index) Q_DECL_OVERRIDE;
};

class UserView : public QTreeView {
private:
	Q_OBJECT
	Q_DISABLE_COPY(UserView)

	UserStatusMetrics m_iconMetrics;
	qt_unique_ptr< UserDelegate > m_userDelegate;
	void adjustIcons();
	/// Style option for hit-testing row \p idx at its current visual position.
	/// Built from the same base the delegate-paint pipeline uses, with the
	/// row's visual rectangle set.
	QStyleOptionViewItem itemOptionFor(const QModelIndex &idx) const;

protected:
	void mouseReleaseEvent(QMouseEvent *) Q_DECL_OVERRIDE;
	void keyPressEvent(QKeyEvent *) Q_DECL_OVERRIDE;
	bool event(QEvent *) Q_DECL_OVERRIDE;

public:
	UserView(QWidget *);
	void keyboardSearch(const QString &search) Q_DECL_OVERRIDE;
	void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
					 const QVector< int > &roles = QVector< int >()) Q_DECL_OVERRIDE;

	/// The current font-derived metrics of the status-icon strip (icon
	/// dimensions and spacing). They follow the view font, so they track
	/// font changes and display scaling.
	UserStatusMetrics statusMetrics() const { return m_iconMetrics; }

	/// The current status-icon pitch (the width of one icon slot) in pixels.
	/// Derived from the view font's metrics, so it follows the font (and
	/// display scaling).
	int statusIconDimension() const { return m_iconMetrics.iconTotalDimension; }

public slots:
	void nodeActivated(const QModelIndex &idx);
	void updateChannel(const QModelIndex &index);
};

#endif
