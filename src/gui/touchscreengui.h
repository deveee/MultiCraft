/*
Copyright (C) 2014 sapier
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

#pragma once

#include "IGUIStaticText.h"
#include "irrlichttypes.h"
#include <IEventReceiver.h>
#include <IGUIButton.h>
#include <IGUIEnvironment.h>
#include <IrrlichtDevice.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

#include "client/tile.h"
#include "client/game.h"

using namespace irr;
using namespace irr::core;
using namespace irr::gui;

typedef enum
{
	unknown_id = -1,
	jump_id = 0,
	drop_id,
	crunch_id,
	// zoom_id,
	special1_id,
	inventory_id,
	escape_id,
	minimap_id,
	range_id,
	camera_id,
	chat_id,
	tab_id,
	overflow_id,
	// settings_starter_id,
	// rare_controls_starter_id,
	// fly_id,
	// noclip_id,
	// fast_id,
	// debug_id,
	// toggle_chat_id,
	forward_id,
	backward_id,
	left_id,
	right_id,
	joystick_off_id,
	joystick_bg_id,
	joystick_center_id
} touch_gui_button_id;

typedef enum
{
	j_forward = 0,
	j_backward,
	j_left,
	j_right,
	j_special1
} touch_gui_joystick_move_id;

#define MIN_DIG_TIME_MS 500
#define BUTTON_REPEAT_DELAY 1.0f
#define NUMBER_OF_TOUCHES 10

struct button_info
{
	IGUIButton *guibutton = nullptr;
	IGUIStaticText *text = nullptr;
	touch_gui_button_id id = unknown_id;
	float repeatcounter = -1;
	bool overflow_menu = false;
	bool pressed = false;
	s32 event_id = -1;
};

struct joystick_info
{
	IGUIButton *button_off = nullptr;
	IGUIButton *button_bg = nullptr;
	IGUIButton *button_center = nullptr;
	bool joystick_has_really_moved = false;
	bool pressed = false;
	s32 event_id = -1;
};

struct TouchEvent
{
    s32 id = 0;
    bool pressed = false;
    s32 x = 0;
    s32 y = 0;
};

class TouchScreenGUI
{
public:
	TouchScreenGUI(IrrlichtDevice *device, IEventReceiver *receiver);
	~TouchScreenGUI();

	void preprocessEvent(const SEvent &event);
	void translateEvent(const SEvent &event);

	void init(ISimpleTextureSource *tsrc, bool simple_singleplayer_mode);

	double getYawChange()
	{
		double res = m_camera_yaw_change;
		m_camera_yaw_change = 0;
		return res;
	}

	double getPitchChange()
	{
		double res = m_camera_pitch;
		m_camera_pitch = 0;
		return res;
	}

	/*
	 * Returns a line which describes what the player is pointing at.
	 * The starting point and looking direction are significant,
	 * the line should be scaled to match its length to the actual distance
	 * the player can reach.
	 * The line starts at the camera and ends on the camera's far plane.
	 * The coordinates do not contain the camera offset.
	 */
	line3d<f32> getShootline() { return m_shootline; }

	void step(float dtime);
	void resetHud();
	void registerHudItem(s32 index, const rect<s32> &rect);
	void Toggle(bool visible);

	void hide();
	void show();
	void reset();

	// handle all buttons
	void handleReleaseAll();

	// returns true if device is active
	static bool isActive() { return m_active; }

	// set device active state
	static void setActive(bool active) { m_active = active; }

private:
	IrrlichtDevice *m_device;
	IGUIEnvironment *m_guienv;
	IEventReceiver *m_receiver;
	ISimpleTextureSource *m_texturesource;
	v2u32 m_screensize;
	s32 m_button_size;
	double m_touchscreen_threshold;
	double m_touch_sensitivity;
	std::map<int, rect<s32>> m_hud_rects;
	std::map<size_t, irr::EKEY_CODE> m_hud_ids;
	bool m_visible = true; // is the gui visible
	bool m_buttons_initialized = false;
	bool m_simple_singleplayer_mode = false;

	// value in degree
	double m_camera_yaw_change = 0.0;
	double m_camera_pitch = 0.0;

	/*
	 * A line starting at the camera and pointing towards the
	 * selected object.
	 * The line ends on the camera's far plane.
	 * The coordinates do not contain the camera offset.
	 */
	line3d<f32> m_shootline;

	bool m_has_move_id = false;
	size_t m_move_id;
	bool m_move_has_really_moved = false;
	u64 m_move_downtime = 0;
	bool m_move_sent_as_mouse_event = false;
	v2s32 m_move_downlocation = v2s32(-10000, -10000);

	std::vector<button_info*> m_buttons;
	joystick_info m_joystick;

	bool m_overflow_open = false;
	IGUIStaticText *m_overflow_bg = nullptr;
	std::vector<IGUIStaticText *> m_overflow_button_titles;
	
	std::array<TouchEvent, NUMBER_OF_TOUCHES> m_events;
	
	void loadButtonTexture(IGUIButton *btn, const char *path,
			const rect<s32> &button_rect);

	void toggleOverflowMenu();

	void initButton(touch_gui_button_id id, const rect<s32> &button_rect,
			bool overflow_menu = false, const char *texture = "");

	void initJoystickButton();

	rect<s32> getButtonRect(touch_gui_button_id id);

	void updateButtons();

	void rebuildOverFlowMenu();

	void moveJoystick(s32 x, s32 y);

	struct id_status
	{
		size_t id;
		s32 X;
		s32 Y;
	};

	// handle pressed hud buttons
	bool isHUDButton(const SEvent &event);

	// handle quick taps
	bool quickTapDetection();

	// handle release event
	void handleReleaseEvent(size_t evt_id);

	// apply joystick status
	void applyJoystickStatus();

	// long-click detection variables
	struct key_event
	{
		u64 down_time;
		s32 x;
		s32 y;
	};

	// array for long-click detection
	key_event m_key_events[2];

	// device active state
	static bool m_active;
};

extern TouchScreenGUI *g_touchscreengui;
