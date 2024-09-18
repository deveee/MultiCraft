/*
Copyright (C) 2014 sapier
Copyright (C) 2018 srifqi, Muhammad Rifqi Priyo Susanto
		<muhammadrifqipriyosusanto@gmail.com>
Copyright (C) 2014-2022 Maksim Gamarnik [MoNTE48] Maksym48@pm.me

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

#include "touchscreengui.h"
#include "irrlichttypes.h"
#include "irr_v2d.h"
#include "log.h"
#include "client/keycode.h"
#include "settings.h"
#include "gettime.h"
#include "util/numeric.h"
#include "gettext.h"
#include "IGUIStaticText.h"
#include "IGUIFont.h"
#include "porting.h"
#include "client/guiscalingfilter.h"
#include "client/renderingengine.h"

#include <iostream>
#include <algorithm>

#include <ISceneCollisionManager.h>

using namespace irr::core;

const char *button_imagenames[] = {
	"jump_btn.png",
	"drop_btn.png",
	"down_btn.png",
	//"zoom.png",
	"aux_btn.png",
	"inventory_btn.png",
	"escape_btn.png",
	"minimap_btn.png",
	"rangeview_btn.png",
	"camera_btn.png",
	"chat_btn.png",
	"tab_btn.png",
	"overflow_btn.png"
};

const char *joystick_imagenames[] = {
	"joystick_off.png",
	"joystick_bg.png",
	"joystick_center.png"
};

// compare with GUIKeyChangeMenu::init_keys
static const char *button_titles[] = {
	N_("Jump"),
	N_("Drop"),
	N_("Sneak"),
	//N_("Zoom"),
	N_("Special"),
	N_("Inventory"),
	N_("Exit"),
	N_("Toggle minimap"),
	N_("Range select"),
	N_("Change camera"),
	N_("Chat"),
	N_("Tab"),
	N_("Overflow menu")
};

static const touch_gui_button_id overflow_buttons_id[] {
	chat_id, inventory_id, drop_id, camera_id, range_id, minimap_id
};


TouchScreenGUI *g_touchscreengui = nullptr;
bool TouchScreenGUI::m_active = true;

TouchScreenGUI::TouchScreenGUI(IrrlichtDevice *device, IEventReceiver *receiver):
	m_device(device),
	m_guienv(device->getGUIEnvironment()),
	m_receiver(receiver)
{
	m_touchscreen_threshold = g_settings->getU16("touchscreen_threshold");
	m_touch_sensitivity = rangelim(g_settings->getFloat("touch_sensitivity"), 0.1, 1.0);
	m_screensize = m_device->getVideoDriver()->getScreenSize();
	m_button_size = std::min(m_screensize.Y / 4.5f,
			RenderingEngine::getDisplayDensity() *
			g_settings->getFloat("hud_scaling") * 65.0f);
}

TouchScreenGUI::~TouchScreenGUI()
{
	if (!m_buttons_initialized)
		return;

	for (auto button : m_buttons) {
		if (button->guibutton) {
			button->guibutton->drop();
			button->guibutton = nullptr;
		}

		if (button->text) {
			button->text->drop();
			button->text = nullptr;
		}
		
		delete button;
	}

	if (m_overflow_bg)
		m_overflow_bg->drop();

	if (m_joystick.button_off) {
		m_joystick.button_off->drop();
		m_joystick.button_off = nullptr;
	}

	if (m_joystick.button_bg) {
		m_joystick.button_bg->drop();
		m_joystick.button_bg = nullptr;
	}

	if (m_joystick.button_center) {
		m_joystick.button_center->drop();
		m_joystick.button_center = nullptr;
	}
}

void TouchScreenGUI::loadButtonTexture(IGUIButton *btn, const char *path,
		const rect<s32> &button_rect)
{
	u32 tid;
	video::ITexture *texture = guiScalingImageButton(m_device->getVideoDriver(),
			m_texturesource->getTexture(path, &tid), button_rect.getWidth(),
			button_rect.getHeight());

	if (texture) {
		btn->setUseAlphaChannel(true);
		if (g_settings->getBool("gui_scaling_filter")) {
			rect<s32> txr_rect = rect<s32>(v2s32(0, 0), button_rect.getSize());
			btn->setImage(texture, txr_rect);
			btn->setPressedImage(texture, txr_rect);
			btn->setScaleImage(false);
		} else {
			btn->setImage(texture);
			btn->setPressedImage(texture);
			btn->setScaleImage(true);
		}
		btn->setDrawBorder(false);
		btn->setText(L"");
	}
}

void TouchScreenGUI::initButton(touch_gui_button_id id, const rect<s32> &button_rect,
		bool overflow_menu, const char *texture)
{
	button_info *btn = new button_info();
	btn->repeatcounter = -1;
	btn->overflow_menu = overflow_menu;
	btn->id = id;

	btn->guibutton = m_guienv->addButton(button_rect, nullptr);
	btn->guibutton->grab();
	btn->guibutton->setVisible(m_visible && !overflow_menu);
	const char *image = strcmp(texture, "") == 0 ? button_imagenames[id] : texture;
	loadButtonTexture(btn->guibutton, image, button_rect);

	const wchar_t *str = wgettext(button_titles[id]);
	btn->text = m_guienv->addStaticText(str, recti());
	btn->text->setTextAlignment(EGUIA_CENTER, EGUIA_UPPERLEFT);
	btn->text->setVisible(m_overflow_open);
	btn->text->grab();
	delete[] str;

	m_buttons.push_back(btn);
}

void TouchScreenGUI::initJoystickButton()
{
	const rect<s32> &button_off_rect = getButtonRect(joystick_off_id);
	m_joystick.button_off = m_guienv->addButton(button_off_rect, nullptr);
	m_joystick.button_off->setVisible(m_visible);
	m_joystick.button_off->grab();
	loadButtonTexture(m_joystick.button_off, joystick_imagenames[0], button_off_rect);
	
	const rect<s32> &button_bg_rect = getButtonRect(joystick_bg_id);
	m_joystick.button_bg = m_guienv->addButton(button_bg_rect, nullptr);
	m_joystick.button_bg->setVisible(false);
	m_joystick.button_bg->grab();
	loadButtonTexture(m_joystick.button_bg, joystick_imagenames[1], button_bg_rect);
	
	const rect<s32> &button_center_rect = getButtonRect(joystick_center_id);
	m_joystick.button_center = m_guienv->addButton(button_center_rect, nullptr);
	m_joystick.button_center->setVisible(false);
	m_joystick.button_center->grab();
	loadButtonTexture(m_joystick.button_center, joystick_imagenames[2], button_center_rect);
}

rect<s32> TouchScreenGUI::getButtonRect(touch_gui_button_id id)
{
	switch (id) {
	case joystick_off_id:
		return rect<s32>(m_button_size / 2,
				m_screensize.Y - m_button_size * 4.5,
				m_button_size * 4.5,
				m_screensize.Y - m_button_size / 2);
	case joystick_bg_id:
		return rect<s32>(m_button_size / 2,
				m_screensize.Y - m_button_size * 4.5,
				m_button_size * 4.5,
				m_screensize.Y - m_button_size / 2);
	case joystick_center_id:
		return rect<s32>(0, 0, m_button_size * 1.5, m_button_size * 1.5);
	case jump_id:
		return rect<s32>(m_screensize.X - m_button_size * 3.37,
				m_screensize.Y - m_button_size * 2.75,
				m_screensize.X - m_button_size * 1.87,
				m_screensize.Y - m_button_size * 1.25);
	case drop_id:
		return rect<s32>(m_screensize.X - m_button_size,
				m_screensize.Y / 2 - m_button_size * 1.5,
				m_screensize.X,
				m_screensize.Y / 2 - m_button_size / 2);
	case crunch_id:
		return rect<s32>(m_screensize.X - m_button_size * 3.38,
				m_screensize.Y - m_button_size * 0.75,
				m_screensize.X - m_button_size * 1.7,
				m_screensize.Y);
	case inventory_id:
		return rect<s32>(m_screensize.X - m_button_size * 1.7,
				m_screensize.Y - m_button_size * 1.5,
				m_screensize.X,
				m_screensize.Y);
	//case zoom_id:
	//	return rect<s32>(m_screensize.X - (1.25 * m_button_size),
	//			m_screensize.Y - (4 * m_button_size),
	//			m_screensize.X - (0.25 * m_button_size),
	//			m_screensize.Y - (3 * m_button_size));
	case special1_id:
		return rect<s32>(m_screensize.X - m_button_size * 1.8,
				m_screensize.Y - m_button_size * 4,
				m_screensize.X - m_button_size * 0.3,
				m_screensize.Y - m_button_size * 2.5);
	case escape_id:
		return rect<s32>(m_screensize.X / 2 - m_button_size * 2,
				0,
				m_screensize.X / 2 - m_button_size,
				m_button_size);
	case minimap_id:
		return rect<s32>(m_screensize.X / 2 - m_button_size,
				0,
				m_screensize.X / 2,
				m_button_size);
	case range_id:
		return rect<s32>(m_screensize.X / 2,
				0,
				m_screensize.X / 2 + m_button_size,
				m_button_size);
	case camera_id:
		return rect<s32>(m_screensize.X / 2 + m_button_size,
				0,
				m_screensize.X / 2 + m_button_size * 2,
				m_button_size);
	case chat_id:
		return rect<s32>(m_screensize.X - m_button_size * 1.25,
				0,
				m_screensize.X,
				m_button_size);
	case tab_id:
		return rect<s32>(m_screensize.X - m_button_size * 1.25,
				m_button_size,
				m_screensize.X,
				m_button_size * 2);
	case overflow_id:
		return rect<s32>(m_screensize.X - m_button_size * 1.25,
				m_button_size * 2,
				m_screensize.X,
				m_button_size * 3);
	default:
		return rect<s32>(0, 0, 0, 0);
	}
}

void TouchScreenGUI::updateButtons()
{
	v2u32 screensize = m_device->getVideoDriver()->getScreenSize();

	if (screensize != m_screensize) {
		m_screensize = screensize;
		m_button_size = std::min(m_screensize.Y / 4.5f,
				RenderingEngine::getDisplayDensity() *
				g_settings->getFloat("hud_scaling") * 65.0f);

		for (auto button : m_buttons) {
			if (button->overflow_menu)
				continue;

			if (button->guibutton) {
				rect<s32> rect = getButtonRect(button->id);
				button->guibutton->setRelativePosition(rect);
			}
		}

		if (m_joystick.button_off) {
			rect<s32> rect = getButtonRect(joystick_off_id);
			m_joystick.button_off->setRelativePosition(rect);
		}

		if (m_joystick.button_bg) {
			rect<s32> rect = getButtonRect(joystick_bg_id);
			m_joystick.button_bg->setRelativePosition(rect);
		}

		if (m_joystick.button_center) {
			rect<s32> rect = getButtonRect(joystick_center_id);
			m_joystick.button_center->setRelativePosition(rect);
		}

		rebuildOverFlowMenu();
	}
}

void TouchScreenGUI::rebuildOverFlowMenu()
{
	recti rect(v2s32(0, 0), dimension2du(m_screensize));
	m_overflow_bg->setRelativePosition(rect);

	s32 cols = 4;
	s32 rows = 3;
	assert((s32)ARRLEN(overflow_buttons_id) <= cols * rows);

	v2s32 size(m_button_size, m_button_size);
	v2s32 spacing(m_screensize.X / (cols + 1), m_screensize.Y / (rows + 1));
	v2s32 pos(spacing);

	for (auto button : m_buttons) {
		if (!button->overflow_menu)
			continue;

		recti button_rect(pos - size / 2, dimension2du(size.X, size.Y));
		if (button_rect.LowerRightCorner.X > (s32)m_screensize.X) {
			pos.X = spacing.X;
			pos.Y += spacing.Y;
			button_rect = recti(pos - size / 2, dimension2du(size.X, size.Y));
		}

		button->guibutton->setRelativePosition(button_rect);

		const wchar_t *str = wgettext(button_titles[button->id]);
		IGUIFont *font = button->text->getActiveFont();
		dimension2du dim = font->getDimension(str);
		dim = dimension2du(dim.Width * 1.25f, dim.Height * 1.25f); // avoid clipping
		recti text_rect = recti(pos.X - dim.Width / 2, pos.Y + size.Y / 2,
				pos.X + dim.Width / 2, pos.Y + size.Y / 2 + dim.Height);
		button->text->setRelativePosition(text_rect);
		delete[] str;

		pos.X += spacing.X;
	}
}

void TouchScreenGUI::init(ISimpleTextureSource *tsrc, bool simple_singleplayer_mode)
{
	assert(tsrc);

	m_texturesource = tsrc;
	m_simple_singleplayer_mode = simple_singleplayer_mode;

	initJoystickButton();

	initButton(jump_id, getButtonRect(jump_id));
	initButton(drop_id, getButtonRect(drop_id));
	initButton(crunch_id, getButtonRect(crunch_id));
	initButton(inventory_id, getButtonRect(inventory_id));
	// initButton(zoom_id, getButtonRect(zoom_id));
	initButton(special1_id, getButtonRect(special1_id));
	initButton(escape_id, getButtonRect(escape_id));
	initButton(minimap_id, getButtonRect(minimap_id));
	initButton(range_id, getButtonRect(range_id));
	initButton(camera_id, getButtonRect(camera_id));

	if (m_simple_singleplayer_mode) {
		initButton(chat_id, getButtonRect(chat_id), false, "chat_btn.png");
	} else {
		initButton(chat_id, getButtonRect(chat_id), false, "chat_mp_btn.png");
		initButton(tab_id, getButtonRect(tab_id));
	}

	initButton(overflow_id, getButtonRect(overflow_id));

	m_overflow_bg = m_guienv->addStaticText(L"", recti());
	m_overflow_bg->setBackgroundColor(video::SColor(140, 0, 0, 0));
	m_overflow_bg->setVisible(m_overflow_open);
	m_overflow_bg->grab();

	for (auto id : overflow_buttons_id) {
		initButton(id, recti(), true);
	}

	rebuildOverFlowMenu();

	m_buttons_initialized = true;
}

bool TouchScreenGUI::isHUDButton(const SEvent &event)
{
	// check if hud item is pressed
	for (auto &hud_rect : m_hud_rects) {
		if (hud_rect.second.isPointInside(v2s32(event.TouchInput.X,
				event.TouchInput.Y))) {
			auto *translated = new SEvent();
			memset(translated, 0, sizeof(SEvent));
			translated->EventType = irr::EET_KEY_INPUT_EVENT;
			translated->KeyInput.Key         = (irr::EKEY_CODE) (KEY_KEY_1 + hud_rect.first);
			translated->KeyInput.Control     = false;
			translated->KeyInput.Shift       = false;
			translated->KeyInput.PressedDown = true;
			m_receiver->OnEvent(*translated);
			m_hud_ids[event.TouchInput.ID]   = translated->KeyInput.Key;
			delete translated;
			return true;
		}
	}
	return false;
}

void TouchScreenGUI::handleReleaseEvent(size_t evt_id)
{
	//~ if (m_has_move_id && evt_id == m_move_id) {
		//~ // handle the point used for moving view
		//~ m_has_move_id = false;

		//~ // if this pointer issued a mouse event issue symmetric release here
		//~ if (m_move_sent_as_mouse_event) {
			//~ auto *translated = new SEvent;
			//~ memset(translated, 0, sizeof(SEvent));
			//~ translated->EventType               = EET_MOUSE_INPUT_EVENT;
			//~ translated->MouseInput.X            = m_move_downlocation.X;
			//~ translated->MouseInput.Y            = m_move_downlocation.Y;
			//~ translated->MouseInput.Shift        = false;
			//~ translated->MouseInput.Control      = false;
			//~ translated->MouseInput.ButtonStates = 0;
			//~ translated->MouseInput.Event        = EMIE_LMOUSE_LEFT_UP;
			//~ m_receiver->OnEvent(*translated);
			//~ delete translated;
		//~ } else if (!m_move_has_really_moved) {
			//~ auto *translated = new SEvent;
			//~ memset(translated, 0, sizeof(SEvent));
			//~ translated->EventType               = EET_MOUSE_INPUT_EVENT;
			//~ translated->MouseInput.X            = m_move_downlocation.X;
			//~ translated->MouseInput.Y            = m_move_downlocation.Y;
			//~ translated->MouseInput.Shift        = false;
			//~ translated->MouseInput.Control      = false;
			//~ translated->MouseInput.ButtonStates = 0;
			//~ translated->MouseInput.Event        = EMIE_LMOUSE_LEFT_UP;
			//~ m_receiver->OnEvent(*translated);
			//~ delete translated;
			//~ quickTapDetection();
			//~ m_shootline = m_device
						//~ ->getSceneManager()
						//~ ->getSceneCollisionManager()
						//~ ->getRayFromScreenCoordinates(
							//~ v2s32(m_move_downlocation.X, m_move_downlocation.Y));
		//~ }
	//~ }

	//~ // handle joystick
	//~ else if (m_has_joystick_id && evt_id == m_joystick_id) {
		//~ m_has_joystick_id = false;

		//~ // reset joystick
		//~ for (u32 i = 0; i < 4; i++)
			//~ m_joystick_status[i] = false;
		//~ applyJoystickStatus();

		//~ if (m_visible)
			//~ m_joystick_btn_off->guibutton->setVisible(true);
		//~ m_joystick_btn_bg->guibutton->setVisible(false);
		//~ m_joystick_btn_center->guibutton->setVisible(false);
	//~ } else {
		//~ infostream
			//~ << "TouchScreenGUI::translateEvent released unknown button: "
			//~ << evt_id << std::endl;
	//~ }
}

void TouchScreenGUI::moveJoystick(s32 x, s32 y) {
	s32 dx = x - m_button_size * 5 / 2;
	s32 dy = y - m_screensize.Y + m_button_size * 5 / 2;

	double distance = sqrt(dx * dx + dy * dy);

	// angle in degrees
	double angle = acos(dx / distance) * 180 / M_PI;
	if (dy < 0)
		angle *= -1;
	// rotate to make comparing easier
	angle = fmod(angle + 180 + 22.5, 360);

	if (distance > m_button_size * 1.5) {
		s32 ndx = m_button_size * dx / distance * 1.5f - m_button_size / 2.0f * 1.5f;
		s32 ndy = m_button_size * dy / distance * 1.5f - m_button_size / 2.0f * 1.5f;
		m_joystick.button_center->setRelativePosition(v2s32(
			m_button_size * 5 / 2 + ndx,
			m_screensize.Y - m_button_size * 5 / 2 + ndy));
	} else {
		m_joystick.button_center->setRelativePosition(v2s32(
				x - m_button_size / 2.0f * 1.5f,
				y - m_button_size / 2.0f * 1.5f));
	}

	//~ m_joystick_has_really_moved = true;
	//~ double distance = sqrt(dx * dx + dy * dy);

	//~ // angle in degrees
	//~ double angle = acos(dx / distance) * 180 / M_PI;
	//~ if (dy < 0)
		//~ angle *= -1;
	//~ // rotate to make comparing easier
	//~ angle = fmod(angle + 180 + 22.5, 360);

	//~ // reset state before applying
	//~ for (bool & joystick_status : m_joystick_status)
		//~ joystick_status = false;

	//~ if (distance <= m_touchscreen_threshold) {
		//~ // do nothing
	//~ } else if (angle < 45)
		//~ m_joystick_status[j_left] = true;
	//~ else if (angle < 90) {
		//~ m_joystick_status[j_forward] = true;
		//~ m_joystick_status[j_left] = true;
	//~ } else if (angle < 135)
		//~ m_joystick_status[j_forward] = true;
	//~ else if (angle < 180) {
		//~ m_joystick_status[j_forward] = true;
		//~ m_joystick_status[j_right] = true;
	//~ } else if (angle < 225)
		//~ m_joystick_status[j_right] = true;
	//~ else if (angle < 270) {
		//~ m_joystick_status[j_backward] = true;
		//~ m_joystick_status[j_right] = true;
	//~ } else if (angle < 315)
		//~ m_joystick_status[j_backward] = true;
	//~ else if (angle <= 360) {
		//~ m_joystick_status[j_backward] = true;
		//~ m_joystick_status[j_left] = true;
	//~ }

	//~ if (distance > m_button_size * 1.5) {
		//~ m_joystick_status[j_special1] = true;
		//~ // move joystick "button"
		//~ s32 ndx = m_button_size * dx / distance * 1.5f - m_button_size / 2.0f * 1.5f;
		//~ s32 ndy = m_button_size * dy / distance * 1.5f - m_button_size / 2.0f * 1.5f;
		//~ if (m_fixed_joystick) {
			//~ m_joystick_btn_center->guibutton->setRelativePosition(v2s32(
				//~ m_button_size * 5 / 2 + ndx,
				//~ m_screensize.Y - m_button_size * 5 / 2 + ndy));
		//~ } else {
			//~ m_joystick_btn_center->guibutton->setRelativePosition(v2s32(
				//~ m_pointerpos[event.TouchInput.ID].X + ndx,
				//~ m_pointerpos[event.TouchInput.ID].Y + ndy));
		//~ }
	//~ } else {
		//~ m_joystick_btn_center->guibutton->setRelativePosition(v2s32(
				//~ event.TouchInput.X - m_button_size / 2.0f * 1.5f,
				//~ event.TouchInput.Y - m_button_size / 2.0f * 1.5f));
	//~ }
}

void TouchScreenGUI::preprocessEvent(const SEvent &event)
{
	if (!m_buttons_initialized)
		return;

	if (!m_visible)
		return;

	if (event.EventType != EET_TOUCH_INPUT_EVENT)
		return;

	s32 id = (unsigned int)event.TouchInput.ID;
	s32 x = event.TouchInput.X;
	s32 y = event.TouchInput.Y;

	m_events[id].id = id;
	m_events[id].x = x;
	m_events[id].y = y;

	if (event.TouchInput.Event == ETIE_PRESSED_DOWN) {
		bool overflow_btn_pressed = false;
		
		for (auto button : m_buttons) {
			if (m_overflow_open != button->overflow_menu)
				continue;
	
			if (button->guibutton->isPointInside(core::position2d<s32>(x, y))) {
				m_events[id].pressed = true;
				button->pressed = true;
				button->event_id = id;
				button->repeatcounter = 0;
				
				if (button->id == overflow_id) {
					overflow_btn_pressed = true;
				}
			}
		}
		
		if (m_joystick.button_off->isPointInside(core::position2d<s32>(x, y))) {
			m_events[id].pressed = true;
			m_joystick.button_off->setVisible(false);
			m_joystick.button_bg->setVisible(true);
			m_joystick.button_center->setVisible(true);
			m_joystick.pressed = true;
			m_joystick.event_id = id;
			
			moveJoystick(x, y);
		}
		
		if (overflow_btn_pressed || (m_overflow_open && !m_events[id].pressed)) {
			toggleOverflowMenu();
		}

	} else if (event.TouchInput.Event == ETIE_LEFT_UP) {
		m_events[id].pressed = false;
		
		for (auto button : m_buttons) {
			if (m_overflow_open != button->overflow_menu)
				continue;
	
			if (button->event_id == id) {
				button->pressed = false;
				button->event_id = -1;
				button->repeatcounter = -1;
			}
			
			if (m_joystick.event_id == id) {
				m_joystick.button_off->setVisible(true);
				m_joystick.button_bg->setVisible(false);
				m_joystick.button_center->setVisible(false);
				m_joystick.pressed = false;
				m_joystick.event_id = -1;
			}
		}
		
	} else if (event.TouchInput.Event == ETIE_MOVED) {
		if (m_events[id].pressed) {
			for (auto button : m_buttons) {
				if (m_overflow_open != button->overflow_menu)
					continue;
		
				if (button->guibutton->isPointInside(core::position2d<s32>(x, y))) {
					button->pressed = true;
					button->event_id = id;
					button->repeatcounter = 0;
				} else if (button->event_id == id) {
					button->pressed = false;
					button->event_id = -1;
					button->repeatcounter = -1;
				}
			}
				
			if (m_joystick.event_id == id) {
				moveJoystick(x, y);
			}
		}
	}
}

void TouchScreenGUI::translateEvent(const SEvent &event)
{
	if (!m_buttons_initialized)
		return;

	if (!m_visible) {
		infostream
			<< "TouchScreenGUI::translateEvent got event but not visible!"
			<< std::endl;
		return;
	}

	if (event.EventType != EET_TOUCH_INPUT_EVENT)
		return;

	if (event.TouchInput.Event == ETIE_PRESSED_DOWN) {
		/*
		 * Add to own copy of event list...
		 * android would provide this information but Irrlicht guys don't
		 * wanna design a efficient interface
		 */
		id_status toadd{};
		toadd.id = event.TouchInput.ID;
		toadd.X  = event.TouchInput.X;
		toadd.Y  = event.TouchInput.Y;

		size_t eventID = event.TouchInput.ID;

		//~ touch_gui_button_id button =
				//~ getButtonID(event.TouchInput.X, event.TouchInput.Y);

		// handle button events
		//~ if (button == overflow_id)  {
			//~ toggleOverflowMenu();
		//~ } else if (button != unknown_id) {
			//~ handleButtonEvent(button, eventID, true);

		//~ } else if (isHUDButton(event)) {

		//~ } else {
			//~ if (m_overflow_open) {
				//~ toggleOverflowMenu();
			//~ }

			//~ s32 dxj = event.TouchInput.X - m_button_size * 5.0f / 2.0f;
			//~ s32 dyj = event.TouchInput.Y - m_screensize.Y + m_button_size * 5.0f / 2.0f;

			//~ /* Select joystick when left 1/3 of screen dragged or
			 //~ * when joystick tapped (fixed joystick position)
			 //~ */
			//~ bool inside_joystick =
				//~ m_fixed_joystick
					//~ ? dxj * dxj + dyj * dyj <= m_button_size * m_button_size * 1.5 * 1.5
					//~ : event.TouchInput.X < m_screensize.X / 3.0f;
			//~ if (inside_joystick) {
				//~ // If we don't already have a starting point for joystick make this the one.
				//~ if (!m_has_joystick_id) {
					//~ m_has_joystick_id           = true;
					//~ m_joystick_id               = event.TouchInput.ID;
					//~ m_joystick_has_really_moved = false;

					//~ m_joystick_btn_off->guibutton->setVisible(false);
					//~ m_joystick_btn_bg->guibutton->setVisible(true);
					//~ m_joystick_btn_center->guibutton->setVisible(true);

					//~ // If it's a fixed joystick, don't move the joystick "button".
					//~ if (m_fixed_joystick) {
						//~ moveJoystick(event, dxj, dyj);
					//~ } else {
						//~ m_joystick_btn_bg->guibutton->setRelativePosition(v2s32(
								//~ event.TouchInput.X - m_button_size * 3.0f / 1.5f,
								//~ event.TouchInput.Y - m_button_size * 3.0f / 1.5f));
						//~ m_joystick_btn_center->guibutton->setRelativePosition(v2s32(
								//~ event.TouchInput.X - m_button_size / 2.0f * 1.5f,
								//~ event.TouchInput.Y - m_button_size / 2.0f * 1.5f));
					//~ }
				//~ }
			//~ } else {
				//~ // If we don't already have a moving point make this the moving one.
				//~ if (!m_has_move_id) {
					//~ m_has_move_id              = true;
					//~ m_move_id                  = event.TouchInput.ID;
					//~ m_move_has_really_moved    = false;
					//~ m_move_downtime            = porting::getTimeMs();
					//~ m_move_downlocation        = v2s32(event.TouchInput.X, event.TouchInput.Y);
					//~ m_move_sent_as_mouse_event = false;
				//~ }
			//~ }
		//~ }

		//~ m_pointerpos[event.TouchInput.ID] = v2s32(event.TouchInput.X, event.TouchInput.Y);
	//~ } else if (event.TouchInput.Event == ETIE_LEFT_UP) {
		//~ verbosestream
			//~ << "Up event for pointerid: " << event.TouchInput.ID << std::endl;
		//~ handleReleaseEvent(event.TouchInput.ID);
	//~ } else if (event.TouchInput.Event == ETIE_MOVED) {
		//~ if (m_pointerpos[event.TouchInput.ID] ==
				//~ v2s32(event.TouchInput.X, event.TouchInput.Y))
			//~ return;

		//~ if (m_has_move_id) {
			//~ if ((event.TouchInput.ID == m_move_id) &&
				//~ (!m_move_sent_as_mouse_event)) {

				//~ double distance = sqrt(
						//~ (m_pointerpos[event.TouchInput.ID].X - event.TouchInput.X) *
						//~ (m_pointerpos[event.TouchInput.ID].X - event.TouchInput.X) +
						//~ (m_pointerpos[event.TouchInput.ID].Y - event.TouchInput.Y) *
						//~ (m_pointerpos[event.TouchInput.ID].Y - event.TouchInput.Y));

				//~ if ((distance > m_touchscreen_threshold) ||
						//~ (m_move_has_really_moved)) {
					//~ m_move_has_really_moved = true;
					//~ s32 X = event.TouchInput.X;
					//~ s32 Y = event.TouchInput.Y;

					//~ // update camera_yaw and camera_pitch
					//~ s32 dx = X - m_pointerpos[event.TouchInput.ID].X;
					//~ s32 dy = Y - m_pointerpos[event.TouchInput.ID].Y;

					//~ m_camera_yaw_change -= dx * m_touch_sensitivity;
					//~ m_camera_pitch = MYMIN(MYMAX(m_camera_pitch + (dy * m_touch_sensitivity), -180), 180);

					//~ // update shootline
					//~ m_shootline = m_device
							//~ ->getSceneManager()
							//~ ->getSceneCollisionManager()
							//~ ->getRayFromScreenCoordinates(v2s32(X, Y));
					//~ m_pointerpos[event.TouchInput.ID] = v2s32(X, Y);
				//~ }
			//~ } else if ((event.TouchInput.ID == m_move_id) &&
					//~ (m_move_sent_as_mouse_event)) {
				//~ m_shootline = m_device
						//~ ->getSceneManager()
						//~ ->getSceneCollisionManager()
						//~ ->getRayFromScreenCoordinates(
								//~ v2s32(event.TouchInput.X, event.TouchInput.Y));
			//~ }
		//~ }

		//~ if (m_has_joystick_id && event.TouchInput.ID == m_joystick_id) {
			//~ s32 X = event.TouchInput.X;
			//~ s32 Y = event.TouchInput.Y;

			//~ s32 dx = X - m_pointerpos[event.TouchInput.ID].X;
			//~ s32 dy = Y - m_pointerpos[event.TouchInput.ID].Y;
			//~ if (m_fixed_joystick) {
				//~ dx = X - m_button_size * 5 / 2;
				//~ dy = Y - m_screensize.Y + m_button_size * 5 / 2;
			//~ }

			//~ s32 dxj = event.TouchInput.X - m_button_size * 5.0f / 2.0f;
			//~ s32 dyj = event.TouchInput.Y - m_screensize.Y + m_button_size * 5.0f / 2.0f;
			//~ bool inside_joystick = (dxj * dxj + dyj * dyj <= m_button_size * m_button_size * 1.5 * 1.5);

			//~ if (m_joystick_has_really_moved || inside_joystick ||
					//~ (!m_fixed_joystick &&
					//~ dx * dx + dy * dy > m_touchscreen_threshold * m_touchscreen_threshold)) {
				//~ moveJoystick(event, dx, dy);
			//~ }
		//~ }

		//~ if (!m_has_move_id && !m_has_joystick_id)
			//~ handleChangedButton(event);
	}
}

// Punch or left click
bool TouchScreenGUI::quickTapDetection()
{
	m_key_events[0].down_time = m_key_events[1].down_time;
	m_key_events[0].x         = m_key_events[1].x;
	m_key_events[0].y         = m_key_events[1].y;

	// ignore the occasional touch
	u64 delta = porting::getDeltaMs(m_move_downtime, porting::getTimeMs());
	if (delta < 50)
		return false;

	auto *translated = new SEvent();
	memset(translated, 0, sizeof(SEvent));
	translated->EventType               = EET_MOUSE_INPUT_EVENT;
	translated->MouseInput.X            = m_key_events[0].x;
	translated->MouseInput.Y            = m_key_events[0].y;
	translated->MouseInput.Shift        = false;
	translated->MouseInput.Control      = false;
	translated->MouseInput.ButtonStates = EMBSM_RIGHT;

	// update shootline
	m_shootline = m_device
			->getSceneManager()
			->getSceneCollisionManager()
			->getRayFromScreenCoordinates(v2s32(m_key_events[0].x, m_key_events[0].y));

	translated->MouseInput.Event = EMIE_RMOUSE_PRESSED_DOWN;
	verbosestream << "TouchScreenGUI::translateEvent right click press" << std::endl;
	m_receiver->OnEvent(*translated);

	translated->MouseInput.ButtonStates = 0;
	translated->MouseInput.Event = EMIE_RMOUSE_LEFT_UP;
	verbosestream << "TouchScreenGUI::translateEvent right click release" << std::endl;
	m_receiver->OnEvent(*translated);
	delete translated;
	return true;
}

void TouchScreenGUI::applyJoystickStatus()
{
	//~ for (u32 i = 0; i < 5; i++) {
		//~ if (i == 4 && !m_joystick_triggers_special1)
			//~ continue;

		//~ SEvent translated{};
		//~ translated.EventType            = irr::EET_KEY_INPUT_EVENT;
		//~ translated.KeyInput.Key         = id2keycode(m_joystick_names[i]);
		//~ translated.KeyInput.PressedDown = false;
		//~ m_receiver->OnEvent(translated);

		//~ if (m_joystick_status[i]) {
			//~ translated.KeyInput.PressedDown = true;
			//~ m_receiver->OnEvent(translated);
		//~ }
	//~ }
}

void TouchScreenGUI::step(float dtime)
{
	updateButtons();

	// simulate keyboard repeats
	//~ for (auto &button : m_buttons) {
		//~ if (!button.ids.empty()) {
			//~ button.repeatcounter += dtime;

			//~ if (button.repeatcounter < button.repeatdelay)
				//~ continue;

			//~ button.repeatcounter            = 0;
			//~ SEvent translated;
			//~ memset(&translated, 0, sizeof(SEvent));
			//~ translated.EventType            = irr::EET_KEY_INPUT_EVENT;
			//~ translated.KeyInput.Key         = button.keycode;
			//~ translated.KeyInput.PressedDown = false;
			//~ m_receiver->OnEvent(translated);

			//~ translated.KeyInput.PressedDown = true;
			//~ m_receiver->OnEvent(translated);
		//~ }
	//~ }

	//~ // joystick
	//~ for (u32 i = 0; i < 4; i++) {
		//~ if (m_joystick_status[i]) {
			//~ applyJoystickStatus();
			//~ break;
		//~ }
	//~ }

	//~ // if a new placed pointer isn't moved for some time start digging
	//~ if (m_has_move_id &&
			//~ (!m_move_has_really_moved) &&
			//~ (!m_move_sent_as_mouse_event)) {
		//~ u64 delta = porting::getDeltaMs(m_move_downtime, porting::getTimeMs());

		//~ if (delta > MIN_DIG_TIME_MS) {
			//~ m_shootline = m_device
					//~ ->getSceneManager()
					//~ ->getSceneCollisionManager()
					//~ ->getRayFromScreenCoordinates(
							//~ v2s32(m_move_downlocation.X, m_move_downlocation.Y));

			//~ SEvent translated;
			//~ memset(&translated, 0, sizeof(SEvent));
			//~ translated.EventType               = EET_MOUSE_INPUT_EVENT;
			//~ translated.MouseInput.X            = m_move_downlocation.X;
			//~ translated.MouseInput.Y            = m_move_downlocation.Y;
			//~ translated.MouseInput.Shift        = false;
			//~ translated.MouseInput.Control      = false;
			//~ translated.MouseInput.ButtonStates = EMBSM_LEFT;
			//~ translated.MouseInput.Event        = EMIE_LMOUSE_PRESSED_DOWN;
			//~ verbosestream << "TouchScreenGUI::step left click press" << std::endl;
			//~ m_receiver->OnEvent(translated);
			//~ m_move_sent_as_mouse_event         = true;
		//~ }
	//~ }
}

void TouchScreenGUI::resetHud()
{
	m_hud_rects.clear();
}

void TouchScreenGUI::registerHudItem(s32 index, const rect<s32> &rect)
{
	m_hud_rects[index] = rect;
}

void TouchScreenGUI::Toggle(bool visible)
{
	m_visible = visible;

	if (!m_buttons_initialized)
		return;

	for (auto button : m_buttons) {
		if (button->guibutton)
			button->guibutton->setVisible(m_visible && m_overflow_open == button->overflow_menu);
		if (button->text)
			button->text->setVisible(m_visible && m_overflow_open == button->overflow_menu);
	}

	if (m_joystick.button_off)
		m_joystick.button_off->setVisible(m_visible && !m_overflow_open);

	if (m_overflow_bg)
		m_overflow_bg->setVisible(m_visible && m_overflow_open);

	if (!visible)
		reset();
}

void TouchScreenGUI::toggleOverflowMenu()
{
	reset();
	m_overflow_open = !m_overflow_open;
	Toggle(m_visible);
}

void TouchScreenGUI::hide()
{
	if (!m_visible)
		return;

	Toggle(false);
}

void TouchScreenGUI::show()
{
	if (m_visible)
		return;

	Toggle(true);
}

void TouchScreenGUI::reset()
{
	for (auto button : m_buttons) {
		button->pressed = false;
		button->event_id = -1;
		button->repeatcounter = -1;
	}
	
	for (auto &event : m_events) {
	    event.id = 0;
	    event.pressed = false;
	    event.x = 0;
	    event.y = 0;
	}
}

void TouchScreenGUI::handleReleaseAll()
{
	reset();
}
