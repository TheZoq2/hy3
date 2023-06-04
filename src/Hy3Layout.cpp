#include "globals.hpp"
#include "Hy3Layout.hpp"
#include "SelectionHook.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <iostream>

#include <sstream>

void errorNotif() {
	HyprlandAPI::addNotificationV2(PHANDLE, {
		{"text", "Something has gone very wrong. Check the log for details."},
		{"time", (uint64_t)10000},
		{"color", CColor(1.0, 0.0, 0.0, 1.0)},
		{"icon", ICON_ERROR},
	});
}

Hy3GroupData::Hy3GroupData(Hy3GroupLayout layout): layout(layout) {}

Hy3NodeData::Hy3NodeData(): Hy3NodeData((CWindow*)nullptr) {}

Hy3NodeData::Hy3NodeData(CWindow *window): type(Hy3NodeData::Window) {
	this->as_window = window;
}

Hy3NodeData::Hy3NodeData(Hy3GroupData group): type(Hy3NodeData::Group) {
	new(&this->as_group) Hy3GroupData(std::move(group));
}

Hy3NodeData::Hy3NodeData(Hy3GroupLayout layout): Hy3NodeData(Hy3GroupData(layout)) {}

Hy3NodeData::~Hy3NodeData() {
	switch (this->type) {
	case Hy3NodeData::Window:
		break;
	case Hy3NodeData::Group:
		this->as_group.~Hy3GroupData();

		// who ever thought calling the dtor after a move was a good idea?
		this->type = Hy3NodeData::Window;
		break;
	}
}

Hy3NodeData::Hy3NodeData(const Hy3NodeData& from): type(from.type) {
	Debug::log(LOG, "Copy CTor type matches? %d is group? %d", this->type == from.type, this->type == Hy3NodeData::Group);
	switch (from.type) {
	case Hy3NodeData::Window:
		this->as_window = from.as_window;
		break;
	case Hy3NodeData::Group:
		new(&this->as_group) Hy3GroupData(from.as_group);
		break;
	}
}

Hy3NodeData::Hy3NodeData(Hy3NodeData&& from): type(from.type) {
	Debug::log(LOG, "Move CTor type matches? %d is group? %d", this->type == from.type, this->type == Hy3NodeData::Group);
	switch (from.type) {
	case Hy3NodeData::Window:
		this->as_window = from.as_window;
		break;
	case Hy3NodeData::Group:
		new(&this->as_group) Hy3GroupData(std::move(from.as_group));
		break;
	}
}

Hy3NodeData& Hy3NodeData::operator=(const Hy3NodeData& from) {
	Debug::log(LOG, "operator= type matches? %d is group? %d", this->type == from.type, this->type == Hy3NodeData::Group);
	if (this->type == Hy3NodeData::Group) {
		this->as_group.~Hy3GroupData();
	}

	this->type = from.type;

	switch (this->type) {
	case Hy3NodeData::Window:
		this->as_window = from.as_window;
		break;
	case Hy3NodeData::Group:
		new(&this->as_group) Hy3GroupData(from.as_group);
		break;
	}

	return *this;
}

Hy3NodeData& Hy3NodeData::operator=(CWindow* window) {
	*this = Hy3NodeData(window);

	return *this;
}

Hy3NodeData& Hy3NodeData::operator=(Hy3GroupLayout layout) {
	*this = Hy3NodeData(layout);

	return *this;
}

bool Hy3NodeData::operator==(const Hy3NodeData& rhs) const {
	return this == &rhs;
}

bool Hy3Node::operator==(const Hy3Node& rhs) const {
	return this->data == rhs.data;
}

void Hy3Node::recalcSizePosRecursive(bool force) {
	if (this->data.type != Hy3NodeData::Group) {
		this->layout->applyNodeDataToWindow(this, force);
		return;
	}

	auto* group = &this->data.as_group;

	if (group->children.size() == 1 && this->parent != nullptr) {
		auto child = group->children.front();

		if (child == this) {
			Debug::log(ERR, "a group (%p) has become its own child", this);
			errorNotif();
		}

		double distortOut;
		double distortIn;

		const auto* gaps_in     = &g_pConfigManager->getConfigValuePtr("general:gaps_in")->intValue;
		const auto* gaps_out    = &g_pConfigManager->getConfigValuePtr("general:gaps_out")->intValue;

		if (gaps_in > gaps_out) {
			distortOut = *gaps_out - 1.0;
		} else {
			distortOut = *gaps_in - 1.0;
		}

		if (distortOut < 0) distortOut = 0.0;

		distortIn = *gaps_in * 2;

		switch (group->layout) {
		case Hy3GroupLayout::SplitH:
			child->position.x = this->position.x - distortOut;
			child->size.x = this->size.x - distortIn;
			child->position.y = this->position.y;
			child->size.y = this->size.y;
			break;
		case Hy3GroupLayout::SplitV:
			child->position.y = this->position.y - distortOut;
			child->size.y = this->size.y - distortIn;
			child->position.x = this->position.x;
			child->size.x = this->size.x;
		case Hy3GroupLayout::Tabbed:
			// TODO
			break;
		}

		child->recalcSizePosRecursive(force);
		return;
	}

	int constraint;
	switch (group->layout) {
	case Hy3GroupLayout::SplitH:
		constraint = this->size.x;
		break;
	case Hy3GroupLayout::SplitV:
		constraint = this->size.y;
		break;
	case Hy3GroupLayout::Tabbed:
		break;
	}

	double ratio_mul = group->layout != Hy3GroupLayout::Tabbed ? group->children.empty() ? 0 : constraint / group->children.size() : 0;

	double offset = 0;

	for(auto child: group->children) {
		switch (group->layout) {
		case Hy3GroupLayout::SplitH:
			child->position.x = this->position.x + offset;
			child->size.x = child->size_ratio * ratio_mul;
			offset += child->size.x;
			child->position.y = this->position.y;
			child->size.y = this->size.y;
			break;
		case Hy3GroupLayout::SplitV:
			child->position.y = this->position.y + offset;
			child->size.y = child->size_ratio * ratio_mul;
			offset += child->size.y;
			child->position.x = this->position.x;
			child->size.x = this->size.x;
			break;
		case Hy3GroupLayout::Tabbed:
			// TODO: tab bars
			child->position = this->position;
			child->size = this->size;
			break;
		}

		child->recalcSizePosRecursive(force);
	}
}

void Hy3Node::markFocused() {
	Hy3Node* node = this;

	// undo decos for root focus
	auto* root = node;
	while (root->parent != nullptr) root = root->parent;
	auto* oldfocus = root->getFocusedNode();

	// update focus
	if (this->data.type == Hy3NodeData::Group) {
		this->data.as_group.group_focused = true;
		this->data.as_group.focused_child = nullptr;
	}

	while (node->parent != nullptr) {
		node->parent->data.as_group.focused_child = node;
		node->parent->data.as_group.group_focused = false;
		node = node->parent;
	}

	if (oldfocus != nullptr) {
		oldfocus->updateDecos();
	}
}

void Hy3Node::focus() {
	this->markFocused();

	switch (this->data.type) {
	case Hy3NodeData::Window:
		g_pCompositor->focusWindow(this->data.as_window);
		break;
	case Hy3NodeData::Group:
		g_pCompositor->focusWindow(nullptr);
		this->raiseToTop();
		break;
	}
}

void Hy3Node::raiseToTop() {
	switch (this->data.type) {
	case Hy3NodeData::Window:
		g_pCompositor->moveWindowToTop(this->data.as_window);
		break;
	case Hy3NodeData::Group:
		for (auto* child: this->data.as_group.children) {
			child->raiseToTop();
		}
		break;
	}
}

Hy3Node* Hy3Node::getFocusedNode() {
	switch (this->data.type) {
	case Hy3NodeData::Window:
		return this;
	case Hy3NodeData::Group:
		if (this->data.as_group.focused_child == nullptr || this->data.as_group.group_focused) {
			return this;
		} else {
			return this->data.as_group.focused_child->getFocusedNode();
		}
	}
}

bool Hy3Node::swallowGroups(Hy3Node* into) {
	if (into == nullptr
			|| into->data.type != Hy3NodeData::Group
			|| into->data.as_group.children.size() != 1)
		return false;

	auto* child = into->data.as_group.children.front();

	// a lot of segfaulting happens once the assumption that the root node is a group is wrong.
	if (into->parent == nullptr && child->data.type != Hy3NodeData::Group) return false;

	Debug::log(LOG, "Swallowing %p into %p", child, into);
	Hy3Node::swapData(*into, *child);
	into->layout->nodes.remove(*child);

	return true;
}

Hy3Node* Hy3Node::removeFromParentRecursive() {
	Hy3Node* parent = this;

	Debug::log(LOG, "Recursively removing parent nodes of %p", parent);

	while (parent != nullptr) {
		if (parent->parent == nullptr) {
			Debug::log(ERR, "* UAF DEBUGGING - %p's parent is null, its the root group", parent);

			if (parent == this) {
				Debug::log(ERR, "* UAF DEBUGGING - returning nullptr as this == root group");
			} else {
				Debug::log(ERR, "* UAF DEBUGGING - deallocing %p and returning nullptr", parent);
				parent->layout->nodes.remove(*parent);
			}
			return nullptr;
		}

		auto* child = parent;
		parent = parent->parent;
		auto& group = parent->data.as_group;

		if (group.children.size() > 2) {
			auto iter = std::find(group.children.begin(), group.children.end(), child);

			group.group_focused = false;
			if (iter == group.children.begin()) {
				group.focused_child = *std::next(iter);
			} else {
				group.focused_child = *std::prev(iter);
			}
		}

		if (!group.children.remove(child)) {
			Debug::log(ERR, "Was unable to remove child node %p from parent %p. Child likely has a false parent pointer.", child, parent);
			errorNotif();
			return nullptr;
		}

		group.group_focused = false;
		if (group.children.size() == 1) {
			group.focused_child = group.children.front();
		}

		auto child_size_ratio = child->size_ratio;
		if (child != this) {
			parent->layout->nodes.remove(*child);
		} else {
			child->parent = nullptr;
		}

		if (!group.children.empty()) {
			auto child_count = group.children.size();
			if (std::find(group.children.begin(), group.children.end(), this) != group.children.end()) {
				child_count -= 1;
			}

			auto splitmod = -((1.0 - child_size_ratio) / child_count);

			for (auto* child: group.children) {
				child->size_ratio += splitmod;
			}

			break;
		}
	}

	return parent;
}

Hy3Node* Hy3Node::intoGroup(Hy3GroupLayout layout) {
	this->layout->nodes.push_back({
		.parent = this,
		.data = layout,
		.workspace_id = this->workspace_id,
		.layout = this->layout,
	});

	auto* node = &this->layout->nodes.back();
	swapData(*this, *node);

	this->data = layout;
	this->data.as_group.children.push_back(node);
	this->data.as_group.group_focused = false;
	this->data.as_group.focused_child = node;
	this->recalcSizePosRecursive();

	return node;
}

bool Hy3GroupData::hasChild(Hy3Node* node) {
	Debug::log(LOG, "Searching for child %p of %p", this, node);
	for (auto child: this->children) {
		if (child == node) return true;

		if (child->data.type == Hy3NodeData::Group) {
			if (child->data.as_group.hasChild(node)) return true;
		}
	}

	return false;
}

void Hy3Node::swapData(Hy3Node& a, Hy3Node& b) {
	Hy3NodeData aData = std::move(a.data);
	a.data = b.data;
	b.data = aData;

	if (a.data.type == Hy3NodeData::Group) {
		for (auto child: a.data.as_group.children) {
			child->parent = &a;
		}
	}

	if (b.data.type == Hy3NodeData::Group) {
		for (auto child: b.data.as_group.children) {
			child->parent = &b;
		}
	}
}

void Hy3Node::updateDecos() {
	switch (this->data.type) {
	case Hy3NodeData::Window:
		if (this->data.as_window->m_bIsMapped)
			g_pCompositor->updateWindowAnimatedDecorationValues(this->data.as_window);
		break;
	case Hy3NodeData::Group:
	  for (auto* child: this->data.as_group.children) {
			child->updateDecos();
		}
	}
}

int Hy3Layout::getWorkspaceNodeCount(const int& id) {
	int count = 0;

	for (auto& node: this->nodes) {
		if (node.workspace_id == id && node.valid) count++;
	}

	return count;
}

Hy3Node* Hy3Layout::getNodeFromWindow(CWindow* window) {
	for (auto& node: this->nodes) {
		if (node.data.type == Hy3NodeData::Window && node.data.as_window == window) {
			return &node;
		}
	}

	return nullptr;
}

Hy3Node* Hy3Layout::getWorkspaceRootGroup(const int& id) {
	for (auto& node: this->nodes) {
		if (node.workspace_id == id && node.parent == nullptr && node.data.type == Hy3NodeData::Group) {
			return &node;
		}
	}

	return nullptr;
}

Hy3Node* Hy3Layout::getWorkspaceFocusedNode(const int& id) {
	auto* rootNode = this->getWorkspaceRootGroup(id);
	if (rootNode == nullptr) return nullptr;
	return rootNode->getFocusedNode();
}

void Hy3Layout::applyNodeDataToWindow(Hy3Node* node, bool force) {
	if (node->data.type != Hy3NodeData::Window) return;
	CWindow* window = node->data.as_window;

	CMonitor* monitor = nullptr;

	if (g_pCompositor->isWorkspaceSpecial(node->workspace_id)) {
		for (auto& m: g_pCompositor->m_vMonitors) {
			if (m->specialWorkspaceID == node->workspace_id) {
				monitor = m.get();
				break;
			}
		}
	} else {
		monitor = g_pCompositor->getMonitorFromID(g_pCompositor->getWorkspaceByID(node->workspace_id)->m_iMonitorID);
	}

	if (monitor == nullptr) {
		Debug::log(ERR, "Orphaned Node %x (workspace ID: %i)!!", node, node->workspace_id);
		errorNotif();
		return;
	}

	// for gaps outer
	const bool display_left   = STICKS(node->position.x, monitor->vecPosition.x + monitor->vecReservedTopLeft.x);
	const bool display_right  = STICKS(node->position.x + node->size.x, monitor->vecPosition.x + monitor->vecSize.x - monitor->vecReservedBottomRight.x);
	const bool display_top    = STICKS(node->position.y, monitor->vecPosition.y + monitor->vecReservedTopLeft.y);
	const bool display_bottom = STICKS(node->position.y + node->size.y, monitor->vecPosition.y + monitor->vecSize.y - monitor->vecReservedBottomRight.y);

	static const auto* border_size           = &HyprlandAPI::getConfigValue(PHANDLE, "general:border_size")->intValue;
	static const auto* gaps_in               = &HyprlandAPI::getConfigValue(PHANDLE, "general:gaps_in")->intValue;
	static const auto* gaps_out              = &HyprlandAPI::getConfigValue(PHANDLE, "general:gaps_out")->intValue;
	static const auto* single_window_no_gaps = &HyprlandAPI::getConfigValue(PHANDLE, "plugin:hy3:no_gaps_when_only")->intValue;

	if (!g_pCompositor->windowExists(window) || !window->m_bIsMapped) {
		Debug::log(ERR, "Node %p holding invalid window %p!!", node, window);
		errorNotif();
		this->onWindowRemovedTiling(window);
		return;
	}

	window->m_vSize = node->size;
	window->m_vPosition = node->position;

	auto calcPos = window->m_vPosition + Vector2D(*border_size, *border_size);
	auto calcSize = window->m_vSize - Vector2D(2 * *border_size, 2 * *border_size);

	auto root_node = this->getWorkspaceRootGroup(window->m_iWorkspaceID);
	auto only_node = root_node->data.as_group.children.size() == 1
		&& root_node->data.as_group.children.front()->data.type == Hy3NodeData::Window;

	if (!g_pCompositor->isWorkspaceSpecial(window->m_iWorkspaceID)
			&& ((*single_window_no_gaps && only_node)
					 || (window->m_bIsFullscreen
							 && g_pCompositor->getWorkspaceByID(window->m_iWorkspaceID)->m_efFullscreenMode == FULLSCREEN_FULL))
	) {
		window->m_vRealPosition = window->m_vPosition;
		window->m_vRealSize = window->m_vSize;

		window->updateWindowDecos();

		window->m_sSpecialRenderData.rounding = false;
		window->m_sSpecialRenderData.border = false;
		window->m_sSpecialRenderData.decorate = false;
	} else {
		window->m_sSpecialRenderData.rounding = true;
		window->m_sSpecialRenderData.border = true;
		window->m_sSpecialRenderData.decorate = true;

		Vector2D offset_topleft(
			display_left ? *gaps_out : *gaps_in,
			display_top ? *gaps_out : *gaps_in
		);

		Vector2D offset_bottomright(
			display_right ? *gaps_out : *gaps_in,
			display_bottom ? *gaps_out : *gaps_in
		);

		calcPos = calcPos + offset_topleft;
		calcSize = calcSize - offset_topleft - offset_bottomright;

		const auto reserved_area = window->getFullWindowReservedArea();
		calcPos = calcPos + reserved_area.topLeft;
		calcSize = calcSize - (reserved_area.topLeft - reserved_area.bottomRight);

		window->m_vRealPosition = calcPos;
		window->m_vRealSize = calcSize;
		Debug::log(LOG, "Set size (%f %f)", calcSize.x, calcSize.y);

		g_pXWaylandManager->setWindowSize(window, calcSize);

		if (force) {
			g_pHyprRenderer->damageWindow(window);

			window->m_vRealPosition.warp();
			window->m_vRealSize.warp();

			g_pHyprRenderer->damageWindow(window);
		}

		window->updateWindowDecos();
	}
}

void Hy3Layout::onWindowCreatedTiling(CWindow* window) {
	if (window->m_bIsFloating) return;

	auto* existing = this->getNodeFromWindow(window);
	if (existing != nullptr) {
		Debug::log(WARN, "Attempted to add a window(%p) that is already tiled(as %p) to the layout", window, existing);
		return;
	}

	auto* monitor = g_pCompositor->getMonitorFromID(window->m_iMonitorID);

	Hy3Node* opening_into;
	Hy3Node* opening_after;

	if (g_pCompositor->m_pLastWindow != nullptr
			&& !g_pCompositor->m_pLastWindow->m_bIsFloating
			&& g_pCompositor->m_pLastWindow != window
			&& g_pCompositor->m_pLastWindow->m_iWorkspaceID == window->m_iWorkspaceID
			&& g_pCompositor->m_pLastWindow->m_bIsMapped)
	{
		opening_after = this->getNodeFromWindow(g_pCompositor->m_pLastWindow);
	} else {
		opening_after = this->getNodeFromWindow(g_pCompositor->vectorToWindowTiled(g_pInputManager->getMouseCoordsInternal()));
	}

	if (opening_after != nullptr && opening_after->workspace_id != window->m_iWorkspaceID) {
		opening_after = nullptr;
	}

	if (opening_after != nullptr) {
		opening_into = opening_after->parent;
	} else {
		if ((opening_into = this->getWorkspaceRootGroup(window->m_iWorkspaceID)) == nullptr) {
			this->nodes.push_back({
				.data = Hy3GroupLayout::SplitH,
				.position = monitor->vecPosition + monitor->vecReservedTopLeft,
				.size = monitor->vecSize - monitor->vecReservedTopLeft - monitor->vecReservedBottomRight,
				.workspace_id = window->m_iWorkspaceID,
				.layout = this,
			});

			opening_into = &this->nodes.back();
		}
	}

	if (opening_into->data.type != Hy3NodeData::Group) {
		Debug::log(ERR, "opening_into node %p was not of type Group", opening_into);
		errorNotif();
		return;
	}

	if (opening_into->workspace_id != window->m_iWorkspaceID) {
		Debug::log(WARN, "opening_into node %p has workspace %d which does not match the opening window (workspace %d)", opening_into, opening_into->workspace_id, window->m_iWorkspaceID);
	}

	this->nodes.push_back({
		.parent = opening_into,
		.data = window,
		.workspace_id = window->m_iWorkspaceID,
		.layout = this,
	});

	auto& node = this->nodes.back();

	if (opening_after == nullptr) {
		opening_into->data.as_group.children.push_back(&node);
	} else {
		auto& children = opening_into->data.as_group.children;
		auto iter = std::find(children.begin(), children.end(), opening_after);
		auto iter2 = std::next(iter);
		children.insert(iter2, &node);
	}
	Debug::log(LOG, "opened new window %p(node: %p) on window %p in %p", window, &node, opening_after, opening_into);

	node.markFocused();
	opening_into->recalcSizePosRecursive();
	Debug::log(LOG, "opening_into (%p) contains new child (%p)? %d", opening_into, &node, opening_into->data.as_group.hasChild(&node));
}

void Hy3Layout::onWindowRemovedTiling(CWindow* window) {
	auto* node = this->getNodeFromWindow(window);
	Debug::log(LOG, "remove tiling %p (window %p)", node, window);

	if (node == nullptr) {
		Debug::log(ERR, "onWindowRemovedTiling node null?");
		return;
	}

	window->m_sSpecialRenderData.rounding = true;
	window->m_sSpecialRenderData.border = true;
	window->m_sSpecialRenderData.decorate = true;

	if (window->m_bIsFullscreen) {
		g_pCompositor->setWindowFullscreen(window, false, FULLSCREEN_FULL);
	}

	auto* parent = node->removeFromParentRecursive();
	this->nodes.remove(*node);

	if (parent != nullptr) {
		parent->recalcSizePosRecursive();

		if (parent->data.as_group.children.size() == 1
				&& parent->data.as_group.children.front()->data.type == Hy3NodeData::Group)
		{
			auto* target_parent = parent;
			while (target_parent != nullptr && Hy3Node::swallowGroups(target_parent)) {
				target_parent = target_parent->parent;
			}

			if (target_parent != parent && target_parent != nullptr)
				target_parent->recalcSizePosRecursive();
		}
	}

}

CWindow* Hy3Layout::getNextWindowCandidate(CWindow* window) {
	auto* node = this->getWorkspaceFocusedNode(window->m_iWorkspaceID);
	if (node == nullptr) return nullptr;

	switch (node->data.type) {
	case Hy3NodeData::Window:
		return node->data.as_window;
	case Hy3NodeData::Group:
		return nullptr;
	}
}

void Hy3Layout::onWindowFocusChange(CWindow* window) {
	Debug::log(LOG, "Switched windows to %p", window);
	auto* node = this->getNodeFromWindow(window);
	if (node == nullptr) return;

	node->markFocused();
}

bool Hy3Layout::isWindowTiled(CWindow* window) {
	return this->getNodeFromWindow(window) != nullptr;
}

void Hy3Layout::recalculateMonitor(const int& monitor_id) {
	Debug::log(LOG, "Recalculate monitor %d", monitor_id);
	const auto monitor = g_pCompositor->getMonitorFromID(monitor_id);
	if (monitor == nullptr) return;

	g_pHyprRenderer->damageMonitor(monitor);

	const auto workspace = g_pCompositor->getWorkspaceByID(monitor->activeWorkspace);
	if (workspace == nullptr) return;

	if (monitor->specialWorkspaceID) {
		const auto top_node = this->getWorkspaceRootGroup(monitor->specialWorkspaceID);

		if (top_node != nullptr) {
			top_node->position = monitor->vecPosition + monitor->vecReservedTopLeft;
			top_node->size = monitor->vecSize - monitor->vecReservedTopLeft - monitor->vecReservedBottomRight;
			top_node->recalcSizePosRecursive();
		}
	}

	if (workspace->m_bHasFullscreenWindow) {
		const auto window = g_pCompositor->getFullscreenWindowOnWorkspace(workspace->m_iID);

		if (workspace->m_efFullscreenMode == FULLSCREEN_FULL) {
			window->m_vRealPosition = monitor->vecPosition;
			window->m_vRealSize = monitor->vecSize;
		} else {
			// Vaxry's hack from below, but again

			Hy3Node fakeNode = {
				.data = window,
				.position = monitor->vecPosition + monitor->vecReservedTopLeft,
				.size = monitor->vecSize - monitor->vecReservedTopLeft - monitor->vecReservedBottomRight,
				.workspace_id = window->m_iWorkspaceID,
			};

			this->applyNodeDataToWindow(&fakeNode);
		}
	} else {
		const auto top_node = this->getWorkspaceRootGroup(monitor->activeWorkspace);

		if (top_node != nullptr) {
			top_node->position = monitor->vecPosition + monitor->vecReservedTopLeft;
			top_node->size = monitor->vecSize - monitor->vecReservedTopLeft - monitor->vecReservedBottomRight;
			top_node->recalcSizePosRecursive();
		}
	}
}

void Hy3Layout::recalculateWindow(CWindow* window) {
	auto* node = this->getNodeFromWindow(window);
	if (node == nullptr) return;
	node->recalcSizePosRecursive();
}

void Hy3Layout::onBeginDragWindow() {
	this->drag_flags.started = false;
	IHyprLayout::onBeginDragWindow();
}

void Hy3Layout::resizeActiveWindow(const Vector2D& delta, CWindow* pWindow) {
	auto window = pWindow ? pWindow : g_pCompositor->m_pLastWindow;
	if (!g_pCompositor->windowValidMapped(window)) return;

	auto* node = this->getNodeFromWindow(window);
	if (node == nullptr) return;

	if (!this->drag_flags.started) {
		if (g_pInputManager->currentlyDraggedWindow == window) {
			auto mouse = g_pInputManager->getMouseCoordsInternal();
			auto mouse_offset = mouse - window->m_vPosition;

			this->drag_flags = {
				.started = true,
				.xExtent = mouse_offset.x > window->m_vSize.x / 2,
				.yExtent = mouse_offset.y > window->m_vSize.y / 2,
			};

			Debug::log(LOG, "Positive offsets - x: %d, y: %d", this->drag_flags.xExtent, this->drag_flags.yExtent);
		} else {
			this->drag_flags = {
				.started = false,
				.xExtent = delta.x > 0,
				.yExtent = delta.y > 0,
			};
		}
	}

	const auto animate = &g_pConfigManager->getConfigValuePtr("misc:animate_manual_resizes")->intValue;

	auto monitor = g_pCompositor->getMonitorFromID(window->m_iMonitorID);

	const bool display_left   = STICKS(node->position.x, monitor->vecPosition.x + monitor->vecReservedTopLeft.x);
	const bool display_right  = STICKS(node->position.x + node->size.x, monitor->vecPosition.x + monitor->vecSize.x - monitor->vecReservedBottomRight.x);
	const bool display_top    = STICKS(node->position.y, monitor->vecPosition.y + monitor->vecReservedTopLeft.y);
	const bool display_bottom = STICKS(node->position.y + node->size.y, monitor->vecPosition.y + monitor->vecSize.y - monitor->vecReservedBottomRight.y);

	Vector2D allowed_movement = delta;
	if (display_left && display_right) allowed_movement.x = 0;
	if (display_top && display_bottom) allowed_movement.y = 0;

	auto* inner_node = node;

	// break into parent groups when encountering a corner we're dragging in or a tab group
	while (inner_node->parent != nullptr) {
		auto& group = inner_node->parent->data.as_group;

		switch (group.layout) {
		case Hy3GroupLayout::Tabbed:
			// treat tabbed layouts as if they dont exist during resizing
			goto cont;
		case Hy3GroupLayout::SplitH:
			if ((this->drag_flags.xExtent && group.children.back() == inner_node)
					|| (!this->drag_flags.xExtent && group.children.front() == inner_node)) {
				goto cont;
			}
			break;
		case Hy3GroupLayout::SplitV:
			if ((this->drag_flags.yExtent && group.children.back() == inner_node)
					|| (!this->drag_flags.yExtent && group.children.front() == inner_node)) {
				goto cont;
			}
			break;
		}

		break;
	cont:
		inner_node = inner_node->parent;
	}

	auto* inner_parent = inner_node->parent;
	if (inner_parent == nullptr) return;

	auto* outer_node = inner_node;

	// break into parent groups when encountering a corner we're dragging in, a tab group,
	// or a layout matching the inner_parent.
	while (outer_node->parent != nullptr) {
		auto& group = outer_node->parent->data.as_group;

		// break out of all layouts that match the orientation of the inner_parent
		if (group.layout == inner_parent->data.as_group.layout) goto cont2;

		switch (group.layout) {
		case Hy3GroupLayout::Tabbed:
			// treat tabbed layouts as if they dont exist during resizing
			goto cont2;
		case Hy3GroupLayout::SplitH:
			if ((this->drag_flags.xExtent && group.children.back() == outer_node)
					|| (!this->drag_flags.xExtent && group.children.front() == outer_node)) {
				goto cont2;
			}
			break;
		case Hy3GroupLayout::SplitV:
			if ((this->drag_flags.yExtent && group.children.back() == outer_node)
					|| (!this->drag_flags.yExtent && group.children.front() == outer_node)) {
				goto cont2;
			}
			break;
		}

		break;
	cont2:
		outer_node = outer_node->parent;
	}

	Debug::log(LOG, "resizeActive - inner_node: %p, outer_node: %p", inner_node, outer_node);

	auto& inner_group = inner_parent->data.as_group;
	// adjust the inner node
	switch (inner_group.layout) {
	case Hy3GroupLayout::SplitH: {
		auto ratio_mod = allowed_movement.x * (float) inner_group.children.size() / inner_parent->size.x;

		auto iter = std::find(inner_group.children.begin(), inner_group.children.end(), inner_node);

		if (this->drag_flags.xExtent) {
			if (inner_node == inner_group.children.back()) break;
			iter = std::next(iter);
		} else {
			if (inner_node == inner_group.children.front()) break;
			iter = std::prev(iter);
			ratio_mod = -ratio_mod;
		}

		auto* neighbor = *iter;

		inner_node->size_ratio += ratio_mod;
		neighbor->size_ratio -= ratio_mod;
	} break;
	case Hy3GroupLayout::SplitV: {
		auto ratio_mod = allowed_movement.y * (float) inner_parent->data.as_group.children.size() / inner_parent->size.y;

		auto iter = std::find(inner_group.children.begin(), inner_group.children.end(), inner_node);

		if (this->drag_flags.yExtent) {
			if (inner_node == inner_group.children.back()) break;
			iter = std::next(iter);
		} else {
			if (inner_node == inner_group.children.front()) break;
			iter = std::prev(iter);
			ratio_mod = -ratio_mod;
		}

		auto* neighbor = *iter;

		inner_node->size_ratio += ratio_mod;
		neighbor->size_ratio -= ratio_mod;
	} break;
	}

	inner_parent->recalcSizePosRecursive(*animate == 0);

	if (outer_node != nullptr && outer_node->parent != nullptr) {
		auto* outer_parent = outer_node->parent;
		auto& outer_group = outer_parent->data.as_group;
		// adjust the outer node
		switch (outer_group.layout) {
		case Hy3GroupLayout::SplitH: {
			auto ratio_mod = allowed_movement.x * (float) outer_group.children.size() / outer_parent->size.x;

			auto iter = std::find(outer_group.children.begin(), outer_group.children.end(), outer_node);

			if (this->drag_flags.xExtent) {
				if (outer_node == inner_group.children.back()) break;
				iter = std::next(iter);
			} else {
				if (outer_node == inner_group.children.front()) break;
				iter = std::prev(iter);
				ratio_mod = -ratio_mod;
			}

			auto* neighbor = *iter;

			outer_node->size_ratio += ratio_mod;
			neighbor->size_ratio -= ratio_mod;
		} break;
		case Hy3GroupLayout::SplitV: {
			auto ratio_mod = allowed_movement.y * (float) outer_parent->data.as_group.children.size() / outer_parent->size.y;

			auto iter = std::find(outer_group.children.begin(), outer_group.children.end(), outer_node);

			if (this->drag_flags.yExtent) {
				if (outer_node == outer_group.children.back()) break;
				iter = std::next(iter);
			} else {
				if (outer_node == outer_group.children.front()) break;
				iter = std::prev(iter);
				ratio_mod = -ratio_mod;
			}

			auto* neighbor = *iter;

			outer_node->size_ratio += ratio_mod;
			neighbor->size_ratio -= ratio_mod;
		} break;
		}

		outer_parent->recalcSizePosRecursive(*animate == 0);
	}
}

void Hy3Layout::fullscreenRequestForWindow(CWindow* window, eFullscreenMode fullscreen_mode, bool on) {
	if (!g_pCompositor->windowValidMapped(window)) return;
	if (on == window->m_bIsFullscreen || g_pCompositor->isWorkspaceSpecial(window->m_iWorkspaceID)) return;

	const auto monitor = g_pCompositor->getMonitorFromID(window->m_iMonitorID);
	const auto workspace = g_pCompositor->getWorkspaceByID(window->m_iWorkspaceID);
	if (workspace->m_bHasFullscreenWindow && on) return;

	window->m_bIsFullscreen = on;
	workspace->m_bHasFullscreenWindow = !workspace->m_bHasFullscreenWindow;

	if (!window->m_bIsFullscreen) {
		auto* node = this->getNodeFromWindow(window);

		if (node) {
			// restore node positioning if tiled
			this->applyNodeDataToWindow(node);
		} else {
			// restore floating position if not
			window->m_vRealPosition = window->m_vLastFloatingPosition;
			window->m_vRealSize = window->m_vLastFloatingSize;

			window->m_sSpecialRenderData.rounding = true;
			window->m_sSpecialRenderData.border = true;
			window->m_sSpecialRenderData.decorate = true;
		}
	} else {
		workspace->m_efFullscreenMode = fullscreen_mode;

		// save position and size if floating
		if (window->m_bIsFloating) {
			window->m_vLastFloatingPosition = window->m_vRealPosition.goalv();
			window->m_vPosition = window->m_vRealPosition.goalv();
			window->m_vLastFloatingSize = window->m_vRealSize.goalv();
			window->m_vSize = window->m_vRealSize.goalv();
		}

		if (fullscreen_mode == FULLSCREEN_FULL) {
			Debug::log(LOG, "fullscreen");
			window->m_vRealPosition = monitor->vecPosition;
			window->m_vRealSize = monitor->vecSize;
		} else {
			Debug::log(LOG, "vaxry hack");
			// Copy of vaxry's massive hack

			Hy3Node fakeNode = {
				.data = window,
				.position = monitor->vecPosition + monitor->vecReservedTopLeft,
				.size = monitor->vecSize - monitor->vecReservedTopLeft - monitor->vecReservedBottomRight,
				.workspace_id = window->m_iWorkspaceID,
			};

			this->applyNodeDataToWindow(&fakeNode);
		}
	}

	g_pCompositor->updateWindowAnimatedDecorationValues(window);
	g_pXWaylandManager->setWindowSize(window, window->m_vRealSize.goalv());
	g_pCompositor->moveWindowToTop(window);
	this->recalculateMonitor(monitor->ID);
}

std::any Hy3Layout::layoutMessage(SLayoutMessageHeader header, std::string content) {
	if (content == "togglesplit") {
		auto* node = this->getNodeFromWindow(header.pWindow);
		if (node != nullptr && node->parent != nullptr) {
			auto& layout = node->parent->data.as_group.layout;

			switch (layout) {
			case Hy3GroupLayout::SplitH:
			  layout = Hy3GroupLayout::SplitV;
				node->parent->recalcSizePosRecursive();
				break;
			case Hy3GroupLayout::SplitV:
				layout = Hy3GroupLayout::SplitH;
				node->parent->recalcSizePosRecursive();
				break;
			case Hy3GroupLayout::Tabbed:
				break;
			}
		}
	}

	return "";
}

SWindowRenderLayoutHints Hy3Layout::requestRenderHints(CWindow* window) {
	return {};
}

void Hy3Layout::switchWindows(CWindow* pWindowA, CWindow* pWindowB) {
	// todo
}

void Hy3Layout::alterSplitRatio(CWindow* pWindow, float delta, bool exact) {
	// todo
}

std::string Hy3Layout::getLayoutName() {
    return "hy3";
}

void Hy3Layout::replaceWindowDataWith(CWindow* from, CWindow* to) {
	auto* node = this->getNodeFromWindow(from);
	if (node == nullptr) return;

	node->data.as_window = to;
	this->applyNodeDataToWindow(node);
}

void Hy3Layout::onEnable() {
	for (auto &window : g_pCompositor->m_vWindows) {
		if (window->isHidden()
				|| !window->m_bIsMapped
				|| window->m_bFadingOut
				|| window->m_bIsFloating)
			continue;

		this->onWindowCreatedTiling(window.get());
	}

	selection_hook::enable();
}

void Hy3Layout::onDisable() {
	selection_hook::disable();
	this->nodes.clear();
}

void Hy3Layout::makeGroupOnWorkspace(int workspace, Hy3GroupLayout layout) {
	auto* node = this->getWorkspaceFocusedNode(workspace);
	this->makeGroupOn(node, layout);
}

void Hy3Layout::makeOppositeGroupOnWorkspace(int workspace) {
	auto* node = this->getWorkspaceFocusedNode(workspace);
	this->makeOppositeGroupOn(node);
}

void Hy3Layout::makeGroupOn(Hy3Node* node, Hy3GroupLayout layout) {
	if (node == nullptr) return;

	if (node->parent != nullptr) {
		auto& group = node->parent->data.as_group;
		if (group.children.size() == 1
			&& (group.layout == Hy3GroupLayout::SplitH
			|| group.layout == Hy3GroupLayout::SplitV))
		{
			group.layout = layout;
			node->parent->recalcSizePosRecursive();
			return;
		}
	}

	node->intoGroup(layout);
}

void Hy3Layout::makeOppositeGroupOn(Hy3Node* node) {
	if (node == nullptr) return;

	if (node->parent == nullptr) {
		node->intoGroup(Hy3GroupLayout::SplitH);
	} else {
		auto& group = node->parent->data.as_group;
		auto layout = group.layout == Hy3GroupLayout::SplitH
			? Hy3GroupLayout::SplitV
			: Hy3GroupLayout::SplitH;

		if (group.children.size() == 1) {
			group.layout = layout;
			node->parent->recalcSizePosRecursive();
		} else {
			node->intoGroup(layout);
		}
	}
}

void Hy3Layout::shiftFocus(int workspace, ShiftDirection direction) {
	auto* node = this->getWorkspaceFocusedNode(workspace);
	Debug::log(LOG, "ShiftFocus %p %d", node, direction);
	if (node == nullptr) return;

	Hy3Node* target;
	if ((target = this->shiftOrGetFocus(*node, direction, false, false))) {
        target->focus();

		auto* parent = target->parent;

		// If this node is in a group
		if (parent != nullptr && parent->data.as_group.layout == Hy3GroupLayout::SplitV ) {
			double split_ratio = 0.1;
			auto& children = parent->data.as_group.children;
			double ratio_mod = children.size();

			for (auto&& child : children) {
				if (child != target) {
					child->size_ratio = split_ratio * ratio_mod;
				}
				else {
					child->size_ratio = (1.0 - (split_ratio * (children.size()-1))) * ratio_mod;
				}
			}
			parent->recalcSizePosRecursive(false);
		}
	}
}

void Hy3Layout::shiftWindow(int workspace, ShiftDirection direction, bool once) {
	auto* node = this->getWorkspaceFocusedNode(workspace);
	Debug::log(LOG, "ShiftWindow %p %d", node, direction);
	if (node == nullptr) return;


	this->shiftOrGetFocus(*node, direction, true, once);
}

bool shiftIsForward(ShiftDirection direction) {
	return direction == ShiftDirection::Right || direction == ShiftDirection::Down;
}

bool shiftIsVertical(ShiftDirection direction) {
	return direction == ShiftDirection::Up || direction == ShiftDirection::Down;
}

bool shiftMatchesLayout(Hy3GroupLayout layout, ShiftDirection direction) {
	return (layout == Hy3GroupLayout::SplitV && shiftIsVertical(direction))
		|| (layout != Hy3GroupLayout::SplitV && !shiftIsVertical(direction));
}

Hy3Node* Hy3Layout::shiftOrGetFocus(Hy3Node& node, ShiftDirection direction, bool shift, bool once) {
	auto* break_origin = &node;
	auto* break_parent = break_origin->parent;

	auto has_broken_once = false;

	// break parents until we hit a container oriented the same way as the shift direction
	while (true) {
		if (break_parent == nullptr) return nullptr;

		auto& group = break_parent->data.as_group; // must be a group in order to be a parent


		if (shiftMatchesLayout(group.layout, direction)) {
			// group has the correct orientation

			if (once && shift && has_broken_once) break;
			if (break_origin != &node) has_broken_once = true;

			// if this movement would break out of the group, continue the break loop (do not enter this if)
			// otherwise break.
			if ((has_broken_once && once && shift)
					|| !((!shiftIsForward(direction) && group.children.front() == break_origin)
							 || (shiftIsForward(direction) && group.children.back() == break_origin)))
				break;
		}

		if (break_parent->parent == nullptr) {
			if (!shift) return nullptr;

			// if we haven't gone up any levels and the group is in the same direction
			// there's no reason to wrap the root group.
			if (shiftMatchesLayout(group.layout, direction)) break;

			if (group.layout != Hy3GroupLayout::Tabbed
				&& group.children.size() == 2
				&& std::find(group.children.begin(), group.children.end(), &node) != group.children.end()
			) {
				group.layout = shiftIsVertical(direction) ? Hy3GroupLayout::SplitV : Hy3GroupLayout::SplitH;
			} else {
				// wrap the root group in another group
				this->nodes.push_back({
						.parent = break_parent,
						.data = shiftIsVertical(direction) ? Hy3GroupLayout::SplitV : Hy3GroupLayout::SplitH,
						.position = break_parent->position,
						.size = break_parent->size,
						.workspace_id = break_parent->workspace_id,
						.layout = this,
				});

				auto* newChild = &this->nodes.back();
				Hy3Node::swapData(*break_parent, *newChild);
				break_parent->data.as_group.children.push_back(newChild);
				break_parent->data.as_group.group_focused = false;
				break_parent->data.as_group.focused_child = newChild;
				break_origin = newChild;
			}

			break;
		} else {
			break_origin = break_parent;
			break_parent = break_origin->parent;
		}
	}

	auto& parent_group = break_parent->data.as_group;
	Hy3Node* target_group = break_parent;
	std::list<Hy3Node*>::iterator insert;

	if (break_origin == parent_group.children.front() && !shiftIsForward(direction)) {
		if (!shift) return nullptr;
		insert = parent_group.children.begin();
	} else if (break_origin == parent_group.children.back() && shiftIsForward(direction)) {
		if (!shift) return nullptr;
		insert = parent_group.children.end();
	} else {
		auto& group_data = target_group->data.as_group;

		auto iter = std::find(group_data.children.begin(), group_data.children.end(), break_origin);
		if (shiftIsForward(direction)) iter = std::next(iter);
		else iter = std::prev(iter);

		if ((*iter)->data.type == Hy3NodeData::Window || (shift && once && has_broken_once)) {
			if (shift) {
				if (target_group == node.parent) {
					if (shiftIsForward(direction)) insert = std::next(iter);
					else insert = iter;
				} else {
					if (shiftIsForward(direction)) insert = iter;
					else insert = std::next(iter);
				}
			} else return *iter;
		} else {
			// break into neighboring groups until we hit a window
			while (true) {
				target_group = *iter;
				auto& group_data = target_group->data.as_group;

				if (group_data.children.empty()) return nullptr; // in theory this would never happen

				bool shift_after = false;

				if (shiftMatchesLayout(group_data.layout, direction)) {
					// if the group has the same orientation as movement pick the last/first child based
					// on movement direction
					if (shiftIsForward(direction)) iter = group_data.children.begin();
					else {
						iter = std::prev(group_data.children.end());
						shift_after = true;
					}
				} else {
					if (group_data.focused_child != nullptr) {
						iter = std::find(group_data.children.begin(), group_data.children.end(), group_data.focused_child);
						shift_after = true;
					} else {
						iter = group_data.children.begin();
					}
				}

				if (shift && once) {
					if (shift_after) insert = std::next(iter);
					else insert = iter;
					break;
				}

				if ((*iter)->data.type == Hy3NodeData::Window) {
					if (shift) {
						if (shift_after) insert = std::next(iter);
						else insert = iter;
						break;
					} else {
						return *iter;
					}
				}
			}
		}
	}

	auto& group_data = target_group->data.as_group;

	if (target_group == node.parent) {
		// nullptr is used as a signal value instead of removing it first to avoid iterator invalidation.
		auto iter = std::find(group_data.children.begin(), group_data.children.end(), &node);
		*iter = nullptr;
		target_group->data.as_group.children.insert(insert, &node);
		target_group->data.as_group.children.remove(nullptr);
		target_group->recalcSizePosRecursive();
	} else {
		target_group->data.as_group.children.insert(insert, &node);

		// must happen AFTER `insert` is used
		auto* old_parent = node.removeFromParentRecursive();
		node.parent = target_group;
		node.size_ratio = 1.0;

		if (old_parent != nullptr) old_parent->recalcSizePosRecursive();
		target_group->recalcSizePosRecursive();

		auto* target_parent = target_group->parent;
		while (target_parent != nullptr && Hy3Node::swallowGroups(target_parent)) {
			target_parent = target_parent->parent;
		}

		node.markFocused();

		if (target_parent != target_group && target_parent != nullptr)
			target_parent->recalcSizePosRecursive();
	}

	return nullptr;
}

void Hy3Layout::raiseFocus(int workspace) {
	auto* node = this->getWorkspaceFocusedNode(workspace);
	if (node == nullptr) return;

	if (node->parent != nullptr && node->parent->parent != nullptr) {
		node->parent->focus();
		node->parent->updateDecos();
	}
}

bool Hy3Layout::shouldRenderSelected(CWindow* window) {
	if (window == nullptr) return false;
	auto* root = this->getWorkspaceRootGroup(window->m_iWorkspaceID);
	if (root == nullptr || root->data.as_group.focused_child == nullptr) return false;
	auto* focused = root->getFocusedNode();
	if (focused == nullptr) return false;

	switch (focused->data.type) {
	case Hy3NodeData::Window:
		return false;
	case Hy3NodeData::Group:
		auto* node = this->getNodeFromWindow(window);
		if (node == nullptr) return false;
		return focused->data.as_group.hasChild(node);
	}
}

std::string Hy3Node::debugNode() {
	std::stringstream buf;
	std::string addr = "0x" + std::to_string((size_t)this);
	switch (this->data.type) {
	case Hy3NodeData::Window:
		buf << "window(";
		buf << std::hex << this;
		buf << ") [hypr ";
		buf << this->data.as_window;
		buf << "] size ratio: ";
		buf << this->size_ratio;
		break;
	case Hy3NodeData::Group:
		buf << "group(";
		buf << std::hex << this;
		buf << ") [";

		switch (this->data.as_group.layout) {
		case Hy3GroupLayout::SplitH:
			buf << "splith";
			break;
		case Hy3GroupLayout::SplitV:
			buf << "splitv";
			break;
		case Hy3GroupLayout::Tabbed:
			buf << "tabs";
			break;
		}

		buf << "] size ratio: ";
		buf << this->size_ratio;
		for (auto* child: this->data.as_group.children) {
			buf << "\n|-";
			if (child == nullptr) {
				buf << "nullptr";
			} else {
				// this is terrible
				for (char c: child->debugNode()) {
					buf << c;
					if (c == '\n') buf << "  ";
				}
			}
		}

		break;
	}

	return buf.str();
}
