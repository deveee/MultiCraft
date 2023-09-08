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

local password_tmp = ""

local esc = core.formspec_escape
local lower = utf8.lower
local small_screen = (PLATFORM == "Android" or PLATFORM == "iOS") and not core.settings:get_bool("device_is_tablet")

local function get_sorted_servers()
	local servers = {
		fav = {},
		public = {},
		incompatible = {}
	}

	local favs = serverlistmgr.get_favorites()
	local taken_favs = {}
	local result = menudata.search_result or serverlistmgr.servers
	for _, server in ipairs(result) do
		server.is_favorite = false
		for index, fav in ipairs(favs) do
			if server.address == fav.address and server.port == fav.port then
				taken_favs[index] = true
				server.is_favorite = true
				break
			end
		end
		server.is_compatible = is_server_protocol_compat(server.proto_min, server.proto_max)
		if server.is_favorite then
			table.insert(servers.fav, server)
		elseif server.is_compatible then
			table.insert(servers.public, server)
		else
			table.insert(servers.incompatible, server)
		end
	end

	if not menudata.search_result then
		for index, fav in ipairs(favs) do
			if not taken_favs[index] then
				table.insert(servers.fav, fav)
			end
		end
	end

	return servers
end

local function get_formspec(tabview, name, tabdata)
	-- Update the cached supported proto info,
	-- it may have changed after a change by the settings menu.
	common_update_cached_supp_proto()

	if not tabdata.search_for then
		tabdata.search_for = ""
	end

	local address = core.settings:get("address")
	local port = tonumber(core.settings:get("remote_port"))
	if port and port ~= 30000 then
		address = address .. ":" .. port
	end

	local retval =
		-- Search
		"formspec_version[3]" ..
		"image[-0.11,4.93;8.02,0.81;" .. defaulttexturedir_esc .. "field_bg.png;32]" ..
		"style[Dte_search;border=false;bgcolor=transparent]" ..
		"field[0.25,5.25;5.9,0.83;Dte_search;;" .. esc(tabdata.search_for) .. "]" ..
		"image_button[5.62,4.93;0.83,0.83;" .. defaulttexturedir_esc ..
			"clear.png;btn_mp_clear;;true;false]" ..
		btn_style("btn_mp_refresh") ..
		"image_button[6.37,4.93;0.83,0.83;" .. defaulttexturedir_esc ..
			"refresh.png;btn_mp_refresh;;true;false]" ..

		-- Address / Port
		"image[7.1,0.09;6,0.8;" .. defaulttexturedir_esc .. "field_bg.png;32]" ..
		"style[te_address;border=false;bgcolor=transparent]" ..
		"field[7.45,0.55;4.9,0.5;te_address;" .. fgettext("Address / Port") .. ":;" ..
			esc(address) .. "]" ..

		-- Name
		"image[7.1,1.25;2.95,0.8;" .. defaulttexturedir_esc .. "field_bg.png;32]" ..
		"style[te_name;border=false;bgcolor=transparent]" ..
		"field[7.45,1.7;2.45,0.5;te_name;" .. fgettext("Name") .. ":;" ..
			esc(core.settings:get("name")) .. "]" ..

		-- Password
		"image[9.55,1.25;2.95,0.8;" .. defaulttexturedir_esc .. "field_bg.png;32]" ..
		"style[te_pwd;border=false;bgcolor=transparent]" ..
		"pwdfield[9.9,1.7;2.45,0.5;te_pwd;" .. fgettext("Password") .. ":;" ..
			esc(password_tmp) .. "]" ..

		-- Description Background
		"background9[7.2,2.2;4.8,2.65;" .. defaulttexturedir_esc .. "desc_bg.png;false;32]" ..

		-- Connect
		btn_style("btn_mp_connect") ..
		"style[btn_mp_connect;font_size=*" .. (small_screen and 1.5 or 2) .. "]" ..
		"image_button[8.8,4.88;3.3,0.9;;btn_mp_connect;" ..
			("\n"):rep(3) .. " " .. fgettext("Play") .. ("\n"):rep(3) .. ";true;false]" .. -- Connect
		"image[9,5;0.6,0.6;" .. defaulttexturedir_esc .. "btn_play_icon.png]" ..
		"tooltip[btn_mp_connect;" .. fgettext("Connect") .. "]"

	if tabdata.selected then
		if gamedata.fav then
			retval = retval ..
				btn_style("btn_delete_favorite", "red") ..
				"image_button[7.1,4.93;0.83,0.83;" .. defaulttexturedir_esc ..
					"trash.png;btn_delete_favorite;;true;false;" .. defaulttexturedir_esc .. "trash_pressed.png]" ..
					"tooltip[btn_delete_favorite;" .. fgettext("Del. Favorite") .. "]"
		end
		if gamedata.serverdescription then
			retval = retval ..
				scrollbar_style("textarea", true) ..
				"textarea[7.5,2.2;4.8,3;;" .. esc((gamedata.serverdescription or ""), true) .. ";]"
		end
	end

	retval = retval .. "container_end[]"

	-- Table
	retval = retval .. "tablecolumns[" ..
		"image,tooltip=" .. fgettext("Ping") .. "," ..
		"0=" .. core.formspec_escape(defaulttexturedir .. "blank.png") .. "," ..
		"1=" .. core.formspec_escape(defaulttexturedir .. "server_ping_4.png") .. "," ..
		"2=" .. core.formspec_escape(defaulttexturedir .. "server_ping_3.png") .. "," ..
		"3=" .. core.formspec_escape(defaulttexturedir .. "server_ping_2.png") .. "," ..
		"4=" .. core.formspec_escape(defaulttexturedir .. "server_ping_1.png") .. "," ..
		"5=" .. core.formspec_escape(defaulttexturedir .. "server_favorite.png") .. "," ..
		"6=" .. core.formspec_escape(defaulttexturedir .. "server_public.png") .. "," ..
		"7=" .. core.formspec_escape(defaulttexturedir .. "server_incompatible.png") .. ";" ..
		"color,span=1;" ..
		"text,align=inline;"..
		"color,span=1;" ..
		"text,align=inline,width=4.25;" ..
		"image,tooltip=" .. fgettext("Creative mode") .. "," ..
		"0=" .. core.formspec_escape(defaulttexturedir .. "blank.png") .. "," ..
		"1=" .. core.formspec_escape(defaulttexturedir .. "server_flags_creative.png") .. "," ..
		"align=inline,padding=0.25,width=1.5;" ..
		--~ PvP = Player versus Player
		"image,tooltip=" .. fgettext("Damage / PvP") .. "," ..
		"0=" .. core.formspec_escape(defaulttexturedir .. "blank.png") .. "," ..
		"1=" .. core.formspec_escape(defaulttexturedir .. "server_flags_damage.png") .. "," ..
		"2=" .. core.formspec_escape(defaulttexturedir .. "server_flags_pvp.png") .. "," ..
		"align=inline,padding=0.25,width=1.5;" ..
		"color,align=inline,span=1;" ..
		"text,align=inline,padding=1]" ..
		"table[0.25,1;9.25,5.75;servers;"

	local servers = get_sorted_servers()

	local dividers = {
		fav = "5,#ffff00," .. fgettext("Favorites") .. ",,,0,0,,",
		public = "6,#4bdd42," .. fgettext("Public Servers") .. ",,,0,0,,",
		incompatible = "7,"..mt_color_grey.."," .. fgettext("Incompatible Servers") .. ",,,0,0,,"
	}
	local order = {"fav", "public", "incompatible"}

	tabdata.lookup = {} -- maps row number to server
	local rows = {}
	for _, section in ipairs(order) do
		local section_servers = servers[section]
		if next(section_servers) ~= nil then
			rows[#rows + 1] = dividers[section]
			for _, server in ipairs(section_servers) do
				tabdata.lookup[#rows + 1] = server
				rows[#rows + 1] = render_serverlist_row(server)
			end
		end
	end

	retval = retval .. table.concat(rows, ",")

	if tabdata.selected then
		retval = retval .. ";" .. tabdata.selected .. "]"
	else
		retval = retval .. ";0]"
	end

	return retval, "size[15.5,7,false]real_coordinates[true]"
end

--------------------------------------------------------------------------------

local function search_server_list(input)
	menudata.search_result = nil
	if #serverlistmgr.servers < 2 then
		return
	end

	-- setup the keyword list
	local keywords = {}
	for word in input:gmatch("%S+") do
		word = word:gsub("(%W)", "%%%1")
		table.insert(keywords, word)
	end

	if #keywords == 0 then
		return
	end

	menudata.search_result = {}

	-- Search the serverlist
	local search_result = {}
	for i = 1, #serverlistmgr.servers do
		local server = serverlistmgr.servers[i]
		local found = 0
		for k = 1, #keywords do
			local keyword = keywords[k]
			if server.name then
				local sername = server.name:lower()
				local _, count = sername:gsub(keyword, keyword)
				found = found + count * 4
			end

			if server.description then
				local desc = server.description:lower()
				local _, count = desc:gsub(keyword, keyword)
				found = found + count * 2
			end
		end
		if found > 0 then
			local points = (#serverlistmgr.servers - i) / 5 + found
			server.points = points
			table.insert(search_result, server)
		end
	end

	if #search_result == 0 then
		return
	end

	table.sort(search_result, function(a, b)
		return a.points > b.points
	end)
	menudata.search_result = search_result
end

local function set_selected_server(tabdata, idx, server)
	-- reset selection
	if idx == nil or server == nil then
		tabdata.selected = nil

		core.settings:set("address", "")
		core.settings:set("remote_port", "30000")
		return
	end

	local address = server.address
	local port    = server.port
	gamedata.serverdescription = server.description

	gamedata.fav = false
	for _, fav in ipairs(serverlistmgr.get_favorites()) do
		if address == fav.address and port == fav.port then
			gamedata.fav = true
			break
		end
	end

	if address and port then
		core.settings:set("address", address)
		core.settings:set("remote_port", port)
	end
	tabdata.selected = idx
end

local function main_button_handler(tabview, fields, name, tabdata)
	if fields.te_name then
		gamedata.playername = fields.te_name
		core.settings:set("name", fields.te_name)
	end

	if fields.te_pwd then
		password_tmp = fields.te_pwd
	end

	if fields.servers then
		local event = core.explode_table_event(fields.servers)
		local server = tabdata.lookup[event.row]

		if server then
			if event.type == "DCL" then
				if not is_server_protocol_compat_or_error(
							server.proto_min, server.proto_max) then
					return true
				end

				gamedata.address    = server.address
				gamedata.port       = server.port
				gamedata.playername = fields.te_name
				gamedata.selected_world = 0

				if fields.te_pwd then
					gamedata.password = fields.te_pwd
				end

				gamedata.servername        = server.name
				gamedata.serverdescription = server.description

				if gamedata.address and gamedata.port then
					core.settings:set("address", gamedata.address)
					core.settings:set("remote_port", gamedata.port)
					core.start()
				end
				return true
			end
			if event.type == "CHG" then
				set_selected_server(tabdata, event.row, server)
				return true
			end
		end
	end

	if fields.btn_delete_favorite then
		local idx = core.get_table_index("servers")
		if not idx then return end
		local server = tabdata.lookup[idx]
		if not server then return end

		serverlistmgr.delete_favorite(server)
		-- the server at [idx+1] will be at idx once list is refreshed
		set_selected_server(tabdata, idx, tabdata.lookup[idx+1])
		return true
	end

	if fields.btn_mp_clear then
		tabdata.search_for = ""
		menudata.search_result = nil
		return true
	end

	if fields.btn_mp_refresh then
		serverlistmgr.sync()
		return true
	end

	if fields.Dte_search and not
			(fields.btn_mp_connect or fields.key_enter) then
		tabdata.search_for = fields.Dte_search
		search_server_list(fields.Dte_search or "")
		if menudata.search_result then
			-- first server in row 2 due to header
			set_selected_server(tabdata, 2, menudata.search_result[1])
		end

		return true
	end

	if fields.btn_mp_refresh then
		serverlistmgr.sync()
		return true
	end

	if (fields.btn_mp_connect or fields.key_enter)
			and fields.te_address ~= "" then
		gamedata.playername = fields.te_name
		gamedata.password   = fields.te_pwd
		gamedata.selected_world = 0

		-- Allow entering "address:port"
		local address, port = fields.te_address:match("^(.+):([0-9]+)$")
		gamedata.address    = address or fields.te_address
		gamedata.port       = tonumber(port) or 30000

		local idx = core.get_table_index("servers")
		local server = idx and tabdata.lookup[idx]

		set_selected_server(tabdata)

		if server and server.address == gamedata.address and
				server.port == gamedata.port then

			if not is_server_protocol_compat_or_error(
						server.proto_min, server.proto_max) then
				return true
			end

			serverlistmgr.add_favorite(server)

			gamedata.servername        = server.name
			gamedata.serverdescription = server.description
		else
			gamedata.servername        = ""
			gamedata.serverdescription = ""

			serverlistmgr.add_favorite({
				address = gamedata.address,
				port = gamedata.port,
			})
		end

		local auto_connect = false
		for _, server in pairs(serverlist) do
			if server.server_id == "multicraft" and server.address == gamedata.address then
				auto_connect = true
				break
			end
		end

		core.settings:set_bool("auto_connect", auto_connect)
		core.settings:set("connect_time", os.time())
		core.settings:set("maintab_LAST", "online")
		core.settings:set("address",     gamedata.address)
		core.settings:set("remote_port", gamedata.port)

		core.start()
		return true
	end

	return false
end

local function on_change(type, old_tab, new_tab)
	if type == "LEAVE" then return end
	serverlistmgr.sync()
end

return {
	name = "online",
	caption = fgettext("Join Game"),
	cbf_formspec = get_formspec,
	cbf_button_handler = main_button_handler,
	on_change = on_change
}
