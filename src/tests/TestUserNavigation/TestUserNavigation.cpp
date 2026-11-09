// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file at the root of the Mumble source tree or at
// <https://www.mumble.info/LICENSE>.

// Tests for the shared status-icon geometry of the channel/user tree
// (Stage 3 of the new UI, commit 1). UserModel's real data() path requires a
// Global/database/server environment, so the view/delegate integration runs
// against a synthetic model mirroring UserModel's observable presentation
// protocol. What these fixtures prove: one geometry drives painting bounds,
// tooltip forwarding and click hit-testing; the comment hitbox sits on the
// painted comment icon for every status combination (including the
// push-to-mute and listener-proxy rows the old flag-based reconstruction got
// wrong); and icon metrics track font changes at construction and at runtime.
// Server-driven behavior (comments arriving, seen-state, talking state)
// stays on the live checklist.

#include <QtTest>

#include "UserModel.h"
#include "UserView.h"

#include <QApplication>
#include <QEvent>
#include <QHelpEvent>
#include <QIcon>
#include <QModelIndex>
#include <QObject>
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
//  - CommentIconRole: whether the leading status icon is the comment.
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
		// + lock. Channels currently have no column-1 flags tooltip.
		Node *channel        = addNode(m_root, QStringLiteral("Everything Channel"), RowType::Channel);
		channel->statusIcons = { commentStatusIcon(), otherStatusIcon(), otherStatusIcon() };
		channel->commentIcon = true;

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
	// The shared calculation must reproduce the pre-refactor inline math of
	// paint()/helpEvent() exactly (pixel-identical rendering).
	void geometryPreservesLegacyFormulas();
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

	// Commit 1 pins the existing RTL behavior (strip anchored to the
	// logical right edge); the visual pass flips it later.
	void iconStripStaysRightAnchoredInRtl();

	void iconMetricsTrackLargeFontsAtConstruction();
	void iconMetricsTrackRuntimeFontChange();
	void styleChangeRefreshesIconMetrics();
	void narrowWidthShrinksContentAreaByStrip();

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

	/// The geometry the production hit-test path computes for a row: driven
	/// by the model-provided icon list, the CommentIconRole and the view's
	/// current icon metrics (padding 1, dimension total-2, as adjustIcons
	/// derives them).
	static UserStatusGeometry rowGeometry(const UserView &view, const QModelIndex &idx) {
		QStyleOptionViewItem option;
		option.rect      = view.visualRect(idx);
		option.direction = view.layoutDirection();

		const QList< QVariant > icons = idx.sibling(idx.row(), 1).data(Qt::DisplayRole).toList();
		const bool commentIcon        = idx.data(UserModel::CommentIconRole).toBool();
		const int total               = view.statusIconDimension();

		return UserDelegate::computeGeometry(option, icons, commentIcon ? 0 : -1, total, 1, total - 2);
	}
};

void TestUserNavigation::geometryPreservesLegacyFormulas() {
	const int total     = 16;
	const int padding   = 1;
	const int dimension = 14;

	for (int count = 0; count <= 6; ++count) {
		QStyleOptionViewItem option;
		option.rect      = QRect(7, 11, 300, 24);
		option.direction = Qt::LeftToRight;

		QVariantList icons;
		for (int i = 0; i < count; ++i)
			icons << QVariant(commentStatusIcon());

		const UserStatusGeometry geometry =
			UserDelegate::computeGeometry(option, icons, count > 0 ? 0 : -1, total, padding, dimension);

		// The pre-refactor inline math of paint()/helpEvent().
		const QRect legacyStrip = QRect(option.rect.right() - count * total, option.rect.y(), count * total,
										option.rect.height());

		if (count == 0) {
			QVERIFY(!geometry.stripRect.isValid());
			QVERIFY(geometry.iconRects.isEmpty());
			QVERIFY(!geometry.commentIconRect.isValid());
			QCOMPARE(geometry.contentRect, option.rect);
		} else {
			QCOMPARE(geometry.stripRect, legacyStrip);
			QCOMPARE(geometry.contentRect, option.rect.adjusted(0, 0, -(count * total), 0));

			const int legacyIconPosY = (option.rect.height() / 2) - (dimension / 2);
			for (int i = 0; i < count; ++i) {
				QRect legacyIcon = legacyStrip;
				legacyIcon.setSize(QSize(dimension, dimension));
				legacyIcon.translate(i * total + padding, legacyIconPosY);
				// The pre-refactor paint() wrapped this rect in
				// QStyle::alignedRect with a cell of equal size - an
				// identity operation - so the plain rect is exact.
				QCOMPARE(geometry.iconRects.at(i), legacyIcon);
			}

			QCOMPARE(geometry.commentIconRect, geometry.iconRects.first());
		}

		// Without a comment icon index there is never a comment rect.
		const UserStatusGeometry withoutComment =
			UserDelegate::computeGeometry(option, icons, -1, total, padding, dimension);
		QVERIFY(!withoutComment.commentIconRect.isValid());
	}
}

void TestUserNavigation::geometryIconRectsAreDisjointInsideStrip() {
	const int total     = 16;
	const int padding   = 1;
	const int dimension = 14;

	QStyleOptionViewItem option;
	option.rect      = QRect(0, 0, 400, 30);
	option.direction = Qt::LeftToRight;

	for (int count = 1; count <= 6; ++count) {
		QVariantList icons;
		for (int i = 0; i < count; ++i)
			icons << QVariant(otherStatusIcon());

		const UserStatusGeometry geometry =
			UserDelegate::computeGeometry(option, icons, -1, total, padding, dimension);

		QCOMPARE(geometry.iconRects.count(), count);
		QVERIFY(geometry.stripRect.contains(geometry.iconRects.at(0)));
		QVERIFY(geometry.stripRect.contains(geometry.iconRects.at(count - 1)));
		for (int i = 0; i + 1 < count; ++i)
			QVERIFY(geometry.iconRects.at(i).right() < geometry.iconRects.at(i + 1).left());

		// Legacy relation preserved: the content rect ends on the same pixel
		// column the strip starts on (adjusted(0, 0, -w, 0) vs. the
		// right()-anchored strip); the painted icons begin one padding
		// further in, so text and icons never visually overlap.
		QCOMPARE(geometry.contentRect.right(), geometry.stripRect.left());
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
							   QStringLiteral("Listener Proxy") };

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

void TestUserNavigation::iconStripStaysRightAnchoredInRtl() {
	ViewHarness harness;
	harness.view.setLayoutDirection(Qt::RightToLeft);
	drainEvents();

	const QModelIndex idx = findRow(&harness.model, QStringLiteral("Comment Plus 3"));
	QVERIFY(idx.isValid());

	const QRect rowRect               = harness.view.visualRect(idx);
	const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
	QVERIFY(!rowRect.isEmpty());
	QVERIFY(geometry.commentIconRect.isValid());

	// Commit 1 preserves the pre-existing anchoring: the strip sits at the
	// logical right edge of the row rectangle - one pixel short of the
	// edge, exactly where the legacy QRect math placed it in every layout
	// direction. (Commit 2 mirrors strip and content rect for RTL.)
	QCOMPARE(geometry.stripRect.right(), rowRect.right() - 1);

	QSignalSpy clickedSpy(&harness.view, &QAbstractItemView::clicked);
	clickedSpy.clear();
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier, geometry.commentIconRect.center());
	QCOMPARE(clickedSpy.count(), 0);

	clickedSpy.clear();
	QTest::mouseClick(harness.view.viewport(), Qt::LeftButton, Qt::NoModifier,
					  QPoint(geometry.contentRect.left() + 10, geometry.contentRect.center().y()));
	QCOMPARE(clickedSpy.count(), 1);
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

	QCOMPARE(view.statusIconDimension(), QFontMetrics(view.font()).height());

	const QModelIndex idx = findRow(&model, QStringLiteral("Comment Plus 5"));
	QVERIFY(idx.isValid());
	const UserStatusGeometry geometry = rowGeometry(view, idx);
	QCOMPARE(geometry.stripRect.width(), 6 * view.statusIconDimension());
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

	// An actual style change: a genuinely different style, not the same
	// pointer re-assigned (QWidget::setStyle does not take ownership).
	const QString currentStyle = harness.view.style()->objectName();
	QString alternativeStyle;
	for (const QString &key : QStyleFactory::keys()) {
		if (key.compare(currentStyle, Qt::CaseInsensitive) != 0) {
			alternativeStyle = key;
			break;
		}
	}
	QVERIFY2(!alternativeStyle.isEmpty(), "no alternative style available on this platform");

	{
		const std::unique_ptr< QStyle > other(QStyleFactory::create(alternativeStyle));
		QVERIFY(other != nullptr);
		harness.view.setStyle(other.get());
		drainEvents();
	}

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

	harness.view.removeEventFilter(&counter);
	harness.view.viewport()->removeEventFilter(&counter);
}

void TestUserNavigation::narrowWidthShrinksContentAreaByStrip() {
	ViewHarness harness;
	harness.view.resize(140, 480);
	drainEvents();

	const QModelIndex idx = findRow(&harness.model, QStringLiteral("Comment Plus 5"));
	QVERIFY(idx.isValid());

	const QRect rowRect = harness.view.visualRect(idx);
	QVERIFY(!rowRect.isEmpty());

	const int iconCount = 6;
	const UserStatusGeometry geometry = rowGeometry(harness.view, idx);
	QCOMPARE(geometry.stripRect.width(), iconCount * harness.view.statusIconDimension());
	QCOMPARE(geometry.contentRect.width(), rowRect.width() - geometry.stripRect.width());
	QCOMPARE(geometry.contentRect.right(), geometry.stripRect.left());

	// Degenerate narrow case: the row is narrower than the strip itself.
	// The formula stays consistent (the content width just goes negative),
	// so text elision input and hit-testing remain derived from the same
	// rects.
	harness.view.resize(60, 480);
	drainEvents();

	const QRect narrowRect = harness.view.visualRect(idx);
	QVERIFY(!narrowRect.isEmpty());
	const UserStatusGeometry narrowGeometry = rowGeometry(harness.view, idx);
	QCOMPARE(narrowGeometry.contentRect.width(), narrowRect.width() - narrowGeometry.stripRect.width());
}

QTEST_MAIN(TestUserNavigation)
#include "TestUserNavigation.moc"
