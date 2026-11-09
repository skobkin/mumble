// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

// Tests for the shared status-icon geometry and the modernized presentation
// of the channel/user tree (Stage 3 of the new UI). UserModel's real data()
// path requires a Global/database/server environment, so the view/delegate
// integration runs against a synthetic model mirroring UserModel's observable
// presentation protocol. What these fixtures prove: one geometry drives
// painting bounds, tooltip forwarding and click hit-testing; the comment
// hitbox sits on the painted comment icon for every status combination
// (including the push-to-mute and listener-proxy rows the old flag-based
// reconstruction got wrong); icon metrics and tree indentation track font
// changes at construction and at runtime; the strip mirrors to the visual
// trailing edge in right-to-left layouts (geometry, clicks and painted
// pixels); rows too narrow for the whole strip truncate it to the leading
// icons that fit (keeping the comment indicator usable) instead of
// producing negative content geometry; and the per-row-kind
// typography/focus refinements render.
// Server-driven behavior (comments arriving, seen-state, talking state)
// stays on the live checklist.

#include <QtTest>

#include "ChannelFilterMode.h"
#include "ClientUser.h"
#include "UserModel.h"
#include "UserView.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHelpEvent>
#include <QIcon>
#include <QImage>
#include <QModelIndex>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QVariant>

#include <memory>

namespace {

QIcon commentStatusIcon() {
	QPixmap pixmap(16, 16);
	pixmap.fill(Qt::blue);
	return QIcon(pixmap);
}

QIcon otherStatusIcon() {
	QPixmap pixmap(16, 16);
	pixmap.fill(Qt::red);
	return QIcon(pixmap);
}

// Synthetic stand-in mirroring the UserModel presentation protocol the
// UserView/UserDelegate pair consumes:
//  - columnCount() == 1, but index() also accepts column 1 (exactly like
//    UserModel::index), so sibling(row, 1) resolves and serves data;
//  - column 0: DisplayRole name, DecorationRole icon, optional FontRole;
//  - column 1: DisplayRole = the status-icon QVariantList, ToolTipRole the
//    flags tooltip (counted when queried so tests can see the forwarding);
//  - CommentIconRole: whether the leading status icon is the comment;
//  - RowKindRole: Channel / User / Listener classification.
// It never emits dataChanged (UserView::dataChanged assumes a UserModel).
class FakeNavigationModel : public QAbstractItemModel {
public:
	enum class RowType { Channel, User, Listener };

	struct Node {
		QString name;
		RowType type         = RowType::User;
		QVariantList statusIcons;
		bool commentIcon     = false;
		bool selfStyled      = false;
		QString flagsTooltip;
		QVector< Node * > children;
		Node *parent         = nullptr;

		~Node() { qDeleteAll(children); }
	};

	explicit FakeNavigationModel(QObject *parent = nullptr) : QAbstractItemModel(parent) {
		m_root       = new Node;
		m_root->name = QStringLiteral("Root");

		// A channel whose row shows the full channel strip: comment + filter
		// + lock, with the matching status tooltip the production model
		// assembles for it.
		Node *channel        = addNode(m_root, QStringLiteral("Everything Channel"), RowType::Channel);
		channel->statusIcons = { commentStatusIcon(), otherStatusIcon(), otherStatusIcon() };
		channel->commentIcon = true;
		channel->flagsTooltip =
			UserModel::channelStatusTooltip(ChannelFilterMode::HIDE, true, false);

		// A channel without any icons: the empty-strip case.
		addNode(m_root, QStringLiteral("Bare Channel"), RowType::Channel);

		// Plain user: no status icons at all.
		addNode(channel, QStringLiteral("Plain User"), RowType::User);

		// User with a comment and no other status: the comment icon is the
		// whole strip.
		Node *commentOnly         = addNode(channel, QStringLiteral("Comment Only"), RowType::User);
		commentOnly->statusIcons  = { commentStatusIcon() };
		commentOnly->commentIcon  = true;
		commentOnly->flagsTooltip = QStringLiteral("Comment");

		// Comment plus k simultaneous status icons (k = 1..5): the comment
		// stays the leading icon and its hit slot must not shift.
		for (int k = 1; k <= 5; ++k) {
			Node *node = addNode(channel, QStringLiteral("Comment Plus %1").arg(k), RowType::User);
			QVariantList icons{ commentStatusIcon() };
			for (int i = 0; i < k; ++i)
				icons << otherStatusIcon();
			node->statusIcons  = icons;
			node->commentIcon  = true;
			node->flagsTooltip = QStringLiteral("Comment Plus %1").arg(k);
		}

		// Self-style row whose strip carries a push-to-mute icon after the
		// comment: the painted list is the only truth. (Regression: the old
		// flag-based hit test did not count push-to-mute and placed the
		// comment hitbox one slot off.)
		Node *selfPushToMute         = addNode(channel, QStringLiteral("Self With Push To Mute"), RowType::User);
		selfPushToMute->statusIcons  = { commentStatusIcon(), otherStatusIcon() };
		selfPushToMute->commentIcon  = true;
		selfPushToMute->selfStyled   = true;
		selfPushToMute->flagsTooltip = QStringLiteral("Self");

		// A row carrying the maximum realistic simultaneous statuses (the
		// comment plus twelve others): narrow panes cannot hold the whole
		// strip, exercising the truncated-overflow geometry.
		Node *maxStatuses        = addNode(channel, QStringLiteral("Max Statuses"), RowType::User);
		maxStatuses->statusIcons = { commentStatusIcon() };
		for (int i = 0; i < 12; ++i)
			maxStatuses->statusIcons << otherStatusIcon();
		maxStatuses->commentIcon  = true;
		maxStatuses->flagsTooltip = QStringLiteral("Max Statuses");

		// Listener proxy: icons may be present (the listening indicator) but
		// no comment indicator, even though the underlying user has a
		// comment hash. (Regression: the old hit test claimed a comment.)
		Node *listener         = addNode(channel, QStringLiteral("Listener Proxy"), RowType::Listener);
		listener->statusIcons  = { otherStatusIcon() };
		listener->flagsTooltip = QStringLiteral("Channel Listener");
	}

	~FakeNavigationModel() override { delete m_root; }

	Node *addNode(Node *parent, const QString &name, RowType type) {
		Node *node   = new Node;
		node->name   = name;
		node->type   = type;
		node->parent = parent;
		parent->children.append(node);
		return node;
	}

	QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override {
		// UserModel reports columnCount() == 1 but still serves column-1
		// data through index(); mirror that quirk.
		if ((row < 0) || (column < 0) || (column > 1))
			return QModelIndex();

		const Node *parentNode = parent.isValid() ? node(parent) : m_root;
		if (!parentNode || row >= parentNode->children.count())
			return QModelIndex();

		return createIndex(row, column, parentNode->children.at(row));
	}

	QModelIndex parent(const QModelIndex &idx) const override {
		const Node *childNode = node(idx);
		if (!childNode || !childNode->parent || childNode->parent == m_root)
			return QModelIndex();

		return createIndex(rowOf(childNode->parent), 0, childNode->parent);
	}

	int rowCount(const QModelIndex &parent = QModelIndex()) const override {
		const Node *parentNode = parent.isValid() ? node(parent) : m_root;
		return parentNode ? static_cast< int >(parentNode->children.count()) : 0;
	}

	int columnCount(const QModelIndex &parent = QModelIndex()) const override {
		Q_UNUSED(parent);
		return 1;
	}

	bool hasChildren(const QModelIndex &parent = QModelIndex()) const override { return rowCount(parent) > 0; }

	QVariant data(const QModelIndex &idx, int role) const override {
		const Node *rowNode = node(idx);
		if (!rowNode)
			return QVariant();

		switch (role) {
			case Qt::DisplayRole:
				if (idx.column() == 0)
					return rowNode->name;
				return rowNode->statusIcons;
			case Qt::DecorationRole:
				if (idx.column() == 0)
					return m_rowIcon;
				return QVariant();
			case Qt::FontRole:
				if (rowNode->selfStyled) {
					QFont font = QApplication::font();
					font.setBold(true);
					return font;
				}
				return QVariant();
			case Qt::ToolTipRole:
				if (idx.column() == 1) {
					// Counted: tests detect where a help event was answered.
					++m_columnOneToolTipQueries;
					return rowNode->flagsTooltip;
				}
				return rowNode->name;
			case UserModel::CommentIconRole:
				return rowNode->commentIcon;
			case UserModel::RowKindRole:
				return static_cast< int >(rowNode->type == RowType::Channel
											  ? UserModel::RowKind::Channel
											  : (rowNode->type == RowType::Listener ? UserModel::RowKind::Listener
																					: UserModel::RowKind::User));
		}

		return QVariant();
	}

	Qt::ItemFlags flags(const QModelIndex &idx) const override {
		Q_UNUSED(idx);
		return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	}

	int columnOneToolTipQueries() const { return m_columnOneToolTipQueries; }

private:
	static int rowOf(const Node *rowNode) {
		return rowNode->parent ? static_cast< int >(rowNode->parent->children.indexOf(const_cast< Node * >(rowNode)))
							   : 0;
	}

	static const Node *node(const QModelIndex &idx) {
		return idx.isValid() ? static_cast< const Node * >(idx.internalPointer()) : nullptr;
	}

	Node *m_root                        = nullptr;
	mutable int m_columnOneToolTipQueries = 0;
	QIcon m_rowIcon                     = otherStatusIcon();
};

// Counts deferred update requests delivered to a widget (the visible effect
// of adjustIcons(): it schedules a viewport update).
class UpdateRequestCounter : public QObject {
public:
	int count = 0;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		if (event->type() == QEvent::UpdateRequest)
			++count;
		return QObject::eventFilter(watched, event);
	}
};

} // namespace

class TestUserNavigation : public QObject {
	Q_OBJECT

private slots:
	// The shared calculation follows the documented contract for both
	// layout directions: trailing-edge strip with inset, content gap,
	// mirrored icon slots in RTL.
	void geometryFollowsMetricsContract();
	void geometryIconRectsAreDisjointInsideStrip();
	void commentRectIsTheLeadingIconInView();

	// For every fixture row, every pixel across the row must classify the
	// same way for clicking as the painted geometry says - the comment icon
	// consumes clicks, everything else is a normal click.
	void clickClassificationMatchesPaintedGeometry();

	// Help events answer the column-1 flags tooltip exactly inside the icon
	// strip (observed through the model's query counter and the delegate's
	// return value, which mirrors the tooltip data).
	void helpEventForwardsExactlyTheIconStrip();

	// The channel status tooltip helper covers every filter/restriction
	// combination, and the user flags string carries the suppressed state.
	void channelStatusTooltipCoversAllStates();
	void flagsStringIncludesSuppressed();

	// In right-to-left layouts the strip must move to the visual trailing
	// edge - in the geometry, in click classification, and in what is
	// actually painted (viewport grab evidence).
	void iconStripMirrorsToVisualTrailingEdgeInRtl();

	// Typography and color refinements per RowKind, composing with (not
	// replacing) the model-provided font.
	void typographyRefinesPerRowKind();

	// The keyboard focus ring renders when the row option carries focus.
	void focusRingRendersWhenFocused();

	void iconMetricsTrackLargeFontsAtConstruction();
	void iconMetricsTrackRuntimeFontChange();
	void styleChangeRefreshesIconMetrics();
	void indentationFollowsFontMetrics();
	void narrowWidthShrinksContentAreaByStrip();

	// Rows narrower than their full icon strip: the geometry keeps only the
	// leading icons that fit and never hands out negative or row-escaping
	// rectangles - as a pure-geometry matrix (both layout directions, large
	// icon metrics) and end-to-end at a 180 px pane full of statuses, a
	// pathological 60 px pane, doubled fonts and RTL (geometry, clicks,
	// tooltip forwarding and painted pixels).
	void narrowGeometryKeepsOnlyLeadingIcons();
	void narrowRowsStayConsistentUnderOverflow();

private:
	/// Processes events until the view's deferred-work cascades settle (the
	/// UserView constructor and its font/style change handling defer
	/// adjustIcons through zero-timeout timers bound to the view).
	static void drainEvents(int passes = 10) {
		for (int i = 0; i < passes; ++i)
			QApplication::processEvents();
	}

	/// A shown, sized view on the synthetic tree, mirroring the production
	/// configuration (single stretched column, uniform row heights).
	class ViewHarness {
	public:
		FakeNavigationModel model;
		UserView view;

		ViewHarness() : view(nullptr) {
			view.setModel(&model);
			view.setUniformRowHeights(true);
			view.resize(420, 480);
			view.show();
			drainEvents();
			view.expandAll();
			drainEvents();
		}
	};

	static QModelIndex findRow(const QAbstractItemModel *model, const QString &name,
							   const QModelIndex &parent = QModelIndex()) {
		for (int row = 0; row < model->rowCount(parent); ++row) {
			const QModelIndex index = model->index(row, 0, parent);
			if (index.data(Qt::DisplayRole).toString() == name)
				return index;
			const QModelIndex deeper = findRow(model, name, index);
			if (deeper.isValid())
				return deeper;
		}
		return QModelIndex();
	}

	/// Counts strongly blue or red pixels (the fixture icon colors) in the
	/// left and right half of a row band of a rendered image - where the
	/// status-icon strip was actually painted.
	static QPair< int, int > coloredPixelsPerSide(const QImage &image, const QRect &rowRect) {
		int leftCount  = 0;
		int rightCount = 0;
		for (int y = rowRect.top(); y <= rowRect.bottom(); ++y) {
			for (int x = rowRect.left(); x <= rowRect.right(); ++x) {
				const QColor color(image.pixel(x, y));
				const bool blue = (color.blueF() > 0.4 && color.blueF() > color.redF() + 0.2
								   && color.blueF() > color.greenF() + 0.2);
				const bool red  = (color.redF() > 0.4 && color.redF() > color.blueF() + 0.2
								   && color.redF() > color.greenF() + 0.2);
				if (!blue && !red)
					continue;
				if (x <= rowRect.center().x())
					++leftCount;
				else
					++rightCount;
			}
		}
		return qMakePair(leftCount, rightCount);
	}

	/// The geometry the production hit-test path computes for a row: driven
	/// by the model-provided icon list, the CommentIconRole and the view's
	/// current font-derived metrics.
	static UserStatusGeometry rowGeometry(const UserView &view, const QModelIndex &idx) {
		QStyleOptionViewItem option;
		option.rect      = view.visualRect(idx);
		option.direction = view.layoutDirection();

		const QList< QVariant > icons = idx.sibling(idx.row(), 1).data(Qt::DisplayRole).toList();
		const bool commentIcon        = idx.data(UserModel::CommentIconRole).toBool();

		return UserDelegate::computeGeometry(option, icons, commentIcon ? 0 : -1, view.statusMetrics());
	}
};

void TestUserNavigation::geometryFollowsMetricsContract() {
	UserStatusMetrics metrics;
	metrics.iconTotalDimension = 16;
	metrics.iconIconPadding    = 1;
	metrics.iconIconDimension  = 14;
	metrics.stripInset         = 3;
	metrics.contentGap         = 3;

	const int iconPosY = (24 / 2) - (metrics.iconIconDimension / 2);

	for (int count = 0; count <= 6; ++count) {
		QVariantList icons;
		for (int i = 0; i < count; ++i)
			icons << QVariant(commentStatusIcon());

		for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft }) {
			QStyleOptionViewItem option;
			option.rect      = QRect(7, 11, 300, 24);
			option.direction = direction;

			const UserStatusGeometry geometry =
				UserDelegate::computeGeometry(option, icons, count > 0 ? 0 : -1, metrics);

			if (count == 0) {
				QVERIFY(!geometry.stripRect.isValid());
				QVERIFY(geometry.iconRects.isEmpty());
				QVERIFY(!geometry.commentIconRect.isValid());
				QCOMPARE(geometry.contentRect, option.rect);
				continue;
			}

			const int stripWidth = count * metrics.iconTotalDimension;
			QCOMPARE(geometry.stripRect.width(), stripWidth);
			QCOMPARE(geometry.contentRect.width(),
					 option.rect.width() - stripWidth - metrics.stripInset - metrics.contentGap);
			QCOMPARE(geometry.commentIconRect, geometry.iconRects.first());

			for (int i = 0; i < count; ++i) {
				const QRect expected(direction == Qt::LeftToRight
										 ? QRect(geometry.stripRect.left() + i * metrics.iconTotalDimension
												 + metrics.iconIconPadding,
												 geometry.stripRect.top() + iconPosY, metrics.iconIconDimension,
												 metrics.iconIconDimension)
										 : QRect(geometry.stripRect.left() + geometry.stripRect.width()
												 - (i + 1) * metrics.iconTotalDimension + metrics.iconIconPadding,
												 geometry.stripRect.top() + iconPosY, metrics.iconIconDimension,
												 metrics.iconIconDimension));
				QCOMPARE(geometry.iconRects.at(i), expected);
			}

			if (direction == Qt::LeftToRight) {
				// strip at the trailing end, inset from the row edge
				QCOMPARE(geometry.stripRect.right(), option.rect.right() - metrics.stripInset);
				QCOMPARE(geometry.contentRect.right(), geometry.stripRect.left() - metrics.contentGap - 1);
			} else {
				// mirrored: strip at the left edge, content keeps the right
				QCOMPARE(geometry.stripRect.left(), option.rect.left() + metrics.stripInset);
				QCOMPARE(geometry.contentRect.right(), option.rect.right());
				QCOMPARE(geometry.contentRect.left(), geometry.stripRect.right() + metrics.contentGap + 1);
				// the leading icon is the one adjacent to the content
				QCOMPARE(geometry.iconRects.first().right(), geometry.contentRect.left() - metrics.contentGap - 1
																 - metrics.iconIconPadding);
			}
		}

		// Without a comment icon index there is never a comment rect.
		QStyleOptionViewItem option;
		option.rect      = QRect(7, 11, 300, 24);
		option.direction = Qt::LeftToRight;
		const UserStatusGeometry withoutComment = UserDelegate::computeGeometry(option, icons, -1, metrics);
		QVERIFY(!withoutComment.commentIconRect.isValid());
	}
}

void TestUserNavigation::geometryIconRectsAreDisjointInsideStrip() {
	UserStatusMetrics metrics;
	metrics.iconTotalDimension = 16;
	metrics.iconIconPadding    = 1;
	metrics.iconIconDimension  = 14;
	metrics.stripInset         = 3;
	metrics.contentGap         = 3;

	QStyleOptionViewItem option;
	option.rect      = QRect(0, 0, 400, 30);
	option.direction = Qt::LeftToRight;

	for (int count = 1; count <= 6; ++count) {
		QVariantList icons;
		for (int i = 0; i < count; ++i)
			icons << QVariant(otherStatusIcon());

		const UserStatusGeometry geometry = UserDelegate::computeGeometry(option, icons, -1, metrics);

		QCOMPARE(geometry.iconRects.count(), count);
		QVERIFY(geometry.stripRect.contains(geometry.iconRects.at(0)));
		QVERIFY(geometry.stripRect.contains(geometry.iconRects.at(count - 1)));
		for (int i = 0; i + 1 < count; ++i)
			QVERIFY(geometry.iconRects.at(i).right() < geometry.iconRects.at(i + 1).left());

		// The content area keeps the documented gap to the strip: no text
		// pixel can collide with an icon slot.
		QCOMPARE(geometry.contentRect.right() + metrics.contentGap + 1, geometry.stripRect.left());
		QVERIFY(geometry.iconRects.at(0).left() > geometry.contentRect.right());
	}
}

void TestUserNavigation::commentRectIsTheLeadingIconInView() {
	ViewHarness harness;

	const QStringList rowsWithComment = { QStringLiteral("Everything Channel"), QStringLiteral("Comment Only"),
										  QStringLiteral("Comment Plus 1"), QStringLiteral("Comment Plus 5"),
										  QStringLiteral("Self With Push To Mute") };

	for (const QString &name : rowsWithComment) {
		const QModelIndex idx = findRow(&harness.model, name);
		QVERIFY2(idx.isValid(), qPrintable(name));

		const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
		QVERIFY2(geometry.commentIconRect.isValid(), qPrintable(name));
		QCOMPARE(geometry.commentIconRect, geometry.iconRects.first());
	}

	// A listener proxy never reports a comment icon, although the
	// underlying user has a comment hash.
	const QModelIndex listenerIdx = findRow(&harness.model, QStringLiteral("Listener Proxy"));
	QVERIFY(listenerIdx.isValid());
	QVERIFY(!rowGeometry(harness.view, listenerIdx).commentIconRect.isValid());
}

void TestUserNavigation::clickClassificationMatchesPaintedGeometry() {
	ViewHarness harness;
	QSignalSpy clickedSpy(&harness.view, &QAbstractItemView::clicked);

	const QStringList rows = { QStringLiteral("Everything Channel"), QStringLiteral("Bare Channel"),
							   QStringLiteral("Plain User"),    QStringLiteral("Comment Only"),
							   QStringLiteral("Comment Plus 1"), QStringLiteral("Comment Plus 3"),
							   QStringLiteral("Comment Plus 5"), QStringLiteral("Self With Push To Mute"),
							   QStringLiteral("Max Statuses"),  QStringLiteral("Listener Proxy") };

	for (const QString &name : rows) {
		const QModelIndex idx = findRow(&harness.model, name);
		QVERIFY2(idx.isValid(), qPrintable(name));

		const QRect rowRect = harness.view.visualRect(idx);
		QVERIFY2(!rowRect.isEmpty(), qPrintable(name));

		const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
		const int centerY                = rowRect.center().y();

		for (int x = rowRect.left(); x <= rowRect.right(); ++x) {
			clickedSpy.clear();
			const QPoint position(x, centerY);
			QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier, position);

			const bool commentPixel =
				geometry.commentIconRect.isValid() && geometry.commentIconRect.contains(position);
			if (commentPixel) {
				// Consumed by the comment-icon handler: no base-class click.
				QCOMPARE(clickedSpy.count(), 0);
			} else {
				QCOMPARE(clickedSpy.count(), 1);
			}
		}
	}
}

void TestUserNavigation::helpEventForwardsExactlyTheIconStrip() {
	ViewHarness harness;

	UserDelegate *delegate = qobject_cast< UserDelegate * >(harness.view.itemDelegate());
	QVERIFY(delegate != nullptr);

	const QStringList rows = { QStringLiteral("Everything Channel"), QStringLiteral("Comment Plus 3"),
							   QStringLiteral("Listener Proxy"), QStringLiteral("Plain User"),
							   QStringLiteral("Bare Channel") };

	for (const QString &name : rows) {
		const QModelIndex idx = findRow(&harness.model, name);
		QVERIFY2(idx.isValid(), qPrintable(name));

		const QRect rowRect = harness.view.visualRect(idx);
		QVERIFY2(!rowRect.isEmpty(), qPrintable(name));

		const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
		const int centerY                = rowRect.center().y();

		QList< QPoint > positions;
		if (geometry.stripRect.isValid()) {
			positions << geometry.stripRect.center() << QPoint(geometry.stripRect.left() - 1, centerY);
		}
		positions << QPoint(rowRect.left() + 10, centerY);

		for (const QPoint &position : positions) {
			const int queriesBefore = harness.model.columnOneToolTipQueries();

			QHelpEvent event(QEvent::ToolTip, position, harness.view.viewport()->mapToGlobal(position));
			QStyleOptionViewItem option;
			option.rect      = rowRect;
			option.direction = harness.view.layoutDirection();
			const bool handled = delegate->helpEvent(&event, &harness.view, option, idx);

			const bool stripPixel = geometry.stripRect.isValid() && geometry.stripRect.contains(position);
			QCOMPARE(harness.model.columnOneToolTipQueries() > queriesBefore, stripPixel);

			if (stripPixel) {
				// The return value mirrors the column-1 tooltip data: an
				// empty flags tooltip (channels today) is not handled.
				const QString tooltip = idx.sibling(idx.row(), 1).data(Qt::ToolTipRole).toString();
				QCOMPARE(handled, !tooltip.isEmpty());
			} else {
				// The column-0 tooltip is the name: always handled.
				QCOMPARE(handled, true);
			}
		}
	}
}

void TestUserNavigation::channelStatusTooltipCoversAllStates() {
	// no filter mode, no restrictions: nothing to say
	QCOMPARE(UserModel::channelStatusTooltip(ChannelFilterMode::NORMAL, false, false), QString());
	QCOMPARE(UserModel::channelStatusTooltip(ChannelFilterMode::NORMAL, false, true), QString());

	// filter modes read back
	const QString filtered = UserModel::channelStatusTooltip(ChannelFilterMode::HIDE, false, false);
	const QString pinned   = UserModel::channelStatusTooltip(ChannelFilterMode::PIN, false, false);
	QVERIFY(!filtered.isEmpty());
	QVERIFY(!pinned.isEmpty());
	QVERIFY(filtered != pinned);

	// enter restrictions distinguish locked from enterable
	const QString locked    = UserModel::channelStatusTooltip(ChannelFilterMode::NORMAL, true, false);
	const QString enterable = UserModel::channelStatusTooltip(ChannelFilterMode::NORMAL, true, true);
	QVERIFY(!locked.isEmpty());
	QVERIFY(!enterable.isEmpty());
	QVERIFY(locked != enterable);

	// combined states contain both parts
	const QString combined = UserModel::channelStatusTooltip(ChannelFilterMode::PIN, true, false);
	QVERIFY(combined.contains(pinned));
	QVERIFY(combined.contains(locked));
}

void TestUserNavigation::flagsStringIncludesSuppressed() {
	ClientUser user;

	// no flags at all
	QCOMPARE(user.getFlagsString(), QString());

	// suppressed is part of the flags now (regression: it was missing while
	// the model already painted the suppressed icon)
	user.bSuppress = true;
	QVERIFY(user.getFlagsString().contains(ClientUser::tr("Muted (suppressed)")));

	// it sorts right after the server mute, matching the icon order
	user.bMute = true;
	const QStringList parts = user.getFlagsString().split(QLatin1String(", "));
	QCOMPARE(parts.count(), 2);
	QCOMPARE(parts.first(), ClientUser::tr("Muted (server)"));
	QCOMPARE(parts.last(), ClientUser::tr("Muted (suppressed)"));
}

void TestUserNavigation::iconStripMirrorsToVisualTrailingEdgeInRtl() {
	ViewHarness harness;

	const QModelIndex idx = findRow(&harness.model, QStringLiteral("Comment Plus 3"));
	QVERIFY(idx.isValid());

	// LTR control: the painted strip sits on the right side of the row.
	QRect rowRect                         = harness.view.visualRect(idx);
	QPair< int, int > ltrColoredPerPixelHalf = coloredPixelsPerSide(harness.view.viewport()->grab().toImage(), rowRect);
	QVERIFY2(ltrColoredPerPixelHalf.second > ltrColoredPerPixelHalf.first,
			 "status icons did not paint on the right side in LTR");
	QVERIFY2(ltrColoredPerPixelHalf.first + ltrColoredPerPixelHalf.second > 50,
			 "status icons did not paint at all");

	harness.view.setLayoutDirection(Qt::RightToLeft);
	drainEvents();

	rowRect = harness.view.visualRect(idx);
	QVERIFY(!rowRect.isEmpty());

	const UserStatusMetrics metrics  = harness.view.statusMetrics();
	const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
	QVERIFY(geometry.commentIconRect.isValid());

	// the strip moves to the visual trailing edge (the left), the content
	// area keeps the opposite edge, and the leading icon stays adjacent to
	// the content
	QCOMPARE(geometry.stripRect.left(), rowRect.left() + metrics.stripInset);
	QCOMPARE(geometry.contentRect.right(), rowRect.right());
	QCOMPARE(geometry.iconRects.first().right(), geometry.contentRect.left() - metrics.contentGap - 1
													 - metrics.iconIconPadding);

	// click classification follows the mirrored geometry
	QSignalSpy clickedSpy(&harness.view, &QAbstractItemView::clicked);
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier, geometry.commentIconRect.center());
	QCOMPARE(clickedSpy.count(), 0);

	clickedSpy.clear();
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier,
					  QPoint(geometry.contentRect.right() - 10, geometry.contentRect.center().y()));
	QCOMPARE(clickedSpy.count(), 1);

	// painted evidence: the colored icons now render on the left side.
	// (Reset selection/current first: the click above selected the row and
	// its blue highlight would drown the icon pixels.)
	harness.view.selectionModel()->reset();
	drainEvents();
	const QPair< int, int > rtlColoredPerPixelHalf =
		coloredPixelsPerSide(harness.view.viewport()->grab().toImage(), rowRect);
	QVERIFY2(rtlColoredPerPixelHalf.first > rtlColoredPerPixelHalf.second,
			 "status icons did not paint on the left side in RTL");
}

void TestUserNavigation::typographyRefinesPerRowKind() {
	const QFont base = QApplication::font();

	// channels: one weight step heavier, never lighter than the model font
	QCOMPARE(UserDelegate::refinedFont(base, UserModel::RowKind::Channel).weight(), QFont::DemiBold);
	QFont bold = base;
	bold.setBold(true);
	QCOMPARE(UserDelegate::refinedFont(bold, UserModel::RowKind::Channel).weight(), QFont::Bold);
	QFont light = base;
	light.setWeight(QFont::Light);
	QCOMPARE(UserDelegate::refinedFont(light, UserModel::RowKind::Channel).weight(), QFont::DemiBold);
	// other properties compose through untouched
	QFont italicChannel = base;
	italicChannel.setItalic(true);
	QVERIFY(UserDelegate::refinedFont(italicChannel, UserModel::RowKind::Channel).italic());

	// listeners: always italic; users: untouched
	QVERIFY(UserDelegate::refinedFont(base, UserModel::RowKind::Listener).italic());
	QFont italic = base;
	italic.setItalic(true);
	QVERIFY(UserDelegate::refinedFont(italic, UserModel::RowKind::Listener).italic());
	QCOMPARE(UserDelegate::refinedFont(base, UserModel::RowKind::User), base);

	// color roles: listeners dim when unselected, everything else regular
	QCOMPARE(UserDelegate::textColorRole(UserModel::RowKind::User, false), QPalette::Text);
	QCOMPARE(UserDelegate::textColorRole(UserModel::RowKind::User, true), QPalette::HighlightedText);
	QCOMPARE(UserDelegate::textColorRole(UserModel::RowKind::Channel, false), QPalette::Text);
	QCOMPARE(UserDelegate::textColorRole(UserModel::RowKind::Listener, false), QPalette::PlaceholderText);
	QCOMPARE(UserDelegate::textColorRole(UserModel::RowKind::Listener, true), QPalette::HighlightedText);
}

void TestUserNavigation::focusRingRendersWhenFocused() {
	ViewHarness harness;

	UserDelegate *delegate = qobject_cast< UserDelegate * >(harness.view.itemDelegate());
	QVERIFY(delegate != nullptr);

	const QModelIndex idx = findRow(&harness.model, QStringLiteral("Comment Plus 3"));
	QVERIFY(idx.isValid());
	const QRect rowRect = harness.view.visualRect(idx);
	QVERIFY(!rowRect.isEmpty());

	// render the same row with and without keyboard focus: the focus ring
	// must add visible pixels (the old custom paint dropped it entirely)
	auto render = [&](bool focused) {
		QImage image(rowRect.size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::white);
		QPainter painter(&image);

		QStyleOptionViewItem option;
		option.rect      = QRect(QPoint(0, 0), rowRect.size());
		option.widget    = &harness.view;
		option.state    |= QStyle::State_Enabled | QStyle::State_Active;
		if (focused)
			option.state |= QStyle::State_HasFocus;

		delegate->paint(&painter, option, idx);
		painter.end();
		return image;
	};

	const QImage plain    = render(false);
	const QImage focused  = render(true);
	QVERIFY(focused != plain);
}

void TestUserNavigation::iconMetricsTrackLargeFontsAtConstruction() {
	FakeNavigationModel model;
	UserView view(nullptr);
	view.setModel(&model);
	view.setUniformRowHeights(true);

	QFont largeFont = view.font();
	largeFont.setPointSizeF(largeFont.pointSizeF() * 2);
	view.setFont(largeFont);

	view.resize(420, 480);
	view.show();
	drainEvents();
	view.expandAll();
	drainEvents();

	// the whole metric set is derived from the font the view settled on
	const UserStatusMetrics metrics = view.statusMetrics();
	const QFontMetrics fontMetrics(view.font());
	QCOMPARE(metrics.iconTotalDimension, fontMetrics.height());
	QCOMPARE(metrics.iconIconPadding, 1);
	QCOMPARE(metrics.iconIconDimension, fontMetrics.height() - 2);
	const int expectedSpacing = qBound(2, fontMetrics.horizontalAdvance(QStringLiteral("n")) / 2, 8);
	QCOMPARE(metrics.stripInset, expectedSpacing);
	QCOMPARE(metrics.contentGap, expectedSpacing);

	const QModelIndex idx = findRow(&model, QStringLiteral("Comment Plus 5"));
	QVERIFY(idx.isValid());
	const UserStatusGeometry geometry = rowGeometry(view, idx);
	QCOMPARE(geometry.stripRect.width(), 6 * metrics.iconTotalDimension);
}

void TestUserNavigation::iconMetricsTrackRuntimeFontChange() {
	ViewHarness harness;

	const int initialDimension = harness.view.statusIconDimension();
	QCOMPARE(initialDimension, QFontMetrics(harness.view.font()).height());

	const QModelIndex idx = findRow(&harness.model, QStringLiteral("Comment Only"));
	QVERIFY(idx.isValid());
	const UserStatusGeometry before = rowGeometry(harness.view, idx);
	QVERIFY(before.commentIconRect.isValid());

	// Sanity: the comment pixel is consumed at the initial geometry.
	QSignalSpy clickedSpy(&harness.view, &QAbstractItemView::clicked);
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier, before.commentIconRect.center());
	QCOMPARE(clickedSpy.count(), 0);

	QFont larger = harness.view.font();
	larger.setPointSizeF(larger.pointSizeF() * 2);
	harness.view.setFont(larger);
	drainEvents();

	// The deferred refresh re-derived the metrics from the new font...
	QCOMPARE(harness.view.statusIconDimension(), QFontMetrics(larger).height());
	QVERIFY(harness.view.statusIconDimension() > initialDimension);

	// ...and the hit-test geometry follows it.
	const UserStatusGeometry after = rowGeometry(harness.view, idx);
	QVERIFY(after.commentIconRect.isValid());
	QCOMPARE(after.stripRect.width(), harness.view.statusIconDimension());
	QVERIFY(after.commentIconRect != before.commentIconRect);

	clickedSpy.clear();
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier, after.commentIconRect.center());
	QCOMPARE(clickedSpy.count(), 0);

	clickedSpy.clear();
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier,
					  QPoint(after.contentRect.left() + 10, after.contentRect.center().y()));
	QCOMPARE(clickedSpy.count(), 1);
}

void TestUserNavigation::styleChangeRefreshesIconMetrics() {
	ViewHarness harness;

	const int dimension = harness.view.statusIconDimension();
	QCOMPARE(dimension, QFontMetrics(harness.view.font()).height());

	// An actual style change: a genuinely different factory key when the
	// platform has one; a platform that ships only a single style falls back
	// to a fresh instance of that key - still a new style object on the
	// widget, not the same pointer re-assigned. QWidget::setStyle does not
	// take ownership, so the view must be handed back to the application
	// style before the temporary object dies.
	const QString currentStyle = harness.view.style()->objectName();
	QString styleKey          = currentStyle;
	for (const QString &key : QStyleFactory::keys()) {
		if (key.compare(currentStyle, Qt::CaseInsensitive) != 0) {
			styleKey = key;
			break;
		}
	}

	const std::unique_ptr< QStyle > other(QStyleFactory::create(styleKey));
	if (!other) {
		QSKIP("QStyleFactory cannot create a style on this platform");
	}

	harness.view.setStyle(other.get());
	drainEvents();

	// The real switch must leave the view consistent (no crash, metrics
	// still derived from the font).
	QCOMPARE(harness.view.statusIconDimension(), QFontMetrics(harness.view.font()).height());

	// Decisive regression assertion for the StyleChange branch itself: a
	// real switch also triggers Qt's own repolish updates, so isolate the
	// handler's deferred refresh by delivering the event directly and
	// observing the update request it schedules. adjustIcons() paints into
	// the viewport, but QWidget::update() delivers the UpdateRequest event
	// to the top-level widget - so watch both.
	UpdateRequestCounter counter;
	harness.view.installEventFilter(&counter);
	harness.view.viewport()->installEventFilter(&counter);
	drainEvents();
	const int settled = counter.count;

	QEvent styleChange(QEvent::StyleChange);
	QApplication::sendEvent(&harness.view, &styleChange);
	drainEvents();

	QVERIFY2(counter.count > settled, "StyleChange did not schedule an icon-metrics refresh");
	QCOMPARE(harness.view.statusIconDimension(), QFontMetrics(harness.view.font()).height());

	// Restore the application style BEFORE the temporary style object dies:
	// a pending polish or paint after this slot must not run through the
	// freed pointer. The application style is owned by QApplication and
	// outlives the view.
	harness.view.setStyle(QApplication::style());
	drainEvents();
	QCOMPARE(harness.view.statusIconDimension(), QFontMetrics(harness.view.font()).height());

	harness.view.removeEventFilter(&counter);
	harness.view.viewport()->removeEventFilter(&counter);
}

void TestUserNavigation::indentationFollowsFontMetrics() {
	ViewHarness harness;

	const auto expected = [](const QFont &font) { return qBound(12, QFontMetrics(font).height() + 4, 40); };
	QCOMPARE(harness.view.indentation(), expected(harness.view.font()));

	QFont larger = harness.view.font();
	larger.setPointSizeF(larger.pointSizeF() * 2);
	harness.view.setFont(larger);
	drainEvents();

	QCOMPARE(harness.view.indentation(), expected(larger));
}

void TestUserNavigation::narrowWidthShrinksContentAreaByStrip() {
	ViewHarness harness;

	const QModelIndex idx = findRow(&harness.model, QStringLiteral("Comment Plus 5"));
	QVERIFY(idx.isValid());

	// A width where the whole strip still fits (computed from the live
	// metrics and the row's actual left offset - the fixture rows sit two
	// indentation levels deep - so the assertion holds for whatever font
	// the tests run with): the content area shrinks by exactly the strip.
	const UserStatusMetrics metrics = harness.view.statusMetrics();
	const int iconCount             = 6;
	const int rowLeft               = harness.view.visualRect(idx).left();
	const int fittingWidth          = rowLeft + iconCount * metrics.iconTotalDimension + metrics.stripInset
									   + metrics.contentGap + 40;
	harness.view.resize(fittingWidth, 480);
	drainEvents();

	const QRect rowRect = harness.view.visualRect(idx);
	QVERIFY(!rowRect.isEmpty());

	const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
	QCOMPARE(geometry.stripRect.width(), iconCount * metrics.iconTotalDimension);
	QCOMPARE(geometry.contentRect.width(),
			 rowRect.width() - geometry.stripRect.width() - metrics.stripInset - metrics.contentGap);
	QCOMPARE(geometry.contentRect.right() + metrics.contentGap + 1, geometry.stripRect.left());

	// What happens once the row gets narrower than the strip is the
	// overflow contract, covered by narrowGeometryKeepsOnlyLeadingIcons and
	// narrowRowsStayConsistentUnderOverflow below.
}

void TestUserNavigation::narrowGeometryKeepsOnlyLeadingIcons() {
	UserStatusMetrics metrics;
	metrics.iconTotalDimension = 16;
	metrics.iconIconPadding    = 1;
	metrics.iconIconDimension  = 14;
	metrics.stripInset         = 3;
	metrics.contentGap         = 3;

	// A large-font metric set: double slot pitch, so far fewer icons fit
	// into the same widths.
	UserStatusMetrics largeFont  = metrics;
	largeFont.iconTotalDimension = 30;
	largeFont.iconIconDimension  = 28;

	for (const UserStatusMetrics &metricSet : { metrics, largeFont }) {
		for (const int width : { 8, 20, 36, 60, 100, 180, 300 }) {
			for (int count = 1; count <= 16; count += (count < 4 ? 1 : 5)) {
				for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft }) {
					QVariantList icons;
					for (int i = 0; i < count; ++i)
						icons << QVariant(i == 0 ? commentStatusIcon() : otherStatusIcon());

					QStyleOptionViewItem option;
					option.rect      = QRect(5, 9, width, 30);
					option.direction = direction;

					const UserStatusGeometry geometry = UserDelegate::computeGeometry(option, icons, 0, metricSet);

					const int expectedVisible =
						qBound(0, (width - metricSet.stripInset) / metricSet.iconTotalDimension, count);

					// whatever the overflow: no negative or row-escaping rectangle
					QVERIFY(geometry.contentRect.width() >= 0);
					QVERIFY(option.rect.contains(geometry.contentRect));
					QCOMPARE(geometry.iconRects.count(), expectedVisible);

					if (expectedVisible == 0) {
						// not even one slot fits: icon-less-row behavior
						QVERIFY(!geometry.stripRect.isValid());
						QVERIFY(!geometry.commentIconRect.isValid());
						QCOMPARE(geometry.contentRect, option.rect);
						continue;
					}

					QVERIFY(option.rect.contains(geometry.stripRect));
					QCOMPARE(geometry.stripRect.width(), expectedVisible * metricSet.iconTotalDimension);
					for (const QRect &iconRect : geometry.iconRects)
						QVERIFY(option.rect.contains(iconRect));

					// the comment indicator is the leading icon: it survives
					// every truncation and stays adjacent to the content
					QVERIFY(geometry.commentIconRect.isValid());
					QCOMPARE(geometry.commentIconRect, geometry.iconRects.first());
					if (direction == Qt::LeftToRight) {
						QCOMPARE(geometry.iconRects.first().left(),
								 geometry.stripRect.left() + metricSet.iconIconPadding);
					} else {
						QCOMPARE(geometry.iconRects.first().right(),
								 geometry.stripRect.left() + geometry.stripRect.width()
									 - metricSet.iconTotalDimension + metricSet.iconIconPadding
									 + metricSet.iconIconDimension - 1);
					}
				}
			}
		}
	}
}

void TestUserNavigation::narrowRowsStayConsistentUnderOverflow() {
	ViewHarness harness;

	UserDelegate *delegate = qobject_cast< UserDelegate * >(harness.view.itemDelegate());
	QVERIFY(delegate != nullptr);

	// Two overflowing rows: the maximal 13-icon strip and a 6-icon one, both
	// with a leading comment indicator.
	const QStringList rowNames = { QStringLiteral("Max Statuses"), QStringLiteral("Comment Plus 5") };
	QList< QPair< QModelIndex, int > > rows;
	for (const QString &name : rowNames) {
		const QModelIndex idx = findRow(&harness.model, name);
		QVERIFY2(idx.isValid(), qPrintable(name));
		const int iconCount = static_cast< int >(idx.sibling(idx.row(), 1).data(Qt::DisplayRole).toList().count());
		QVERIFY(iconCount > 0);
		rows.append(qMakePair(idx, iconCount));
	}

	QSignalSpy clickedSpy(&harness.view, &QAbstractItemView::clicked);

	// Resize to a width that cannot hold the row's full strip (guaranteed
	// overflow whatever font the tests run under), typically landing on the
	// requested pane size: 180 px for the many-icon pane, 60 px for the
	// pathological case.
	auto resizeOverflowing = [&](int iconCount, int typicalWidth) {
		const UserStatusMetrics metrics = harness.view.statusMetrics();
		const int fullStrip             = iconCount * metrics.iconTotalDimension + metrics.stripInset;
		QVERIFY(fullStrip > 1);
		harness.view.resize(qMin(fullStrip - 1, qMax(typicalWidth, fullStrip / 2)), 480);
		drainEvents();
	};

	// The overflow contract for the row at the view's current state: the
	// geometry truncates to the icons that fit, stays inside the row, keeps
	// the comment indicator usable and answers the flags tooltip where the
	// strip is painted.
	auto verifyOverflow = [&](const QModelIndex &idx, int iconCount, const char *context) {
		const QRect rowRect = harness.view.visualRect(idx);
		QVERIFY2(!rowRect.isEmpty(), context);

		const UserStatusMetrics metrics = harness.view.statusMetrics();
		const int expectedVisible =
			qBound(0, (rowRect.width() - metrics.stripInset) / metrics.iconTotalDimension, iconCount);
		QVERIFY2(expectedVisible < iconCount, "this width does not actually overflow the strip");

		const UserStatusGeometry geometry = rowGeometry(harness.view, idx);

		QVERIFY2(geometry.contentRect.width() >= 0, context);
		QVERIFY2(rowRect.contains(geometry.contentRect), context);
		QCOMPARE(geometry.iconRects.count(), expectedVisible);
		for (const QRect &iconRect : geometry.iconRects)
			QVERIFY2(rowRect.contains(iconRect), context);

		// the comment indicator survives truncation (it is the leading icon)
		QVERIFY2(geometry.commentIconRect.isValid(), context);
		QCOMPARE(geometry.commentIconRect, geometry.iconRects.first());
		QVERIFY(rowRect.contains(geometry.commentIconRect));

		// and it stays clickable at the truncated geometry
		clickedSpy.clear();
		QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier,
						  geometry.commentIconRect.center());
		QCOMPARE(clickedSpy.count(), 0);

		// strip pixels still answer the column-1 flags tooltip where the
		// truncated strip is painted
		const int queriesBefore = harness.model.columnOneToolTipQueries();
		QHelpEvent event(QEvent::ToolTip, geometry.stripRect.center(),
						 harness.view.viewport()->mapToGlobal(geometry.stripRect.center()));
		QStyleOptionViewItem option;
		option.rect      = rowRect;
		option.direction = harness.view.layoutDirection();
		QVERIFY(delegate->helpEvent(&event, &harness.view, option, idx));
		QVERIFY(harness.model.columnOneToolTipQueries() > queriesBefore);
	};

	// Painted evidence: the blue comment icon renders inside its computed
	// slot and no icon-colored pixel leaves the row band.
	auto verifyPainted = [&](const QModelIndex &idx, const char *context) {
		harness.view.selectionModel()->reset();
		drainEvents();

		const QRect rowRect                = harness.view.visualRect(idx);
		const UserStatusGeometry geometry  = rowGeometry(harness.view, idx);
		QVERIFY2(geometry.commentIconRect.isValid(), context);

		const QImage image = harness.view.viewport()->grab().toImage();
		int bluePixels     = 0;
		int misplacedBlue  = 0;
		int outsideRow     = 0;
		for (int y = rowRect.top(); y <= rowRect.bottom(); ++y) {
			for (int x = 0; x < image.width(); ++x) {
				const QColor color(image.pixel(x, y));
				const bool blue = (color.blueF() > 0.4 && color.blueF() > color.redF() + 0.2
								   && color.blueF() > color.greenF() + 0.2);
				const bool red  = (color.redF() > 0.4 && color.redF() > color.blueF() + 0.2
								   && color.redF() > color.greenF() + 0.2);
				if (!blue && !red)
					continue;
				if (x < rowRect.left() || x > rowRect.right())
					++outsideRow;
				if (blue) {
					++bluePixels;
					if (x < geometry.commentIconRect.left() - 1 || x > geometry.commentIconRect.right() + 1)
						++misplacedBlue;
				}
			}
		}
		QVERIFY2(bluePixels > 0, context);
		QVERIFY2(outsideRow == 0, context);
		QVERIFY2(misplacedBlue == 0, context);
	};

	// 180 px tree pane with many simultaneous status icons
	for (const auto &row : rows) {
		resizeOverflowing(row.second, 180);
		verifyOverflow(row.first, row.second, "180px LTR");
		verifyPainted(row.first, "180px LTR");
	}

	// pathological 60 px width
	for (const auto &row : rows) {
		resizeOverflowing(row.second, 60);
		verifyOverflow(row.first, row.second, "60px LTR");
		verifyPainted(row.first, "60px LTR");
	}

	// doubled fonts on the 180 px pane
	QFont larger = harness.view.font();
	larger.setPointSizeF(larger.pointSizeF() * 2);
	harness.view.setFont(larger);
	drainEvents();
	resizeOverflowing(rows.first().second, 180);
	verifyOverflow(rows.first().first, rows.first().second, "180px large font");
	verifyPainted(rows.first().first, "180px large font");

	// right-to-left at both widths
	harness.view.setFont(QApplication::font());
	drainEvents();
	harness.view.setLayoutDirection(Qt::RightToLeft);
	drainEvents();
	resizeOverflowing(rows.first().second, 180);
	verifyOverflow(rows.first().first, rows.first().second, "180px RTL");
	verifyPainted(rows.first().first, "180px RTL");
	resizeOverflowing(rows.first().second, 60);
	verifyOverflow(rows.first().first, rows.first().second, "60px RTL");
}

QTEST_MAIN(TestUserNavigation)
#include "TestUserNavigation.moc"
