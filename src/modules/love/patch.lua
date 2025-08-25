R"luastring"--(
-- DO NOT REMOVE THE ABOVE LINE. It is used to load this file as a C++ string.
-- There is a matching delimiter at the bottom of the file.

-- This file provides helper functions for patching a string of text,
-- used when initializing mods

local love = require("love")

love.patch = {}

--- Iterator over all lines in str
--- @param str string
function love.patch.lines(str)
	if str:sub(-1) ~= "\n" then str = str .. "\n" end
	return str:gmatch("(.-)\n")
end

--- Applies patches to a string
--- @param str string The string to apply the patches to
--- @param ... table A sequence of patches to apply
--- @return string # The result of applying the patches
function love.patch.apply(str, ...)
	local patches = {...}
	local end_payloads = {}
	local res_lines = {}

	local function applyPatch(patch, result)
		local function applyPayload(payload)
			if type(payload) == "table" then
				for _, p in ipairs(payload) do applyPayload(p) end
			elseif type(payload) == "string" then
				table.insert(result, payload)
			end
		end
		local function applySource(source)
			if type(source) == "table" then
				for _, s in ipairs(source) do applySource(s) end
			elseif type(source) == "string" then
				applyPayload(love.filesystem.read(source))
			end
		end
		applyPayload(patch.payload)
		applySource(patch.source)
	end

	local function isMatch(line, patch, property)
		line = line:match("^%s*(.-)%s*$") -- trim whitespace
		if patch.pattern then
			return line:match(patch[property])
		end
		return line == patch[property]
	end

	-- Apply to the start of the file, save patches to the end of the file,
	-- discard any invalid patches
	for i, patch in ipairs(patches) do
		if type(patch) ~= "table" then
			table.remove(patches, i)
		else
			if patch.prepend then
				applyPatch(patch, res_lines)
			end
			if patch.append then
				applyPatch(patch, end_payloads)
			end
		end
	end

	-- Apply patches within the string
	for line in love.patch.lines(str) do
		local after_payloads = {}
		local overwrite = false
		for i, patch in ipairs(patches) do
			if isMatch(line, patch, "before") then
				applyPatch(patch, res_lines)
				overwrite = patch.overwrite
				patch.before = nil
			end
			if isMatch(line, patch, "after") then
				applyPatch(patch, after_payloads)
				overwrite = patch.overwrite
				patch.after = nil
			end
			if not patch.before and not patch.after then
				table.remove(patches, i)
			end
		end
		if not overwrite then
			table.insert(res_lines, line)
		end
		for _, payload in ipairs(after_payloads) do
			table.insert(res_lines, payload)
		end
	end

	-- Apply the end patches
	for _, payload in ipairs(end_payloads) do
		table.insert(res_lines, payload)
	end

	return table.concat(res_lines, "\n")
end

-- DO NOT REMOVE THE NEXT LINE. It is used to load this file as a C++ string.
--)luastring"--"
