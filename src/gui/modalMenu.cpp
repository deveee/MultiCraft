/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>
Copyright (C) 2018 stujones11, Stuart Jones <stujones111@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <cstdlib>
#include "modalMenu.h"
#include "gettext.h"
#include "porting.h"
#include "settings.h"
#include "client/renderingengine.h"

#ifdef HAVE_TOUCHSCREENGUI
#include "touchscreengui.h"
#endif

#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
#include <SDL.h>
#endif

// clang-format off
GUIModalMenu::GUIModalMenu(gui::IGUIEnvironment* env, gui::IGUIElement* parent,
	s32 id, IMenuManager *menumgr, bool remap_dbl_click) :
		IGUIElement(gui::EGUIET_ELEMENT, env, parent, id,
				core::rect<s32>(0, 0, 100, 100)),
#if defined(__ANDROID__) || defined(__IOS__)
		m_jni_field_name(""),
#endif
		m_menumgr(menumgr),
		m_remap_dbl_click(remap_dbl_click)
{
	m_gui_scale = g_settings->getFloat("gui_scaling");
#ifdef HAVE_TOUCHSCREENGUI
	float d = RenderingEngine::getDisplayDensity();
	m_gui_scale *= 1.1 - 0.3 * d + 0.2 * d * d;
#endif
	setVisible(true);
	Environment->setFocus(this);
	m_menumgr->createdMenu(this);

	m_doubleclickdetect[0].time = 0;
	m_doubleclickdetect[1].time = 0;

	m_doubleclickdetect[0].pos = v2s32(0, 0);
	m_doubleclickdetect[1].pos = v2s32(0, 0);
}
// clang-format on

GUIModalMenu::~GUIModalMenu()
{
#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
	if (porting::hasRealKeyboard() && SDL_IsTextInputActive())
		SDL_StopTextInput();
#endif
	m_menumgr->deletingMenu(this);
}

void GUIModalMenu::allowFocusRemoval(bool allow)
{
	m_allow_focus_removal = allow;
}

bool GUIModalMenu::canTakeFocus(gui::IGUIElement *e)
{
	return (e && (e == this || isMyChild(e))) || m_allow_focus_removal;
}

void GUIModalMenu::draw()
{
	if (!IsVisible)
		return;

	video::IVideoDriver *driver = Environment->getVideoDriver();
	v2u32 screensize = driver->getScreenSize();
	if (screensize != m_screensize_old) {
		m_screensize_old = screensize;
		regenerateGui(screensize);
	}

	drawMenu();
}

/*
	This should be called when the menu wants to quit.

	WARNING: THIS DEALLOCATES THE MENU FROM MEMORY. Return
	immediately if you call this from the menu itself.

	(More precisely, this decrements the reference count.)
*/
void GUIModalMenu::quitMenu()
{
	allowFocusRemoval(true);
	// This removes Environment's grab on us
	Environment->removeFocus(this);
	m_menumgr->deletingMenu(this);
	this->remove();
#ifdef HAVE_TOUCHSCREENGUI
	if (g_touchscreengui && g_touchscreengui->isActive() && m_touchscreen_visible)
		g_touchscreengui->show();
#endif
}

void GUIModalMenu::removeChildren()
{
	const core::list<gui::IGUIElement *> &children = getChildren();
	core::list<gui::IGUIElement *> children_copy;
	for (gui::IGUIElement *i : children) {
		children_copy.push_back(i);
	}

	for (gui::IGUIElement *i : children_copy) {
		i->remove();
	}
}

// clang-format off
bool GUIModalMenu::DoubleClickDetection(const SEvent &event)
{
	/* The following code is for capturing double-clicks of the mouse button
	 * and translating the double-click into an EET_KEY_INPUT_EVENT event
	 * -- which closes the form -- under some circumstances.
	 *
	 * There have been many github issues reporting this as a bug even though it
	 * was an intended feature.  For this reason, remapping the double-click as
	 * an ESC must be explicitly set when creating this class via the
	 * /p remap_dbl_click parameter of the constructor.
	 */

	if (!m_remap_dbl_click)
		return false;

	if (event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN) {
		m_doubleclickdetect[0].pos = m_doubleclickdetect[1].pos;
		m_doubleclickdetect[0].time = m_doubleclickdetect[1].time;

		m_doubleclickdetect[1].pos = m_pointer;
		m_doubleclickdetect[1].time = porting::getTimeMs();
	} else if (event.MouseInput.Event == EMIE_LMOUSE_LEFT_UP) {
		u64 delta = porting::getDeltaMs(
			m_doubleclickdetect[0].time, porting::getTimeMs());
		if (delta > 400)
			return false;

		double squaredistance = m_doubleclickdetect[0].pos.
			getDistanceFromSQ(m_doubleclickdetect[1].pos);

		if (squaredistance > (30 * 30)) {
			return false;
		}

		SEvent translated{};
		// translate doubleclick to escape
		translated.EventType            = EET_KEY_INPUT_EVENT;
		translated.KeyInput.Key         = KEY_ESCAPE;
		translated.KeyInput.Control     = false;
		translated.KeyInput.Shift       = false;
		translated.KeyInput.PressedDown = true;
		translated.KeyInput.Char        = 0;
		OnEvent(translated);

		return true;
	}

	return false;
}
// clang-format on

static bool isChild(gui::IGUIElement *tocheck, gui::IGUIElement *parent)
{
	while (tocheck) {
		if (tocheck == parent) {
			return true;
		}
		tocheck = tocheck->getParent();
	}
	return false;
}

#ifdef HAVE_TOUCHSCREENGUI

bool GUIModalMenu::convertToMouseEvent(
		SEvent &mouse_event, ETOUCH_INPUT_EVENT touch_event) const noexcept
{
	mouse_event = {};
	mouse_event.EventType = EET_MOUSE_INPUT_EVENT;
	mouse_event.MouseInput.X = m_pointer.X;
	mouse_event.MouseInput.Y = m_pointer.Y;
	switch (touch_event) {
	case ETIE_PRESSED_DOWN:
		mouse_event.MouseInput.Event = EMIE_LMOUSE_PRESSED_DOWN;
		mouse_event.MouseInput.ButtonStates = EMBSM_LEFT;
		break;
	case ETIE_MOVED:
		mouse_event.MouseInput.Event = EMIE_MOUSE_MOVED;
		mouse_event.MouseInput.ButtonStates = EMBSM_LEFT;
		break;
	case ETIE_LEFT_UP:
		mouse_event.MouseInput.Event = EMIE_LMOUSE_LEFT_UP;
		mouse_event.MouseInput.ButtonStates = 0;
		break;
	default:
		return false;
	}
	return true;
}

void GUIModalMenu::enter(gui::IGUIElement *hovered)
{
	if (!hovered)
		return;
	sanity_check(!m_hovered);
	m_hovered.grab(hovered);
	SEvent gui_event{};
	gui_event.EventType = EET_GUI_EVENT;
	gui_event.GUIEvent.Caller = m_hovered.get();
	gui_event.GUIEvent.EventType = EGET_ELEMENT_HOVERED;
	gui_event.GUIEvent.Element = gui_event.GUIEvent.Caller;
	m_hovered->OnEvent(gui_event);
}

void GUIModalMenu::leave()
{
	if (!m_hovered)
		return;
	SEvent gui_event{};
	gui_event.EventType = EET_GUI_EVENT;
	gui_event.GUIEvent.Caller = m_hovered.get();
	gui_event.GUIEvent.EventType = EGET_ELEMENT_LEFT;
	m_hovered->OnEvent(gui_event);
	m_hovered.reset();
}

#endif

void GUIModalMenu::getAllChildren(gui::IGUIElement* element, std::vector<gui::IGUIElement*>& all_children)
{
	all_children.push_back(element);

	const core::list<gui::IGUIElement*> &children = element->getChildren();

	for (gui::IGUIElement* child : children)
	{
		all_children.push_back(child);
		getAllChildren(child, all_children);
	}
}

gui::IGUIElement* GUIModalMenu::findClosestElement(const NavigationDirection direction,
												gui::IGUIElement* current_element)
{
	if (direction == ND_UP || direction == ND_DOWN)
	{
		gui::IGUIElement* combo_box = nullptr;

		if (current_element->getType() == gui::EGUIET_COMBO_BOX)
			combo_box = current_element;

		if (current_element->getParent() && current_element->getParent()->getType() == gui::EGUIET_COMBO_BOX)
			combo_box = current_element->getParent();

		if (combo_box)
		{
			for (gui::IGUIElement* child : combo_box->getChildren())
			{
				if (child->getType() == gui::EGUIET_LIST_BOX)
					return nullptr;
			}
		}
	}

	if (direction == ND_UP || direction == ND_DOWN)
	{
		gui::IGUIElement* list_view = nullptr;

		if (current_element->getType() == gui::EGUIET_ELEMENT)
		{
			for (gui::IGUIElement* child : current_element->getChildren())
			{
				if (child->getType() == gui::EGUIET_SCROLL_BAR)
				{
					list_view = current_element;
					break;
				}
			}
		}

		if (current_element->getParent() && current_element->getParent()->getType() == gui::EGUIET_ELEMENT)
		{
			for (gui::IGUIElement* child : current_element->getParent()->getChildren())
			{
				if (child->getType() == gui::EGUIET_SCROLL_BAR)
				{
					list_view = current_element->getParent();
					break;
				}
			}
		}

		if (list_view)
		{
			return nullptr;
		}
	}

	video::IVideoDriver* driver = Environment->getVideoDriver();
	const int distance_max = driver->getScreenSize().Width * 100;

	gui::IGUIElement* closest_widget = nullptr;
	int smallest_distance = distance_max;
	int smallest_wrapping_distance = distance_max;

	core::rect<s32> current_position = current_element->getAbsoluteClippingRect();
	int current_x = current_position.UpperLeftCorner.X;
	int current_y = current_position.UpperLeftCorner.Y;
	int current_w = current_position.getWidth();
	int current_h = current_position.getHeight();

	std::vector<gui::IGUIElement*> all_children;
	getAllChildren(this, all_children);

	for (gui::IGUIElement* child : all_children)
	{
		if (child == NULL || !child->isTabStop() || current_element == child ||
			!child->isTrulyVisible() || !child->isEnabled())
			continue;

		int distance = 0;
		int offset = 0;
		core::rect<s32> child_position = child->getAbsoluteClippingRect();
		int child_x = child_position.UpperLeftCorner.X;
		int child_y = child_position.UpperLeftCorner.Y;
		int child_w = child_position.getWidth();
		int child_h = child_position.getHeight();

		if (direction == ND_UP || direction == ND_DOWN)
		{
			if (direction == ND_UP)
				distance = current_y - (child_y + child_h);
			else
				distance = child_y - (current_y + current_h);

			int right_offset = std::max(0, child_x - current_x);
			int left_offset  = std::max(0, (current_x + current_w) - (child_x + child_w));
			offset = std::max(right_offset - left_offset, left_offset - right_offset);

			distance *= 100;
		}
		else if (direction == ND_LEFT || direction == ND_RIGHT)
		{
			if (direction == ND_LEFT)
				distance = current_x - (child_x + child_w);
			else
				distance = child_x - (current_x + current_w);

			int down_offset = std::max(0, child_y - current_y);
			int up_offset  = std::max(0, (current_y + current_h) - (child_y + child_h));
			offset = std::max(down_offset - up_offset, up_offset - down_offset);

			if (offset >= current_h)
				distance = distance_max;
		}

		distance += offset;

		if (distance < 0)
		{
			if (smallest_distance == distance_max && distance < smallest_wrapping_distance)
			{
				smallest_wrapping_distance = distance;
				closest_widget = child;
			}
		}
		else
		{
			if (distance < smallest_distance)
			{
				smallest_distance = distance;
				closest_widget = child;
			}
		}
	}

	return closest_widget;
}

bool GUIModalMenu::preprocessEvent(const SEvent &event)
{
	// clang-format off
#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
	// Enable text input events when edit box is focused
	if (event.EventType == EET_GUI_EVENT) {
		if (event.GUIEvent.EventType == irr::gui::EGET_ELEMENT_FOCUSED &&
			event.GUIEvent.Caller &&
			event.GUIEvent.Caller->getType() == irr::gui::EGUIET_EDIT_BOX) {
			if (porting::hasRealKeyboard())
				SDL_StartTextInput();
		}
		else if (event.GUIEvent.EventType == irr::gui::EGET_ELEMENT_FOCUS_LOST &&
			event.GUIEvent.Caller &&
			event.GUIEvent.Caller->getType() == irr::gui::EGUIET_EDIT_BOX) {
			if (porting::hasRealKeyboard() && SDL_IsTextInputActive())
				SDL_StopTextInput();
		}
	}
#endif

	if (event.EventType == EET_KEY_INPUT_EVENT)
	{
		if (event.KeyInput.PressedDown)
		{
			IrrlichtDevice* device = RenderingEngine::get_raw_device();
			core::position2d<s32> position = device->getCursorControl()->getPosition();

			gui::IGUIElement* closest_element = nullptr;
			gui::IGUIElement* hovered =
				Environment->getRootGUIElement()->getElementFromPoint(
					core::position2d<s32>(position.X, position.Y));

			if (event.KeyInput.Key == KEY_LEFT)
				closest_element = findClosestElement(ND_LEFT, hovered);
			else if (event.KeyInput.Key == KEY_RIGHT)
				closest_element = findClosestElement(ND_RIGHT, hovered);
			else if (event.KeyInput.Key == KEY_UP)
				closest_element = findClosestElement(ND_UP, hovered);
			else if (event.KeyInput.Key == KEY_DOWN)
				closest_element = findClosestElement(ND_DOWN, hovered);

			if (closest_element)
			{
				core::position2d<s32> position = closest_element->getAbsoluteClippingRect().getCenter();
				device->getCursorControl()->setPosition(position);
			}
		}
	}

#if defined(__ANDROID__) || defined(__IOS__)
	// display software keyboard when clicking edit boxes
	if (event.EventType == EET_MOUSE_INPUT_EVENT &&
			event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN) {
		gui::IGUIElement *hovered =
			Environment->getRootGUIElement()->getElementFromPoint(
				core::position2d<s32>(event.MouseInput.X, event.MouseInput.Y));
		if ((hovered) && (hovered->getType() == irr::gui::EGUIET_EDIT_BOX)) {
			bool retval = hovered->OnEvent(event);
			if (retval)
				Environment->setFocus(hovered);

			std::string field_name = getNameByID(hovered->getID());
			// read-only field
			if (field_name.empty() || porting::hasRealKeyboard())
				return retval;

			m_jni_field_name = field_name;
			/*~ Imperative, as in "Enter/type in text".
			Don't forget the space. */
			std::string message = gettext("Enter ");
			std::string label = wide_to_utf8(getLabelByID(hovered->getID()));
			if (label.empty())
				label = "text";
			message += gettext(label.c_str());
			message += ":";

			// single line text input
			int type = 2;

			// multi line text input
			if (((gui::IGUIEditBox *)hovered)->isMultiLineEnabled())
				type = 1;

			// passwords are always single line
			if (((gui::IGUIEditBox *)hovered)->isPasswordBox())
				type = 3;

			porting::showInputDialog(gettext("OK"), "",
				wide_to_utf8(((gui::IGUIEditBox *)hovered)->getText()), type);
			return retval;
		}
	}
#endif

#ifdef HAVE_TOUCHSCREENGUI
	if (event.EventType == EET_TOUCH_INPUT_EVENT) {
		irr_ptr<GUIModalMenu> holder;
		holder.grab(this); // keep this alive until return (it might be dropped downstream [?])

		switch ((int)event.TouchInput.touchedCount) {
		case 1: {
			if (event.TouchInput.Event == ETIE_PRESSED_DOWN || event.TouchInput.Event == ETIE_MOVED)
				m_pointer = v2s32(event.TouchInput.X, event.TouchInput.Y);
			if (event.TouchInput.Event == ETIE_PRESSED_DOWN)
				m_old_pointer = m_pointer;
			gui::IGUIElement *hovered = Environment->getRootGUIElement()->getElementFromPoint(core::position2d<s32>(m_pointer));
			if (event.TouchInput.Event == ETIE_PRESSED_DOWN)
				Environment->setFocus(hovered);
			if (m_hovered != hovered) {
				leave();
				enter(hovered);
			}
			gui::IGUIElement *focused = Environment->getFocus();
			SEvent mouse_event;
			if (!convertToMouseEvent(mouse_event, event.TouchInput.Event))
				return false;
			bool ret = preprocessEvent(mouse_event);
			if (!ret && focused)
				ret = focused->OnEvent(mouse_event);
			if (!ret && m_hovered && m_hovered != focused)
				ret = m_hovered->OnEvent(mouse_event);
			if (event.TouchInput.Event == ETIE_LEFT_UP) {
				m_pointer = v2s32(0, 0);
				leave();
			}
			return ret;
		}
		case 2: {
			if (event.TouchInput.Event != ETIE_PRESSED_DOWN)
				return true; // ignore
			auto focused = Environment->getFocus();
			if (!focused)
				return true;
			SEvent rclick_event{};
			rclick_event.EventType = EET_MOUSE_INPUT_EVENT;
			rclick_event.MouseInput.Event = EMIE_RMOUSE_PRESSED_DOWN;
			rclick_event.MouseInput.ButtonStates = EMBSM_LEFT | EMBSM_RIGHT;
			rclick_event.MouseInput.X = m_pointer.X;
			rclick_event.MouseInput.Y = m_pointer.Y;
			focused->OnEvent(rclick_event);
			rclick_event.MouseInput.Event = EMIE_RMOUSE_LEFT_UP;
			rclick_event.MouseInput.ButtonStates = EMBSM_LEFT;
			focused->OnEvent(rclick_event);
			return true;
		}
		default: // ignored
			return true;
		}
	}
#endif

	if (event.EventType == EET_MOUSE_INPUT_EVENT) {
		s32 x = event.MouseInput.X;
		s32 y = event.MouseInput.Y;
		gui::IGUIElement *hovered =
				Environment->getRootGUIElement()->getElementFromPoint(
						core::position2d<s32>(x, y));
		if (!isChild(hovered, this)) {
			if (DoubleClickDetection(event)) {
				return true;
			}
		}
	}
	return false;
}

#if defined(__ANDROID__) || defined(__IOS__)
bool GUIModalMenu::hasAndroidUIInput()
{
	// no dialog shown
	if (m_jni_field_name.empty())
		return false;

	// still waiting
	if (porting::getInputDialogState() == -1)
		return true;

	// no value abort dialog processing
	if (porting::getInputDialogState() != 0) {
		m_jni_field_name.clear();
		return false;
	}

	return true;
}
#endif
