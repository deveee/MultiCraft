--Minetest
--Copyright (C) 2014 sapier
--
--This program is free software; you can redistribute it and/or modify
--it under the terms of the GNU Lesser General Public License as published by
--the Free Software Foundation; either version 3.0 of the License, or
--(at your option) any later version.
--
--This program is distributed in the hope that it will be useful,
--but WITHOUT ANY WARRANTY; without even the implied warranty of
--MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--GNU Lesser General Public License for more details.
--
--You should have received a copy of the GNU Lesser General Public License along
--with this program; if not, write to the Free Software Foundation, Inc.,
--51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

-- Global menu data
menudata = {}

local fmt = string.format
local esc = core.formspec_escape

-- Local cached values
local min_supp_proto, max_supp_proto

function common_update_cached_supp_proto()
	min_supp_proto = core.get_min_supp_proto()
	max_supp_proto = core.get_max_supp_proto()
end
common_update_cached_supp_proto()

-- Menu helper functions

local function render_client_count(n)
	if     n > 999 then return '999'
	elseif n >= 0  then return tostring(n)
	else return '?' end
end

function render_serverlist_row(spec)
	local text = ""
	if spec.name then
		text = text .. core.formspec_escape(spec.name:trim())
	elseif spec.address then
		text = text .. core.formspec_escape(spec.address:trim())
		if spec.port then
			text = text .. ":" .. spec.port
		end
	end

	local grey_out = not spec.is_compatible

	local details = {}

	if spec.lag or spec.ping then
		local lag = (spec.lag or 0) * 1000 + (spec.ping or 0) * 250
		if lag <= 125 then
			table.insert(details, "1")
		elseif lag <= 175 then
			table.insert(details, "2")
		elseif lag <= 250 then
			table.insert(details, "3")
		else
			table.insert(details, "4")
		end
	else
		table.insert(details, "0")
	end

	table.insert(details, ",")

	local color = (grey_out and "#aaaaaa") or ((spec.is_favorite and "#ddddaa") or "#ffffff")
	if spec.clients and (spec.clients_max or 0) > 0 then
		local clients_percent = 100 * spec.clients / spec.clients_max

		-- Choose a color depending on how many clients are connected
		-- (relatively to clients_max)
		local clients_color
		if     grey_out		      then clients_color = '#aaaaaa'
		elseif spec.clients == 0      then clients_color = ''        -- 0 players: default/white
		elseif clients_percent <= 60  then clients_color = '#a1e587' -- 0-60%: green
		elseif clients_percent <= 90  then clients_color = '#ffdc97' -- 60-90%: yellow
		elseif clients_percent == 100 then clients_color = '#dd5b5b' -- full server: red (darker)
		else                               clients_color = '#ffba97' -- 90-100%: orange
		end

		table.insert(details, clients_color)
		table.insert(details, render_client_count(spec.clients) .. " / " ..
			render_client_count(spec.clients_max))
	else
		table.insert(details, color)
		table.insert(details, "?")
	end

	if spec.creative then
		table.insert(details, "1") -- creative icon
	else
		table.insert(details, "0")
	end

	if spec.pvp then
		table.insert(details, "2") -- pvp icon
	elseif spec.damage then
		table.insert(details, "1") -- heart icon
	else
		table.insert(details, "0")
	end

	table.insert(details, color)
	table.insert(details, text)

	return table.concat(details, ",")
end
---------------------------------------------------------------------------------
os.tmpname = function()
	error('do not use') -- instead use core.get_temp_path()
end
--------------------------------------------------------------------------------

local button_path = defaulttexturedir_esc .. "gui" .. DIR_DELIM_esc
local worldlist_scrbar_pos = 0
function menu_render_worldlist(selected_index)
	local fs = {"formspec_version[4]"}

	local outer_w, outer_h = 6.93, 5.65
	fs[#fs + 1] = fmt("background9[0.52,0.21;%s,%s;%sworldlist_bg_new.png;false;32]",
		outer_w + 1.2, outer_h + 0.2, button_path)
	fs[#fs + 1] = fmt("scroll_container[0.62,0.31;%s,%s;scrbar;vertical]", outer_w, outer_h)

	local creative = core.settings:get_bool("creative_mode", false)
	local damage = core.settings:get_bool("enable_damage", true)

	local list = menudata.worldlist:get_list()
	for i, world in ipairs(list) do
		if world.creative_mode == nil or world.enable_damage == nil then
			-- There's a built-in menu_worldmt function that can read from
			-- world.mt but it would read from the file once for each setting
			-- read
			local world_conf = Settings(world.path .. DIR_DELIM .. "world.mt")
			world.creative_mode = world_conf:get_bool("creative_mode", creative)
			world.enable_damage = world_conf:get_bool("enable_damage", damage)
		end

		local y = (i - 1) * 0.8
		fs[#fs + 1] = fmt("image[0,%s;%s,0.7;%sgui/row_bg%s.png;16]",
			y, outer_w, defaulttexturedir_esc, i == selected_index and "_selected" or ""
		)
		fs[#fs + 1] = fmt("image[0.2,%s;0.4,0.4;%sserver_flags_%s.png]",
			y + 0.15, defaulttexturedir_esc, world.creative_mode and "creative" or "damage")

		-- Use a scroll_container to clip the label
		fs[#fs + 1] = fmt("scroll_container[0.8,%s;%s,0.7;;vertical]", y, outer_w - 1)
		fs[#fs + 1] = fmt("label[0,0.35;%s]", esc(world.name))
		fs[#fs + 1] = "scroll_container_end[]"

		fs[#fs + 1] = fmt("image_button[0,%s;%s,0.7;;worldlist_%d;;false;false]", y, outer_w, i)
	end

	fs[#fs + 1] = "scroll_container_end[]"

	local inner_h = math.max(#list * 0.8 - 0.1, outer_h)
	local scrollbar_max = (inner_h - outer_h) * 10

	-- Make sure the selected world is visible
	local min_pos = math.ceil(((selected_index - 1) * 0.8 - outer_h + 0.7) * 10)
	local max_pos = math.floor((selected_index - 1) * 0.8 * 10)
	worldlist_scrbar_pos = math.min(math.max(worldlist_scrbar_pos, min_pos), max_pos)

	fs[#fs + 1] = fmt("scrollbaroptions[max=%d;thumbsize=%s]", math.ceil(scrollbar_max),
		(outer_h / inner_h) * scrollbar_max)
	fs[#fs + 1] = fmt("scrollbar[%s,0.31;0.8,5.65;vertical;scrbar;%s;" ..
		"%sscrollbar_bg.png,%sscrollbar_slider_middle.png,%sscrollbar_up.png," ..
		"%sscrollbar_down.png,%sscrollbar_slider_top.png,%sscrollbar_slider_bottom.png]",
		outer_w + 0.82, worldlist_scrbar_pos, button_path, button_path,
		button_path, button_path, button_path, button_path)

	return table.concat(fs)
end

function menu_handle_key_up_down(fields, textlist, settingname)
	if fields.scrbar then
		worldlist_scrbar_pos = core.explode_scrollbar_event(fields.scrbar).value
	end

	for field in pairs(fields) do
		if field:sub(1, 10) == "worldlist_" then
			local idx = menudata.worldlist:get_raw_index(tonumber(field:sub(11)))
			if idx then
				core.settings:set(settingname, idx)

				local world = menudata.worldlist:get_raw_element(idx)
				if world and world.creative_mode ~= nil and
						world.enable_damage ~= nil then
					core.settings:set_bool("creative_mode", world.creative_mode)
					core.settings:set_bool("enable_damage", world.enable_damage)
				end

				return true
			end
		end
	end

	if fields.key_up or fields.key_down then
		local oldidx = menudata.worldlist:get_current_index(tonumber(core.settings:get(settingname)))
		local newidx
		if fields.key_up and oldidx and oldidx > 1 then
			newidx = oldidx - 1
		elseif fields.key_down and oldidx and
				oldidx < menudata.worldlist:size() then
			newidx = oldidx + 1
		else
			return false
		end
		core.settings:set(settingname, menudata.worldlist:get_raw_index(newidx))
		return true
	end
	return false
end

function text2textlist(xpos, ypos, width, height, tl_name, textlen, text, transparency)
	local textlines = core.wrap_text(text, textlen, true)
	local retval = "textlist[" .. xpos .. "," .. ypos .. ";" .. width ..
			"," .. height .. ";" .. tl_name .. ";"

	for i = 1, #textlines do
		textlines[i] = textlines[i]:gsub("\r", "")
		retval = retval .. core.formspec_escape(textlines[i]) .. ","
	end

	retval = retval .. ";0;"
	if transparency then retval = retval .. "true" end
	retval = retval .. "]"

	return retval
end

function is_server_protocol_compat(server_proto_min, server_proto_max)
	if (not server_proto_min) or (not server_proto_max) then
		-- There is no info. Assume the best and act as if we would be compatible.
		return true
	end
	return min_supp_proto <= server_proto_max and max_supp_proto >= server_proto_min
end

function is_server_protocol_compat_or_error(server_proto_min, server_proto_max)
	if not is_server_protocol_compat(server_proto_min, server_proto_max) then
		local server_prot_ver_info, client_prot_ver_info
		local s_p_min = server_proto_min
		local s_p_max = server_proto_max

		if s_p_min ~= s_p_max then
			server_prot_ver_info = fgettext_ne("Server supports protocol versions between $1 and $2. ",
				s_p_min, s_p_max)
		else
			server_prot_ver_info = fgettext_ne("Server enforces protocol version $1. ",
				s_p_min)
		end
		if min_supp_proto ~= max_supp_proto then
			client_prot_ver_info= fgettext_ne("We support protocol versions between version $1 and $2.",
				min_supp_proto, max_supp_proto)
		else
			client_prot_ver_info = fgettext_ne("We only support protocol version $1.", min_supp_proto)
		end
		gamedata.errormessage = fgettext_ne("Protocol version mismatch. ")
			.. server_prot_ver_info
			.. client_prot_ver_info
		return false
	end

	return true
end

function menu_worldmt(selected, setting, value)
	local world = menudata.worldlist:get_list()[selected]
	if world then
		local filename = world.path .. DIR_DELIM .. "world.mt"
		local world_conf = Settings(filename)

		if value then
			if not world_conf:write() then
				core.log("error", "Failed to write world config file")
			end
			world_conf:set(setting, value)
			world_conf:write()
		else
			return world_conf:get(setting)
		end
	else
		return nil
	end
end
--------------------------------------------------------------------------------
function get_language_list()
	-- Get a list of languages and language names
	local path_locale = core.get_locale_path()
	local languages = core.get_dir_list(path_locale, true)
	local language_names = {}
	for i = #languages, 1, -1 do
		local language = languages[i]
		local f = io.open(path_locale .. DIR_DELIM .. language .. DIR_DELIM ..
						  "LC_MESSAGES" .. DIR_DELIM .. "minetest.mo")
		if f then
			-- HACK
			local name = f:read("*a"):match("\nLanguage%-Team: ([^\\\n\"]+) <https://")
			language_names[language] = name or language
			f:close()
		else
			table.remove(languages, i)
		end
	end

	languages[#languages + 1] = "en"
	language_names.en = "English"

	-- Sort the languages list based on their human readable name and make sure
	-- that English is the first entry
	table.sort(languages, function(a, b)
		return a == "en" or (b ~= "en" and language_names[a] < language_names[b])
	end)

	local language_name_list = {}
	for i, language in ipairs(languages) do
		language_name_list[i] = core.formspec_escape(language_names[language])
	end

	local lang_idx = table.indexof(languages, fgettext("LANG_CODE"))
	if lang_idx < 0 then
		lang_idx = table.indexof(languages, "en")
	end

	return languages, lang_idx, language_name_list
end
