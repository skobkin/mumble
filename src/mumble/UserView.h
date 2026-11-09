// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_USERVIEW_H_
#define MUMBLE_MUMBLE_USERVIEW_H_

#include <QtCore/QtGlobal>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTreeView>

#include "QtUtils.h"
#include "Timer.h"

/// Geometry of the trailing status-icon strip of one tree row, plus the
/// content area that remains for decoration and text.
///
/// Derived exclusively from the model-provided status-icon list (column 1,
/// Qt::DisplayRole) and the font-metrics-derived icon sizes, so that
/// painting, tooltip handling and click hit-testing all agree on one
/// calculation instead of each reconstructing an icon layout of their own.
struct UserStatusGeometry {
	/// Bounds of the whole icon strip; invalid when the row has no icons.
	QRect stripRect;
	/// One rectangle per status icon, in list order (index 0 is the leading
	/// icon - where the comment indicator sits when present).
	QList< QRect > iconRects;
	/// Bounds of the comment indicator icon; invalid when the row has none.
	QRect commentIconRect;
	/// The row rectangle reduced by the icon strip: where the decoration and
	/// the text are laid out (and what text elision has to fit into).
	QRect contentRect;
};

class UserDelegate : public QStyledItemDelegate {
private:
	Q_OBJECT
	Q_DISABLE_COPY(UserDelegate)

	int m_iconTotalDimension;
	int m_iconIconPadding;
	int m_iconIconDimension;

public:
	UserDelegate(QObject *parent);
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const Q_DECL_OVERRIDE;
	void adjustIcons(int iconTotalDimension, int iconIconPadding, int iconIconDimension);

	/// The single shared status-icon geometry calculation for one row.
	///
	/// Pure function of the style option (rect and layout direction), the
	/// model-provided status-icon list, the index of the comment indicator
	/// within that list (-1 for none) and the font-metrics-derived icon
	/// sizes. It has no model, view or Global access, so painting, tooltip
	/// forwarding and click hit-testing can all use it directly.
	static UserStatusGeometry computeGeometry(const QStyleOptionViewItem &option, const QList< QVariant > &statusIcons,
											  int commentIconIndex, int iconTotalDimension, int iconIconPadding,
											  int iconIconDimension);

public slots:
	bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
				   const QModelIndex &index) Q_DECL_OVERRIDE;
};

class UserView : public QTreeView {
private:
	Q_OBJECT
	Q_DISABLE_COPY(UserView)

	int m_iconTotalDimension;
	int m_iconIconPadding;
	int m_iconIconDimension;
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

	/// The current status-icon pitch (the width of one icon slot) in pixels.
	/// Derived from the view font's metrics, so it follows the font (and
	/// display scaling).
	int statusIconDimension() const { return m_iconTotalDimension; }

public slots:
	void nodeActivated(const QModelIndex &idx);
	void updateChannel(const QModelIndex &index);
};

#endif
