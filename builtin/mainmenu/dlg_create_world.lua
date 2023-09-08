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

-- cf. tab_local, the gamebar already provides game selection so we hide the list from here
local hide_gamelist = PLATFORM ~= "Android"

local dropdown_open = false

local function table_to_flags(ftable)
	-- Convert e.g. { jungles = true, caves = false } to "jungles,nocaves"
	local str = {}
	for flag, is_set in pairs(ftable) do
		str[#str + 1] = is_set and flag or ("no" .. flag)
	end
	return table.concat(str, ",")
end

-- Same as check_flag but returns a string

local cb_caverns = { "caverns", fgettext("Caverns"),
	fgettext("Very large caverns deep in the underground") }

local flag_checkboxes = {
	v5 = {
		cb_caverns,
	},
	v7 = {
		cb_caverns,
		{ "ridges", fgettext("Rivers"), fgettext("Sea level rivers") },
		{ "mountains", fgettext("Mountains") },
		{ "floatlands", fgettext("Floatlands (experimental)"),
		fgettext("Floating landmasses in the sky") },
	},
	carpathian = {
		cb_caverns,
		{ "rivers", fgettext("Rivers"), fgettext("Sea level rivers") },
	},
	valleys = {
		{ "altitude_chill", fgettext("Altitude chill"),
		fgettext("Reduces heat with altitude") },
		{ "altitude_dry", fgettext("Altitude dry"),
		fgettext("Reduces humidity with altitude") },
		{ "humid_rivers", fgettext("Humid rivers"),
		fgettext("Increases humidity around rivers") },
		{ "vary_river_depth", fgettext("Vary river depth"),
		fgettext("Low humidity and high heat causes shallow or dry rivers") },
	},
	flat = {
		cb_caverns,
		{ "hills", fgettext("Hills") },
		{ "lakes", fgettext("Lakes") },
	},
	fractal = {
		{ "terrain", fgettext("Additional terrain"),
		fgettext("Generate non-fractal terrain: Oceans and underground") },
	},
	v6 = {
		{ "trees", fgettext("Trees and jungle grass") },
		{ "flat", fgettext("Flat terrain") },
		{ "mudflow", fgettext("Mud flow"), fgettext("Terrain surface erosion") },
		-- Biome settings are in mgv6_biomes below
	},
}

local mgv6_biomes = {
	{
		fgettext("Temperate, Desert, Jungle, Tundra, Taiga"),
		{jungles = true, snowbiomes = true}
	},
	{
		fgettext("Temperate, Desert, Jungle"),
		{jungles = true, snowbiomes = false}
	},
	{
		fgettext("Temperate, Desert"),
		{jungles = false, snowbiomes = false}
	},
}

local function create_world_formspec(dialogdata)

	-- Error out when no games found
	if #pkgmgr.games == 0 then
		return "size[12.25,3]" ..
			"bgcolor[#0000]" ..
			"background9[0,0;0,0;" .. defaulttexturedir_esc .. "bg_common.png;true;40]" ..
			"box[0,0;12,2;#ff8800]" ..
			"textarea[0.3,0;11.7,2;;;"..
			fgettext("You have no games installed.") .. "\n" ..
			fgettext("Download one from minetest.net") .. "]" ..
			btn_style("world_create_cancel") ..
			"button[4.75,2.5;3,0.5;world_create_cancel;" .. fgettext("Cancel") .. "]"
	end

	local current_mg = dialogdata.mg
	local mapgens = core.get_mapgen_names()
	mapgens[#mapgens + 1] = "superflat"

	local gameid = core.settings:get("menu_last_game")

	local flags = dialogdata.flags

	local game, gameidx = pkgmgr.find_by_gameid(gameid)
	if game == nil and hide_gamelist then
		-- should never happen but just pick the first game
		game = pkgmgr.get_game(1)
		gameidx = 1
		core.settings:set("menu_last_game", game.id)
	elseif game == nil then
		gameidx = 0
	end

	local disallowed_mapgen_settings = {}
	if game ~= nil then
		local gameconfig = Settings(game.path.."/game.conf")

		local allowed_mapgens = (gameconfig:get("allowed_mapgens") or ""):split()
		for key, value in ipairs(allowed_mapgens) do
			value = value:trim()
			allowed_mapgens[key] = value
			if value == "flat" then
				allowed_mapgens[#allowed_mapgens + 1] = "superflat"
			end
		end

		local disallowed_mapgens = (gameconfig:get("disallowed_mapgens") or ""):split()
		for key, value in ipairs(disallowed_mapgens) do
			value = value:trim()
			disallowed_mapgens[key] = value
			if value == "flat" then
				disallowed_mapgens[#disallowed_mapgens + 1] = "superflat"
			end
		end

		if #allowed_mapgens > 0 then
			for i = #mapgens, 1, -1 do
				if table.indexof(allowed_mapgens, mapgens[i]) == -1 then
					table.remove(mapgens, i)
				end
			end
		end

		if #disallowed_mapgens > 0 then
			for i = #mapgens, 1, -1 do
				if table.indexof(disallowed_mapgens, mapgens[i]) > 0 then
					table.remove(mapgens, i)
				end
			end
		end

		local ds = (gameconfig:get("disallowed_mapgen_settings") or ""):split()
		for _, value in pairs(ds) do
			disallowed_mapgen_settings[value:trim()] = true
		end
	end

	local first_mg, selindex
	local mapgen_names = {}
	dialogdata.mapgens = mapgens
	for i, v in ipairs(mapgens) do
		if not first_mg then
			first_mg = v
		end
		if current_mg == v then
			selindex = i
		end

		if v == "v7p" then
			mapgen_names[i] = "Default"
		else
			mapgen_names[i] = v:sub(1, 1):upper() .. v:sub(2)
		end
	end
	if not selindex then
		selindex = 1
		current_mg = first_mg
	end

	-- The logic of the flag element IDs is as follows:
	-- "flag_main_foo-bar-baz" controls dialogdata.flags["main"]["foo_bar_baz"]
	-- see the buttonhandler for the implementation of this

	local mg_main_flags = function(mapgen, y)
		if mapgen == "singlenode" or mapgen == "superflat" then
			return "", y
		end
		if disallowed_mapgen_settings["mg_flags"] then
			return "", y
		end

		local form = checkbox(0, y, "flag_mg_caves", fgettext("Caves"), flags.main.caves)
		y = y + 0.575

		form = form .. checkbox(0, y, "flag_mg_dungeons", fgettext("Dungeons"), flags.main.dungeons)
		y = y + 0.575

		local d_name = fgettext("Decorations")
		local d_tt
		if mapgen == "v6" then
			d_tt = fgettext("Structures appearing on the terrain (no effect on trees and jungle grass created by v6)")
		else
			d_tt = fgettext("Structures appearing on the terrain, typically trees and plants")
		end
		form = form .. checkbox(0, y, "flag_mg_decorations", d_name, flags.main.decorations) ..
			"tooltip[flag_mg_decorations;" ..
			d_tt ..
			"]"
		y = y + 0.575

		form = form .. "tooltip[flag_main_caves;" ..
		fgettext("Network of tunnels and caves")
		.. "]"
		return form, y
	end

	local mg_specific_flags = function(mapgen, y)
		if not flag_checkboxes[mapgen] then
			return "", y
		end
		if disallowed_mapgen_settings["mg"..mapgen.."_spflags"] then
			return "", y
		end
		local form = ""
		for _, tab in pairs(flag_checkboxes[mapgen]) do
			local id = "flag_"..mapgen.."_"..tab[1]:gsub("_", "-")
			form = form .. checkbox(0, y, id, tab[2], flags[mapgen][tab[1]])

			if tab[3] then
				form = form .. "tooltip["..id..";"..tab[3].."]"
			end
			y = y + 0.575
		end

		if mapgen ~= "v6" then
			-- No special treatment
			return form, y
		end
		-- Special treatment for v6 (add biome widgets)

		-- Biome type (jungles, snowbiomes)
		local biometype
		if flags.v6.snowbiomes == true then
			biometype = 1
		elseif flags.v6.jungles == true  then
			biometype = 2
		else
			biometype = 3
		end
		y = y + 0.345

		form = form .. "label[0,"..(y+0.11)..";" .. fgettext("Biomes") .. ":]"
		y = y + 0.69

		form = form .. "dropdown[0,"..y..";6.3;mgv6_biomes;"
		for b=1, #mgv6_biomes do
			form = form .. mgv6_biomes[b][1]
			if b < #mgv6_biomes then
				form = form .. ","
			end
		end
		form = form .. ";" .. biometype.. "]"

		-- biomeblend
		y = y + 0.6325
		form = form .. checkbox(0, y, "flag_mgv6_biomeblend",
			fgettext("Biome blending"), flags.v6.biomeblend) ..
			"tooltip[flag_v6_biomeblend;" ..
			fgettext("Smooth transition between biomes") .. "]"

		return form, y
	end

	local y_start = 0.575
	local y = y_start
	local str_flags, str_spflags
	local label_flags, label_spflags = "", ""
	y = y + 0.5
	str_flags, y = mg_main_flags(current_mg, y)
	if str_flags ~= "" then
		label_flags = "label[0,"..y_start..";" .. fgettext("Mapgen flags") .. ":]"
		y_start = y + 0.5
	end
	y = y_start + 0.5
	str_spflags = mg_specific_flags(current_mg, y)
	if str_spflags ~= "" then
		label_spflags = "label[0,"..y_start..";" .. fgettext("Mapgen-specific flags") .. ":]"
	end

	-- Warning if only devtest is installed
	local devtest_only = ""
	local gamelist_height = 2.97
	if #pkgmgr.games == 1 and pkgmgr.games[1].id == "devtest" then
		devtest_only = "box[0,0;7.25,1.6;#ff8800]" ..
				"textarea[0,0;7.25,1.6;;;"..
				fgettext("Warning: The Development Test is meant for developers.") .. "\n" ..
				fgettext("Download a game, such as Minetest Game, from minetest.net") .. "]"
		gamelist_height = 1.12
	end

	local _gameidx = gameidx
	if _gameidx >= pkgmgr.default_game_idx then
		_gameidx = _gameidx - 1
	end

	local retval =
		"size[12.25,7]" ..
		"bgcolor[#0000]" ..
		"background9[0,0;0,0;" .. defaulttexturedir_esc .. "bg_common.png;true;40]" ..

		-- Left side
		"real_coordinates[true]" ..
		"formspec_version[3]" ..
		"image[0.37,0.6;7.28,0.8;" .. defaulttexturedir_esc .. "field_bg.png;32]" ..
		"style[te_world_name;border=false;bgcolor=transparent]" ..
		"field[0.42,0.6;7.18,0.8;te_world_name;" ..
		fgettext("World name") ..
		":;" .. core.formspec_escape(worldname) .. "]" ..
		"set_focus[te_world_name;false]" ..

		"image[0.37,1.9;7.28,0.8;" .. defaulttexturedir_esc .. "field_bg.png;32]" ..
		"style[te_seed;border=false;bgcolor=transparent]" ..
		"field[0.42,1.9;7.18,0.8;te_seed;" ..
		fgettext("Seed") ..
		":;".. core.formspec_escape(dialogdata.seed) .. "]"

	if not hide_gamelist or devtest_only ~= "" then
		retval = retval ..
			"label[0.43,4.3;" .. fgettext("Game") .. ":]" ..
			"background9[0.37,4.5;7.28," .. gamelist_height .. ";" .. defaulttexturedir_esc .. "worldlist_bg.png;false;40]" ..
			"textlist[0.47,4.6;7.08,".. gamelist_height - 0.2 .. ";games;" ..
				pkgmgr.gamelist() .. ";" .. _gameidx .. ";true]" ..
			"container[0.37,5.8]" ..
			devtest_only ..
			"container_end[]"
	end

	retval = retval ..
		"container_end[]" ..

		-- Right side
		"container[8.25,0]" ..
		label_flags .. str_flags ..
		label_spflags .. str_spflags ..
		"container_end[]" ..
		"real_coordinates[false]" ..

		-- Menu buttons
		btn_style("world_create_confirm", "green") ..
		"button[3.25,6.5;3,0.5;world_create_confirm;" .. fgettext("Create") .. "]" ..
		btn_style("world_create_cancel") ..
		"button[6.25,6.5;3,0.5;world_create_cancel;" .. fgettext("Cancel") .. "]" ..

		-- Mapgen (must be last)
		"real_coordinates[true]" ..
		"label[0.43,3;" .. fgettext("Mapgen") .. ":]"..
		get_dropdown(0.37, 3.2, 7.28, "change_mapgen", mapgen_names, selindex, dropdown_open)

	return retval

end

local function create_world_buttonhandler(this, fields)

	if fields["world_create_confirm"] or
		fields["key_enter"] then

		local worldname = fields["te_world_name"]
		local game, gameindex
		if hide_gamelist then
			game, gameindex = pkgmgr.find_by_gameid(core.settings:get("menu_last_game"))
		else
			gameindex = core.get_textlist_index("games")
			game = pkgmgr.get_game(gameindex)
		end

		local message
		if game == nil then
			message = fgettext("No game selected")
		end

		if message == nil then
			if gameindex >= pkgmgr.default_game_idx then
				gameindex = gameindex + 1
			end

			if not pkgmgr.games[gameindex] then
				gamedata.errormessage = fgettext("No game selected")
				this:delete()
				return true
			end

			-- For unnamed worlds use the generated name 'World <number>',
			-- where the number increments: it is set to 1 larger than the largest
			-- generated name number found.
			if worldname == "" then
				local worldnum_max = 0
				for _, world in ipairs(menudata.worldlist:get_list()) do
					if world.name:match("^World %d+$") then
						local worldnum = tonumber(world.name:sub(6))
						worldnum_max = math.max(worldnum_max, worldnum)
					end
				end
				worldname = "World " .. worldnum_max + 1
			end

			if menudata.worldlist:uid_exists_raw(worldname) then
				message = fgettext("A world named \"$1\" already exists", worldname)
			end
		end

		if message == nil then
			this.data.seed = fields["te_seed"]
			this.data.mg = fields["dd_mapgen"]

			-- actual names as used by engine
			local settings = {
				fixed_map_seed = this.data.seed,
				mg_name = this.data.mg,
				mg_flags = table_to_flags(this.data.flags.main),
				mgv5_spflags = table_to_flags(this.data.flags.v5),
				mgv6_spflags = table_to_flags(this.data.flags.v6),
				mgv7_spflags = table_to_flags(this.data.flags.v7),
				mgfractal_spflags = table_to_flags(this.data.flags.fractal),
				mgcarpathian_spflags = table_to_flags(this.data.flags.carpathian),
				mgvalleys_spflags = table_to_flags(this.data.flags.valleys),
				mgflat_spflags = table_to_flags(this.data.flags.flat),
			}
			message = core.create_world(worldname, gameindex, settings)
		end

		if message == nil then
			core.settings:set("menu_last_game", game.id)
			if this.data.update_worldlist_filter then
				menudata.worldlist:set_filtercriteria(game.id)
			end
			menudata.worldlist:refresh()
			core.settings:set("mainmenu_last_selected_world",
					menudata.worldlist:raw_index_by_uid(worldname))
		end

		gamedata.errormessage = message
		this:delete()
		return true
	end

	this.data.worldname = fields["te_world_name"]
	this.data.seed = fields["te_seed"]

	if fields["games"] then
		local gameindex = core.get_textlist_index("games")
		if gameindex >= pkgmgr.default_game_idx then
			gameindex = gameindex + 1
		end
		local game = pkgmgr.games[gameindex]
		if not game then return end
		core.settings:set("menu_last_game", game.id)
		return true
	end

	for k in pairs(fields) do
		local split = string.split(k, "_", nil, 3)
		if split and split[1] == "flag" then
			-- We replaced the underscore of flag names with a dash.
			local flag = string.gsub(split[3], "-", "_")
			local ftable = this.data.flags[split[2]]
			assert(ftable)
			ftable[flag] = v == "true"
			return true
		end
	end

	if fields["world_create_cancel"] then
		this:delete()
		return true
	end

	if fields.change_mapgen then
		dropdown_open = true
		return true
	end

	for field in pairs(fields) do
		if field:sub(1, 9) == "dropdown_" and this.data.mapgens then
			dropdown_open = false
			local new_mapgen = this.data.mapgens[tonumber(field:sub(10))]
			if new_mapgen then
				core.settings:set("mg_name", new_mapgen)
			end
			return true
		end
	end

	if fields["mgv6_biomes"] then
		local entry = core.formspec_escape(fields["mgv6_biomes"])
		for b=1, #mgv6_biomes do
			if entry == mgv6_biomes[b][1] then
				local ftable = this.data.flags.v6
				ftable.jungles = mgv6_biomes[b][2].jungles
				ftable.snowbiomes = mgv6_biomes[b][2].snowbiomes
				return true
			end
		end
	end

	return false
end


function create_create_world_dlg(update_worldlistfilter)
	local retval = dialog_create("sp_create_world",
					create_world_formspec,
					create_world_buttonhandler,
					nil)
	retval.data = {
		update_worldlist_filter = update_worldlistfilter,
		worldname = "",
		-- settings the world is created with:
		seed = core.settings:get("fixed_map_seed") or "",
		mg = core.settings:get("mg_name"),
		flags = {
			main = core.settings:get_flags("mg_flags"),
			v5 = core.settings:get_flags("mgv5_spflags"),
			v6 = core.settings:get_flags("mgv6_spflags"),
			v7 = core.settings:get_flags("mgv7_spflags"),
			fractal = core.settings:get_flags("mgfractal_spflags"),
			carpathian = core.settings:get_flags("mgcarpathian_spflags"),
			valleys = core.settings:get_flags("mgvalleys_spflags"),
			flat = core.settings:get_flags("mgflat_spflags"),
		}
	}

	return retval
end
