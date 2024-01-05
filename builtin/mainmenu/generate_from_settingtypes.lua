local settings = ...

local concat = table.concat
local insert = table.insert
local sprintf = string.format
local rep = string.rep

local minetest_example_header = [[
#    This file contains a list of all available settings and their default value for multicraft.conf

#    By default, all the settings are commented and not functional.
#    Uncomment settings by removing the preceding #.

#    multicraft.conf is read by default from:
#    ../multicraft.conf
#    ../../multicraft.conf
#    Any other path can be chosen by passing the path as a parameter
#    to the program, eg. "multicraft.exe --config ../multicraft.conf.example".

#    Further documentation:
#    http://wiki.minetest.net/

]]

local group_format_template = [[
# %s = {
#    offset      = %s,
#    scale       = %s,
#    spread      = (%s, %s, %s),
#    seed        = %s,
#    octaves     = %s,
#    persistence = %s,
#    lacunarity  = %s,
#    flags       = %s
# }

]]

local function create_minetest_conf_example()
	local result = { minetest_example_header }
	for _, entry in ipairs(settings) do
		if entry.type == "category" then
			if entry.level == 0 then
				insert(result, "#\n# " .. entry.name .. "\n#\n\n")
			else
				insert(result, rep("#", entry.level))
				insert(result, "# " .. entry.name .. "\n\n")
			end
		else
			local group_format = false
			if entry.noise_params and entry.values then
				if entry.type == "noise_params_2d" or entry.type == "noise_params_3d" then
					group_format = true
				end
			end
			if entry.comment ~= "" then
				for _, comment_line in ipairs(entry.comment:split("\n", true)) do
					insert(result, "#    " .. comment_line .. "\n")
				end
			end
			insert(result, "#    type: " .. entry.type)
			if entry.min then
				insert(result, " min: " .. entry.min)
			end
			if entry.max then
				insert(result, " max: " .. entry.max)
			end
			if entry.values and entry.noise_params == nil then
				insert(result, " values: " .. concat(entry.values, ", "))
			end
			if entry.possible then
				insert(result, " possible values: " .. concat(entry.possible, ", "))
			end
			insert(result, "\n")
			if group_format == true then
				insert(result, sprintf(group_format_template, entry.name, entry.values[1],
						entry.values[2], entry.values[3], entry.values[4], entry.values[5],
						entry.values[6], entry.values[7], entry.values[8], entry.values[9],
						entry.values[10]))
			else
				local append
				if entry.default ~= "" then
					append = " " .. entry.default
				end
				insert(result, sprintf("# %s =%s\n\n", entry.name, append or ""))
			end
		end
	end
	return concat(result)
end

local translation_file_header = [[
// This file is automatically generated
// It conatins a bunch of fake gettext calls, to tell xgettext about the strings in config files
// To update it, refer to the bottom of builtin/mainmenu/dlg_settings_advanced.lua

fake_function() {]]

local function create_translation_file()
	local result = { translation_file_header }
	for _, entry in ipairs(settings) do
		if entry.type == "category" then
			insert(result, sprintf("\tgettext(%q);", entry.name))
		else
			if entry.readable_name then
				insert(result, sprintf("\tgettext(%q);", entry.readable_name))
			end
			if entry.comment ~= "" then
				local comment_escaped = entry.comment:gsub("\n", "\\n")
				comment_escaped = comment_escaped:gsub("\"", "\\\"")
				insert(result, "\tgettext(\"" .. comment_escaped .. "\");")
			end
		end
	end
	insert(result, "}\n")
	return concat(result, "\n")
end

local file = assert(io.open("multicraft.conf.example", "w"))
file:write(create_minetest_conf_example())
file:close()

file = assert(io.open("src/settings_translation_file.cpp", "w"))
-- If 'multicraft.conf.example' appears in the 'bin' folder, the line below may have to be
-- used instead. The file will also appear in the 'bin' folder.
--file = assert(io.open("settings_translation_file.cpp", "w"))
file:write(create_translation_file())
file:close()
