#include "transformwatcher.hpp"

#include <qcontainerfwd.h>
#include <qdebug.h>
#include <qlist.h>
#include <qlogging.h>
#include <qobject.h>
#include <qquickitem.h>
#include <qquickwindow.h>

void TransformWatcher::resolveChains(QQuickItem* a, QQuickItem* b, QQuickItem* commonParent) {
	if (a == nullptr || b == nullptr) return;

	auto aChain = QVector<QQuickItem*>();
	auto bChain = QVector<QQuickItem*>();

	auto* aParent = a;
	auto* bParent = b;

	// resolve the parent chain of b. if a is in the chain break early
	while (bParent != nullptr) {
		bChain.push_back(bParent);

		if (bParent == a) {
			aChain.push_back(a);
			goto chainResolved;
		}

		if (bParent == commonParent) break;
		bParent = bParent->parentItem();
	}

	// resolve the parent chain of a, breaking as soon as b is found
	while (aParent != nullptr) {
		aChain.push_back(aParent);

		for (auto bParent = bChain.begin(); bParent != bChain.end(); bParent++) {
			if (*bParent == aParent) {
				bParent++;
				bChain.erase(bParent, bChain.end());
				goto chainResolved;
			}
		}

		if (aParent == commonParent) break;
		aParent = aParent->parentItem();
	}

	if (commonParent != nullptr && aParent == commonParent) {
		qWarning() << this << "failed to find a common parent between" << a << "and" << b
		           << "due to incorrectly set commonParent" << commonParent;

		return;
	}

chainResolved:

	this->parentChain = aChain;
	if (bChain.last() == aChain.last()) bChain.removeLast();
	this->childChain = bChain;

	if (a->window() != b->window()) {
		this->parentWindow = a->window();
		this->childWindow = b->window();
	} else {
		this->parentWindow = nullptr;
		this->childWindow = nullptr;
	}
}

void TransformWatcher::resolveChains() {
	this->resolveChains(this->mA, this->mB, this->mCommonParent);
}

void TransformWatcher::linkItem(QQuickItem* item) const {
	QObject::connect(item, &QQuickItem::xChanged, this, &TransformWatcher::transformChanged);
	QObject::connect(item, &QQuickItem::yChanged, this, &TransformWatcher::transformChanged);
	QObject::connect(item, &QQuickItem::widthChanged, this, &TransformWatcher::transformChanged);
	QObject::connect(item, &QQuickItem::heightChanged, this, &TransformWatcher::transformChanged);
	QObject::connect(item, &QQuickItem::scaleChanged, this, &TransformWatcher::transformChanged);
	QObject::connect(item, &QQuickItem::rotationChanged, this, &TransformWatcher::transformChanged);

	QObject::connect(item, &QQuickItem::parentChanged, this, &TransformWatcher::recalcChains);
	QObject::connect(item, &QQuickItem::windowChanged, this, &TransformWatcher::recalcChains);
	QObject::connect(item, &QObject::destroyed, this, &TransformWatcher::recalcChains);
}

void TransformWatcher::linkChains() {
	for (auto* item: this->parentChain) {
		this->linkItem(item);
	}

	for (auto* item: this->childChain) {
		this->linkItem(item);
	}
}

void TransformWatcher::unlinkChains() {
	for (auto* item: this->parentChain) {
		QObject::disconnect(item, nullptr, this, nullptr);
	}

	for (auto* item: this->childChain) {
		QObject::disconnect(item, nullptr, this, nullptr);
	}
}

void TransformWatcher::recalcChains() {
	this->unlinkChains();
	this->resolveChains();
	this->linkChains();
}

QQuickItem* TransformWatcher::a() const { return this->mA; }

void TransformWatcher::setA(QQuickItem* a) {
	if (this->mA == a) return;
	this->mA = a;
	this->recalcChains();
}

QQuickItem* TransformWatcher::b() const { return this->mB; }

void TransformWatcher::setB(QQuickItem* b) {
	if (this->mB == b) return;
	this->mB = b;
	this->recalcChains();
}

QQuickItem* TransformWatcher::commonParent() const { return this->mCommonParent; }

void TransformWatcher::setCommonParent(QQuickItem* commonParent) {
	if (this->mCommonParent == commonParent) return;
	this->mCommonParent = commonParent;
	this->resolveChains();
}
